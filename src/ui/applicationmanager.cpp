#include "applicationmanager.h"
#include "../core/deployerfactory.h"
#include "../core/installer.h"
#include "../core/pathutils.h"
#include <QCoreApplication>
#include <QDebug>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUrl>
#include <algorithm>
#include <chrono>
#include <limits>
#include <fstream>
#include <regex>

namespace sfs = std::filesystem;
namespace pu = path_utils;

bool performDownload(ImportModInfo& info, ApplicationManager* app_mgr)
{
  // Security: the remote URL may carry a signed token / expiry in its query string; strip the
  // query before logging so those credentials are not written to the debug log.
  QUrl log_url(QString::fromStdString(info.remote_download_url));
  const std::string log_url_str =
    log_url.isValid()
      ? log_url.toString(QUrl::RemoveQuery | QUrl::RemoveUserInfo).toStdString()
      : info.remote_download_url;
  app_mgr->sendLogMessage(Log::LOG_DEBUG,
                          std::format("Downloading from : '{}'", log_url_str));
  std::regex url_regex(R"(.*/([^/?]+)(?:\?.*)?$)");
  std::smatch match;
  if(!std::regex_match(info.remote_download_url, match, url_regex))
    throw std::runtime_error(std::format("Invalid download URL \"{}\"", info.remote_download_url));
  sfs::path download_path = info.target_path;
  if(!sfs::exists(download_path))
    sfs::create_directories(download_path);
  // Security: a malicious/compromised CDN could return a URL whose last path segment is empty
  // or "..", redirecting the write outside the download directory. Use the basename only.
  sfs::path file_name = sfs::path(match[1].str()).filename();
  // fork #233/#114: for pre-resolved direct downloads (paste-a-link importer, OMM repository)
  // the URL's last segment may not be a usable archive name — e.g. GitHub's .../zipball/<tag>.
  // Prefer the file name the resolver supplied, still sanitized down to a bare basename.
  if(info.remote_type != ImportModInfo::nexus && !info.remote_file_name.empty())
  {
    const sfs::path resolved_name = sfs::path(info.remote_file_name).filename();
    if(!resolved_name.empty() && resolved_name != "..")
      file_name = resolved_name;
  }
  if(file_name.empty() || file_name == "..")
    throw std::runtime_error(
      std::format("Invalid file name in download URL \"{}\"", info.remote_download_url));
  const std::string file_name_prefix = file_name.stem();
  const std::string extension = file_name.extension();
  int suffix = 1;
  while(pu::exists(download_path / file_name))
  {
    file_name = file_name_prefix + "(" + std::to_string(suffix) + ")" + extension;
    suffix++;
  }
  std::string file_name_str = file_name.string();
  auto pos = file_name_str.find("%20");
  while(pos != std::string::npos)
  {
    file_name_str.replace(pos, 3, " ");
    pos = file_name_str.find("%20");
  }
  file_name = file_name_str;

  auto progress_callback = [app_mgr](float progress) { app_mgr->sendUpdateProgress(progress); };
  std::ofstream fstream(download_path / file_name, std::ios::binary);
  if(!fstream.is_open())
    throw std::runtime_error("Failed to write to disk.");
  bool message_sent = false;
  // fork #8: track start time so the persistent download queue / DownloadsWidget can
  // be updated with byte counts and an approximate speed, and so the active download
  // can be aborted on cancellation.
  const auto download_start = std::chrono::steady_clock::now();
  // fork #140: apply the configurable download bandwidth cap (KB/s, 0 = unlimited).
  QSettings download_settings(QCoreApplication::applicationName());
  const int download_limit_kbps = download_settings.value("download_speed_limit_kbps", 0).toInt();
  const cpr::LimitRate limit_rate(
    download_limit_kbps > 0 ? static_cast<std::int64_t>(download_limit_kbps) * 1024 : 0, 0);
  // fork #233: the paste-a-link importer may require a browser User-Agent and a Referer
  // (ModHub's CDN enforces hotlink protection). These are empty for ordinary downloads.
  cpr::Header download_header;
  if(!info.download_user_agent.empty())
    download_header["User-Agent"] = info.download_user_agent;
  if(!info.download_referer.empty())
    download_header["Referer"] = info.download_referer;
  cpr::Response response = cpr::Download(
    fstream,
    cpr::Url(info.remote_download_url),
    download_header,
    limit_rate,
    cpr::ProgressCallback(
      [app_mgr, &message_sent, &file_name, &download_start, progress_callback](
        auto download_total,
        auto download_now,
        auto upload_total,
        auto upload_now,
        intptr_t user_data)
      {
        if(!message_sent && download_total > 0)
        {
          std::string size_string;
          long long last_size = 0;
          long long size = download_total;
          int exp = 0;
          const std::vector<std::string> units{ "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
          while(size > 1024 && exp < static_cast<int>(units.size()))
          {
            last_size = size;
            size /= 1024;
            exp++;
          }
          last_size /= 1.024;
          size_string = std::to_string(size);
          const int first_digit = (last_size / 100) % 10;
          const int second_digit = (last_size / 10) % 10;
          if(first_digit != 0 || second_digit != 0)
            size_string += "." + std::to_string(first_digit);
          if(second_digit != 0)
            size_string += std::to_string(second_digit);
          size_string += units[exp];

          app_mgr->sendLogMessage(
            Log::LOG_INFO,
            ("Downloading \"" + file_name.string() + "\" with size: ").c_str() + size_string +
              "...");
          message_sent = true;
        }
        if(download_total != 0)
          progress_callback((float)download_now / (float)download_total);
        // fork #8: report byte progress + speed to the persistent queue. Returning
        // false here aborts the cpr transfer, which is how cancellation is honored.
        double speed = 0.0;
        const auto elapsed =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - download_start).count();
        if(elapsed > 0.0)
          speed = (double)download_now / elapsed;
        return app_mgr->reportDownloadProgress(download_now, download_total, speed);
      }));
  // fork #8: a user-requested cancellation aborts the transfer; surface it as an error
  // so downloadMod marks the item cancelled rather than complete.
  if(app_mgr->downloadCancelRequested())
  {
    fstream.close();
    sfs::remove(download_path / file_name);
    throw std::runtime_error("Download cancelled.");
  }
  if(response.status_code != 200)
  {
    sfs::remove(download_path / file_name);
    throw std::runtime_error("Download failed with response: \"" + response.status_line +
                             "\" (code " + std::to_string(response.status_code) + ").");
  }
  fstream.close();
  info.local_source = download_path / file_name;
  info.current_path = info.local_source;
  return true;
}

bool performExtraction(ImportModInfo& info, ApplicationManager* app_mgr)
{
  info.last_action_was_successful = false;
  auto progress_callback = [app_mgr](float progress) { app_mgr->sendUpdateProgress(progress); };
  ProgressNode node(progress_callback);
  Installer::extract(info.local_source, info.target_path, &node);
  info.current_path = info.target_path;
  info.last_action_was_successful = true;
  return true;
}

ApplicationManager::ApplicationManager(QObject* parent) : QObject{ parent }
{
  if(number_of_instances_ > 0)
    throw std::runtime_error("Do not instantiate more than one ApplicationManager!");
  number_of_instances_++;
  Installer::log = [app_mgr = this](Log::LogLevel log_level, const std::string& message)
  { app_mgr->sendLogMessage(log_level, message); };
}

ApplicationManager::~ApplicationManager()
{
  number_of_instances_--;
}

void ApplicationManager::init()
{
  updateState();
  // fork #8: restore the persistent download queue from disk so queued/incomplete
  // downloads survive a restart.
  loadDownloadQueue();
  // fork #47: configure the scheduled / startup automatic update check. init() runs on the
  // creating (UI) thread before this object is moved to the worker thread, so defer the timer
  // setup with a queued invocation: it then executes on the worker thread's event loop, where
  // the QTimer fires and the (synchronous) update check runs off the UI thread.
  QMetaObject::invokeMethod(this, [this] { initAutoUpdateCheck(); }, Qt::QueuedConnection);
}

// ---- fork #47: scheduled / startup automatic mod-update checks ----------------
void ApplicationManager::initAutoUpdateCheck()
{
  QSettings settings(QCoreApplication::applicationName());
  auto_update_check_enabled_ = settings.value(AUTO_UPDATE_ENABLED_KEY, false).toBool();
  auto_update_check_interval_hours_ =
    std::max(1, settings.value(AUTO_UPDATE_INTERVAL_KEY, 24).toInt());
  applyAutoUpdateCheckConfig();
  if(auto_update_check_enabled_)
  {
    // Startup auto-check: kick off one check shortly after launch (deferred so it does not
    // block init), reusing the regular update-check path for the active app.
    QMetaObject::invokeMethod(this, [this] { runScheduledUpdateCheck(); }, Qt::QueuedConnection);
  }
}

void ApplicationManager::applyAutoUpdateCheckConfig()
{
  if(!auto_update_timer_)
  {
    auto_update_timer_ = new QTimer(this);
    auto_update_timer_->setSingleShot(false);
    connect(auto_update_timer_, &QTimer::timeout, this,
            [this] { runScheduledUpdateCheck(); });
  }
  if(auto_update_check_enabled_)
  {
    const int interval_hours = std::max(1, auto_update_check_interval_hours_.load());
    // milliseconds; clamp the interval into QTimer's int range to avoid overflow.
    const qint64 interval_ms = static_cast<qint64>(interval_hours) * 60 * 60 * 1000;
    auto_update_timer_->setInterval(
      static_cast<int>(std::min<qint64>(interval_ms, std::numeric_limits<int>::max())));
    auto_update_timer_->start();
  }
  else
    auto_update_timer_->stop();
}

void ApplicationManager::runScheduledUpdateCheck()
{
  if(!auto_update_check_enabled_)
    return;
  // Avoid hammering the API: skip if a previous scheduled check is still running.
  if(auto_update_check_in_progress_)
    return;
  if(!appIndexIsValid(auto_update_check_app_id_, false))
    return;
  auto_update_check_in_progress_ = true;
  sendLogMessage(Log::LOG_DEBUG,
                 std::string("fork #47: running scheduled mod-update check"));
  // Reuse the existing update-check path; this emits the usual update-available signalling
  // (via completedOperations / ModdedApplication state) so the UI's normal "updates
  // available" indication fires. Runs on this (worker) thread, not the UI thread.
  // Clear the in-progress guard even if checkForModUpdates throws, so future scheduled
  // checks are not permanently blocked.
  try
  {
    checkForModUpdates(auto_update_check_app_id_);
  }
  catch(...)
  {
    auto_update_check_in_progress_ = false;
    throw;
  }
  auto_update_check_in_progress_ = false;
}

void ApplicationManager::setAutoUpdateCheck(bool enabled, int interval_hours)
{
  // Marshal onto the owning thread so timer (re)configuration is thread-safe regardless of
  // which thread the caller (e.g. a settings dialog on the UI thread) invokes this from.
  const int hours = std::max(1, interval_hours);
  QMetaObject::invokeMethod(
    this,
    [this, enabled, hours]
    {
      auto_update_check_enabled_ = enabled;
      auto_update_check_interval_hours_ = hours;
      QSettings settings(QCoreApplication::applicationName());
      settings.setValue(AUTO_UPDATE_ENABLED_KEY, enabled);
      settings.setValue(AUTO_UPDATE_INTERVAL_KEY, hours);
      applyAutoUpdateCheckConfig();
    },
    Qt::QueuedConnection);
}

bool ApplicationManager::autoUpdateCheckEnabled() const
{
  return auto_update_check_enabled_;
}

int ApplicationManager::autoUpdateCheckIntervalHours() const
{
  return auto_update_check_interval_hours_;
}

void ApplicationManager::setAutoUpdateCheckApp(int app_id)
{
  QMetaObject::invokeMethod(
    this, [this, app_id] { auto_update_check_app_id_ = app_id; }, Qt::QueuedConnection);
}
// ---- End fork #47 ------------------------------------------------------------

void ApplicationManager::sendLogMessage(Log::LogLevel log_level, const std::string& message)
{
  emit logMessage(log_level, message.c_str());
}

std::string ApplicationManager::toString() const
{
  std::string summary = "";
  for(int i = 0; const auto& app : apps_)
  {
    summary += "[" + std::to_string(i) + "] " + app.name() + "\n";
    for(int j = 0; const auto& profile : app.getProfileNames())
    {
      summary += "\t[" + std::to_string(j) + "] " + profile + "\n";
      j++;
    }
    i++;
  }
  return summary;
}

int ApplicationManager::getNumApplications() const
{
  return apps_.size();
}

// fork #24: minimal synchronous passthrough for the save-game manager.
std::string ApplicationManager::getStagingDir(int app_id) const
{
  if(app_id < 0 || app_id >= static_cast<int>(apps_.size()))
    return "";
  return apps_[app_id].getStagingDir().string();
}

int ApplicationManager::getNumProfiles(int app_id) const
{
  if(app_id >= 0 && app_id < apps_.size())
    return apps_[app_id].getProfileNames().size();
  return 0;
}

void ApplicationManager::enableExceptions(bool enabled)
{
  throw_exceptions_ = enabled;
}

void ApplicationManager::updateSettings()
{
  QSettings settings(QCoreApplication::applicationName());
  settings.beginWriteArray("staging_directories");
  for(int i = 0; i < apps_.size(); i++)
  {
    settings.setArrayIndex(i);
    settings.setValue(QString::number(i), apps_[i].getStagingDir().c_str());
  }
  settings.endArray();
}

void ApplicationManager::updateState()
{
  apps_.clear();
  QSettings settings(QCoreApplication::applicationName());
  int num_apps = settings.beginReadArray("staging_directories");
  for(int i = 0; i < num_apps; i++)
  {
    sfs::path staging_dir;
    settings.setArrayIndex(i);
    if(settings.contains(QString::number(i)))
      staging_dir = settings.value(QString::number(i)).toString().toStdString();
    else
    {
      handleParseError(settings.fileName().toStdString(), "Could not parse staging directories.");
      continue;
    }
    int code = ModdedApplication::verifyStagingDir(staging_dir);
    if(code != 0)
    {
      handleAddAppError(code, staging_dir);
      continue;
    }
    try
    {
      apps_.emplace_back(staging_dir);
      apps_.back().setProgressCallback([app_mgr = this](float p)
                                       { app_mgr->sendUpdateProgress(p); });
      apps_.back().setLog([app_mgr = this](Log::LogLevel log_level, const std::string& message)
                          { app_mgr->sendLogMessage(log_level, message); });
    }
    catch(Json::RuntimeError& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
      continue;
    }
    catch(Json::LogicError& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
      continue;
    }
    catch(ParseError& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
      continue;
    }
    catch(sfs::filesystem_error& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
      continue;
    }
    catch(std::runtime_error& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
      continue;
    }
    catch(std::invalid_argument& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
      continue;
    }
    catch(std::logic_error& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
      continue;
    }
    catch(...)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(),
                       "Unexpected error!");
      continue;
    }
  }
  settings.endArray();
}

bool ApplicationManager::appIndexIsValid(int app_id, bool show_error)
{
  if(app_id >= 0 && app_id < apps_.size())
    return true;
  if(show_error)
    emit sendError("Error", "App index \"" + QString::number(app_id) + "\" out of range!");
  return false;
}

bool ApplicationManager::deployerIndexIsValid(int app_id, int deployer, bool show_error)
{
  if(app_id < 0 || app_id >= static_cast<int>(apps_.size()))
    return false;
  if(deployer >= 0 && deployer < apps_[app_id].getNumDeployers())
    return true;
  if(show_error)
    emit sendError("Error", "Deployer index \"" + QString::number(deployer) + "\" out of range!");
  return false;
}

void ApplicationManager::handleAddAppError(int code, sfs::path staging_dir)
{
  if(code == 1)
    emit sendError(
      "Error", "Could not read from config file in " + QString(staging_dir.string().c_str()) + "!");
  else
    emit sendError("Error",
                   "Could not parse config file in " + QString(staging_dir.string().c_str()) + "!");
  emit completedOperations();
}

void ApplicationManager::handleAddDeployerError(int code,
                                                sfs::path staging_dir,
                                                sfs::path dest_dir,
                                                const std::string& error_message)
{
  if(code == 1)
    emit sendError("Error",
                   "Could not write to staging dir " + QString(staging_dir.string().c_str()) + "!");
  else if(code == 2)
    emit sendError(
      "Error",
      "Could not create hard link from\n\"" + QString(staging_dir.string().c_str()) + "\"\nto\n\"" +
        QString(dest_dir.string().c_str()) + "\".\n" +
        "Ensure that both directories are on the same partition!\n" "Alternatively: " "Switch to " "sym " "link deployment.");
  else if(code == 3)
    emit sendError("Error",
                   "Could no copy files\n\"" + QString(staging_dir.string().c_str()) +
                     "\"\nto\n\"" + QString(dest_dir.string().c_str()) + "\"!");
  if(code != 0)
    emit logMessage(Log::LOG_ERROR, error_message.c_str());
}

void ApplicationManager::handleParseError(std::string path, std::string message)
{
  emit sendError("Error",
                 ("Error parsing settings file in \"" + path + "\".\n" + message +
                  "\n\n A backup of the last known good state, named \"." +
                  ModdedApplication::CONFIG_FILE_NAME + ".bak\", exists in the same directory.")
                   .c_str());
  emit completedOperations();
}

void ApplicationManager::sendUpdateProgress(float progress)
{
  emit updateProgress(progress);
}

void ApplicationManager::sendLogMessage(Log::LogLevel level, QString message)
{
  emit logMessage(level, message);
}

void ApplicationManager::addApplication(EditApplicationInfo info)
{
  sfs::path staging_dir{ info.staging_dir };
  int code = ModdedApplication::verifyStagingDir(staging_dir);
  if(code == 0)
  {
    try
    {
      apps_.emplace_back(staging_dir, info.name, info.command, info.icon_path, info.app_version);
      apps_.back().setProgressCallback([app_mgr = this](float p)
                                       { app_mgr->sendUpdateProgress(p); });
      apps_.back().setLog([app_mgr = this](Log::LogLevel log_level, const std::string& message)
                          { app_mgr->sendLogMessage(log_level, message); });

      for(const auto& depl_info : info.deployers)
        apps_.back().addDeployer(depl_info);
      apps_.back().fixInvalidHardLinkDeployers();
      for(const auto& tag : info.auto_tags)
        apps_.back().addAutoTag(tag, true);
      updateSettings();
      emit completedOperations("Application added");
    }
    catch(Json::RuntimeError& error)
    {
      handleParseError(staging_dir / ModdedApplication::CONFIG_FILE_NAME, error.what());
    }
    catch(Json::LogicError& error)
    {
      handleParseError(staging_dir / ModdedApplication::CONFIG_FILE_NAME, error.what());
    }
    catch(ParseError& error)
    {
      handleParseError(staging_dir / ModdedApplication::CONFIG_FILE_NAME, error.what());
    }
    catch(sfs::filesystem_error& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
    }
    catch(std::runtime_error& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
    }
    catch(std::invalid_argument& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
    }
    catch(std::logic_error& error)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(), error.what());
    }
    catch(...)
    {
      handleParseError((staging_dir / ModdedApplication::CONFIG_FILE_NAME).string(),
                       "Unexpected error while adding application!");
    }
  }
  else
    handleAddAppError(code, staging_dir);
}

void ApplicationManager::removeApplication(int app_id, bool cleanup)
{
  if(!appIndexIsValid(app_id))
    return;
  if(cleanup)
    handleExceptions<&ModdedApplication::deleteAllData>(app_id);
  apps_.erase(apps_.begin() + app_id);
  updateSettings();
}

void ApplicationManager::deployMods(int app_id)
{
  // fork audit: only report "Mods deployed" when deployment actually ran and succeeded. If the
  // app id is invalid, directory verification reported an error, or the deploy threw (an error
  // dialog is surfaced by handleAddDeployerError / handleExceptions), emit a neutral completion.
  bool deployed = false;
  if(appIndexIsValid(app_id))
  {
    auto ret_val = handleExceptions(&ModdedApplication::verifyDeployerDirectories, apps_[app_id]);
    if(ret_val)
    {
      auto [code, path, message] = *ret_val;
      handleAddDeployerError(code, apps_[app_id].getStagingDir(), path, message);
      if(code == 0)
        deployed = !handleExceptions<&ModdedApplication::deployMods>(app_id);
    }
  }
  if(deployed)
    emit completedOperations("Mods deployed");
  else
    emit completedOperations();
}

void ApplicationManager::deployModsFor(int app_id, std::vector<int> deployer_ids)
{
  if(appIndexIsValid(app_id))
  {
    for(int deployer : deployer_ids)
    {
      if(!deployerIndexIsValid(app_id, deployer))
      {
        emit completedOperations();
        return;
      }
    }
    auto ret_val = handleExceptions(&ModdedApplication::verifyDeployerDirectories, apps_[app_id]);
    if(ret_val)
    {
      auto [code, path, message] = *ret_val;
      handleAddDeployerError(code, apps_[app_id].getStagingDir(), path, message);
      if(code == 0)
        handleExceptions<&ModdedApplication::deployModsFor>(app_id, deployer_ids);
    }
  }
  emit completedOperations("Mods deployed");
}

void ApplicationManager::unDeployMods(int app_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::unDeployMods>(app_id);
  emit completedOperations("Mods undeployed");
}

// fork #208: purge (undeploy) every deployer then deploy from scratch, to recover from
// drift or external tampering with the deployed links/files.
void ApplicationManager::forceRedeployMods(int app_id)
{
  if(appIndexIsValid(app_id))
  {
    handleExceptions<&ModdedApplication::unDeployMods>(app_id);
    auto ret_val = handleExceptions(&ModdedApplication::verifyDeployerDirectories, apps_[app_id]);
    if(ret_val)
    {
      auto [code, path, message] = *ret_val;
      handleAddDeployerError(code, apps_[app_id].getStagingDir(), path, message);
      if(code == 0)
        handleExceptions<&ModdedApplication::deployMods>(app_id);
    }
  }
  emit completedOperations("Mods redeployed");
}

void ApplicationManager::unDeployModsFor(int app_id, std::vector<int> deployer_ids)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::unDeployModsFor>(app_id, deployer_ids);
  emit completedOperations("Mods undeployed");
}

void ApplicationManager::installMod(int app_id, ImportModInfo info)
{
  bool has_thrown = false;
  if(appIndexIsValid(app_id))
  {
    has_thrown = handleExceptions<&ModdedApplication::installMod>(app_id, info);
    if(has_thrown)
      handleExceptions<&ModdedApplication::cleanupFailedInstallation>(app_id);
  }
  emit modInstallationComplete(!has_thrown);
}

void ApplicationManager::uninstallMods(int app_id,
                                       std::vector<int> mod_ids,
                                       std::string installer_type)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::uninstallMods>(app_id, mod_ids, installer_type);
  emit completedOperations(std::format("Mod{} removed", mod_ids.size() == 1 ? "" : "s").c_str());
}

// fork #148
void ApplicationManager::mergeMods(int app_id,
                                   std::vector<int> source_mod_ids,
                                   int target_mod_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::mergeMods>(app_id, source_mod_ids, target_mod_id);
  emit completedOperations("Mods merged");
}

void ApplicationManager::setUpdateIgnored(int app_id, int mod_id, bool ignored)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setUpdateIgnored>(app_id, mod_id, ignored);
}

void ApplicationManager::exportModArchive(int app_id, int mod_id, std::filesystem::path target)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::exportModArchive>(app_id, mod_id, target);
  emit completedOperations("Mod exported");
}

void ApplicationManager::commitChanges(int app_id, int deployer)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
    handleExceptions<&ModdedApplication::commitChanges>(app_id);
}

void ApplicationManager::updateModDeployers(int app_id,
                                            std::vector<int> mod_ids,
                                            std::vector<bool> deployers)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::updateModDeployers>(app_id, mod_ids, deployers);
  emit completedOperations("Deployers updated");
}

void ApplicationManager::removeNodeFromDeployer(int app_id, int deployer, void *node_ptr)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
    handleExceptions<&ModdedApplication::removeNodeFromDeployer>(
      app_id, deployer, node_ptr, true, std::optional<ProgressNode*>{});
  emit completedOperations("Deployers updated");
}

void ApplicationManager::setModStatus(int app_id, int deployer, int mod_id, bool status)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
    handleExceptions<&ModdedApplication::setModStatus>(app_id, deployer, mod_id, status);
}

void ApplicationManager::addDeployer(int app_id, EditDeployerInfo info)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::addDeployer>(app_id, info);
  emit completedOperations("Deployer added");
}

void ApplicationManager::removeDeployer(int app_id, int deployer, bool cleanup)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
    handleExceptions<&ModdedApplication::removeDeployer>(app_id, deployer, cleanup);
}

void ApplicationManager::getDeployerNames(int app_id, bool is_new)
{
  QStringList names{};
  if(appIndexIsValid(app_id, false))
  {
    for(const auto& name : apps_[app_id].getDeployerNames())
      names << name.c_str();
  }
  emit sendDeployerNames(names, is_new);
}

void ApplicationManager::getModInfo(int app_id)
{
  if(appIndexIsValid(app_id, false))
  {
    auto info = handleExceptions(&ModdedApplication::getModInfo, apps_[app_id]);
    if(info)
    {
      emit sendModInfo(*info);
      return;
    }
  }
  emit sendModInfo(std::vector<ModInfo>{});
}

void ApplicationManager::getDeployerInfo(int app_id, int deployer)
{
  if(appIndexIsValid(app_id, false) && deployerIndexIsValid(app_id, deployer, false))
  {
    auto info = handleExceptions(&ModdedApplication::getDeployerInfo, apps_[app_id], deployer);
    if(info)
    {
      emit sendDeployerInfo(*info);
      return;
    }
  }
  emit sendDeployerInfo(DeployerInfo{});
}

void ApplicationManager::getApplicationNames(bool is_new)
{
  QStringList names{};
  QStringList icon_paths{};
  for(const auto& app : apps_)
  {
    names.append(app.name().c_str());
    icon_paths.append(app.iconPath().string().c_str());
  }
  emit sendApplicationNames(names, icon_paths, is_new);
}

void ApplicationManager::getSteamAppIds()
{
  QList<int> ids;
  for(const auto& app : apps_)
    ids.append(static_cast<int>(app.getSteamAppId()));
  emit sendSteamAppIds(ids);
}

void ApplicationManager::changeModName(int app_id, int mod_id, QString new_name)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::changeModName>(app_id, mod_id, new_name.toStdString());
}

// fork #66
void ApplicationManager::updateModFromLocal(int app_id,
                                            int mod_id,
                                            std::filesystem::path source_archive)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::updateModFromLocal>(app_id, mod_id, source_archive);
  emit completedOperations("Mod files updated");
}

void ApplicationManager::getFileConflicts(int app_id, int deployer, int mod_id, bool show_disabled)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
  {
    auto conflicts = handleExceptions(
      &ModdedApplication::getFileConflicts, apps_[app_id], deployer, mod_id, show_disabled);
    if(conflicts)
      emit sendFileConflicts(*conflicts);
  }
  emit completedOperations();
}

void ApplicationManager::getAppInfo(int app_id)
{
  if(!appIndexIsValid(app_id, false))
  {
    emit sendAppInfo(AppInfo{});
    return;
  }
  emit sendAppInfo(apps_[app_id].getAppInfo());
}

void ApplicationManager::addTool(int app_id, Tool tool)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::addTool>(app_id, tool);
}

void ApplicationManager::removeTool(int app_id, int tool_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::removeTool>(app_id, tool_id);
}

void ApplicationManager::editApplication(EditApplicationInfo info, int app_id)
{
  if(appIndexIsValid(app_id))
  {
    if(!handleExceptions<&ModdedApplication::setStagingDir>(
         app_id, info.staging_dir, info.move_staging_dir))
    {
      handleExceptions<&ModdedApplication::setName>(app_id, info.name);
      handleExceptions<&ModdedApplication::setCommand>(app_id, info.command);
      handleExceptions<&ModdedApplication::setIconPath>(app_id, info.icon_path);
      handleExceptions<&ModdedApplication::setAppVersion>(app_id, info.app_version);
    }
    updateSettings();
  }
}

void ApplicationManager::editDeployer(EditDeployerInfo info, int app_id, int deployer)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
    handleExceptions<&ModdedApplication::editDeployer>(app_id, deployer, info);
}

void ApplicationManager::getModConflicts(int app_id, int deployer, int mod_id)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
  {
    auto conflicts =
      handleExceptions(&ModdedApplication::getModConflicts, apps_[app_id], deployer, mod_id);
    if(conflicts)
      emit sendModConflicts(*conflicts);
  }
  emit completedOperations();
}

void ApplicationManager::setProfile(int app_id, int profile)
{
  if(appIndexIsValid(app_id, false))
    handleExceptions<&ModdedApplication::setProfile>(app_id, profile);
}

void ApplicationManager::setPackActive(int app_id, QString pack, bool active)
{
  if(appIndexIsValid(app_id, false))
    handleExceptions<&ModdedApplication::setPackActive>(app_id, pack.toStdString(), active);
}

void ApplicationManager::addPack(int app_id, QString name, QString notes)
{
  if(appIndexIsValid(app_id, false))
    handleExceptions<&ModdedApplication::addPack>(app_id, name.toStdString(), notes.toStdString());
}

void ApplicationManager::removePack(int app_id, QString name)
{
  if(appIndexIsValid(app_id, false))
    handleExceptions<&ModdedApplication::removePack>(app_id, name.toStdString());
}

void ApplicationManager::renamePack(int app_id, QString old_name, QString new_name)
{
  if(appIndexIsValid(app_id, false))
    handleExceptions<&ModdedApplication::renamePack>(
      app_id, old_name.toStdString(), new_name.toStdString());
}

void ApplicationManager::setPackNotes(int app_id, QString name, QString notes)
{
  if(appIndexIsValid(app_id, false))
    handleExceptions<&ModdedApplication::setPackNotes>(
      app_id, name.toStdString(), notes.toStdString());
}

void ApplicationManager::setPackMods(int app_id, QString name, QList<int> mod_ids)
{
  if(!appIndexIsValid(app_id, false))
    return;
  std::vector<int> ids(mod_ids.begin(), mod_ids.end());
  handleExceptions<&ModdedApplication::setPackMods>(app_id, name.toStdString(), ids);
}

void ApplicationManager::getPackInfo(int app_id)
{
  if(!appIndexIsValid(app_id, false))
    return;
  QJsonObject root;
  QJsonArray active;
  for(const auto& name : apps_[app_id].getActivePacks())
    active.append(QString::fromStdString(name));
  root["active"] = active;
  QJsonArray packs;
  for(const Pack& pack : apps_[app_id].getPacks())
  {
    QJsonObject pack_obj;
    pack_obj["name"] = QString::fromStdString(pack.name);
    pack_obj["notes"] = QString::fromStdString(pack.notes);
    QJsonArray mods;
    for(int mod_id : pack.mod_ids)
      mods.append(mod_id);
    pack_obj["mods"] = mods;
    packs.append(pack_obj);
  }
  root["packs"] = packs;
  QJsonArray all_mods;
  for(const ModInfo& mod_info : apps_[app_id].getModInfo())
  {
    QJsonObject mod_obj;
    mod_obj["id"] = mod_info.mod.id;
    mod_obj["name"] = QString::fromStdString(mod_info.mod.name);
    all_mods.append(mod_obj);
  }
  root["mods"] = all_mods;
  emit sendPackInfo(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

void ApplicationManager::addProfile(int app_id, EditProfileInfo info)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::addProfile>(app_id, info);
}

void ApplicationManager::removeProfile(int app_id, int profile)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::removeProfile>(app_id, profile);
}

void ApplicationManager::getProfileNames(int app_id, bool is_new)
{
  QStringList names{};
  if(appIndexIsValid(app_id, false))
  {
    for(const auto& name : apps_[app_id].getProfileNames())
      names << name.c_str();
  }
  emit sendProfileNames(names, is_new);
}

void ApplicationManager::editProfile(int app_id, int profile, EditProfileInfo info)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::editProfile>(app_id, profile, info);
}

void ApplicationManager::editTool(int app_id, int tool_id, Tool new_tool)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::editTool>(app_id, tool_id, new_tool);
}

void ApplicationManager::addModToGroup(int app_id, int mod_id, int group)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::addModToGroup>(
      app_id, mod_id, group, std::optional<ProgressNode*>{});
  emit completedOperations("Mod added to group");
}

void ApplicationManager::removeModFromGroup(int app_id, int mod_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::removeModFromGroup>(
      app_id, mod_id, true, std::optional<ProgressNode*>{});
  emit completedOperations("Mod removed from group");
}

void ApplicationManager::createGroup(int app_id, int first_mod_id, int second_mod_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::createGroup>(
      app_id, first_mod_id, second_mod_id, std::optional<ProgressNode*>{});
  emit completedOperations("Mod added to group");
}

void ApplicationManager::changeActiveGroupMember(int app_id, int group, int mod_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::changeActiveGroupMember>(
      app_id, group, mod_id, std::optional<ProgressNode*>{});
  emit completedOperations();
}

void ApplicationManager::changeModVersion(int app_id, int mod_id, QString new_version)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::changeModVersion>(
      app_id, mod_id, new_version.toStdString());
}

void ApplicationManager::setModNote(int app_id, int mod_id, QString note)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setModNote>(app_id, mod_id, note.toStdString());
}

// fork #199: pass-throughs for per-mod highlight colour labels.
void ApplicationManager::setModColor(int app_id, int mod_id, QString color)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setModColor>(app_id, mod_id, color.toStdString());
}

QString ApplicationManager::getModColor(int app_id, int mod_id)
{
  if(!appIndexIsValid(app_id, false))
    return {};
  auto color = handleExceptions(&ModdedApplication::getModColor, apps_[app_id], mod_id);
  if(color)
    return QString::fromStdString(*color);
  return {};
}

std::map<int, std::string> ApplicationManager::getModColors(int app_id)
{
  // fork audit: read-only getter, do not surface an error dialog on a stale/invalid app id.
  if(!appIndexIsValid(app_id, false))
    return {};
  auto colors = handleExceptions(&ModdedApplication::getModColors, apps_[app_id]);
  if(colors)
    return *colors;
  return {};
}

// fork #198: pass-throughs for per-mod free-text categories.
void ApplicationManager::setModCategory(int app_id, int mod_id, QString category)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setModCategory>(app_id, mod_id, category.toStdString());
}

QString ApplicationManager::getModCategory(int app_id, int mod_id)
{
  // fork audit: read-only getter, do not surface an error dialog on a stale/invalid app id.
  if(!appIndexIsValid(app_id, false))
    return {};
  auto category = handleExceptions(&ModdedApplication::getModCategory, apps_[app_id], mod_id);
  if(category)
    return QString::fromStdString(*category);
  return {};
}

// fork #145: pass-throughs for bulk prune of outdated mod archive versions.
std::pair<std::vector<PrunableArchive>, unsigned long>
ApplicationManager::getPrunableArchives(int app_id)
{
  // fork audit: read-only getter, do not surface an error dialog on a stale/invalid app id.
  if(!appIndexIsValid(app_id, false))
    return {};
  auto result = handleExceptions(&ModdedApplication::getPrunableArchives, apps_[app_id]);
  if(result)
    return *result;
  return {};
}

void ApplicationManager::requestPrunableArchives(int app_id)
{
  auto [archives, total] = getPrunableArchives(app_id);
  emit sendPrunableArchives(archives, total, app_id);
}

void ApplicationManager::pruneArchives(int app_id,
                                       const std::vector<std::filesystem::path>& paths)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::pruneArchives>(app_id, paths);
}

void ApplicationManager::setModPinned(int app_id, int mod_id, bool pinned)
{
  if(!appIndexIsValid(app_id))
    return;
  if(pinned)
    handleExceptions<&ModdedApplication::pinModVersion>(app_id, mod_id);
  else
    handleExceptions<&ModdedApplication::unpinModVersion>(app_id, mod_id);
}

void ApplicationManager::getModRulesFor(int app_id, int mod_id)
{
  // fork audit: read-only getter, do not surface an error dialog on a stale/invalid app id.
  if(!appIndexIsValid(app_id, false))
    return;
  auto rules = handleExceptions(&ModdedApplication::getModRulesFor, apps_[app_id], mod_id);
  if(rules)
    emit sendModRules(app_id, mod_id, *rules);
}

void ApplicationManager::setModRulesFor(int app_id, int source_mod_id, std::vector<ModRule> rules)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setModRulesFor>(app_id, source_mod_id, rules);
  emit completedOperations("Mod rules updated");
}

void ApplicationManager::getGroupData(int app_id)
{
  // fork audit: read-only getter, do not surface an error dialog on a stale/invalid app id.
  if(!appIndexIsValid(app_id, false))
    return;
  std::vector<std::string> names;
  std::vector<std::string> notes;
  std::vector<std::vector<int>> members;
  std::vector<int> active;
  const auto metadata = apps_[app_id].getGroupMetadata();
  for(int g = 0; g < static_cast<int>(metadata.size()); g++)
  {
    names.push_back(metadata[g].first);
    notes.push_back(metadata[g].second);
    members.push_back(apps_[app_id].getGroupMembers(g));
    active.push_back(apps_[app_id].getActiveGroupMember(g));
  }
  emit sendGroupData(app_id, names, notes, members, active);
}

void ApplicationManager::setGroupName(int app_id, int group, QString name)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setGroupName>(app_id, group, name.toStdString());
}

void ApplicationManager::setGroupNotes(int app_id, int group, QString notes)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setGroupNotes>(app_id, group, notes.toStdString());
}

void ApplicationManager::dissolveGroup(int app_id, int group)
{
  if(appIndexIsValid(app_id))
  {
    const auto members = apps_[app_id].getGroupMembers(group);
    for(int mod_id : members)
      handleExceptions<&ModdedApplication::removeModFromGroup>(
        app_id, mod_id, true, std::optional<ProgressNode*>{});
  }
  emit completedOperations("Group dissolved");
}

void ApplicationManager::mergeTw3Scripts(int app_id, int deployer)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
  {
    auto result = handleExceptions(&ModdedApplication::mergeTw3Scripts, apps_[app_id], deployer);
    if(result)
      emit sendGameToolResult("Witcher 3 Script Merge", result->c_str());
  }
  emit completedOperations();
}

// fork #59: pass-throughs for the configured vanilla Witcher 3 scripts root.
void ApplicationManager::setTw3VanillaScriptsRoot(int app_id, QString path)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setTw3VanillaScriptsRoot>(app_id, path.toStdString());
}

QString ApplicationManager::getTw3VanillaScriptsRoot(int app_id)
{
  // fork audit: read-only getter, do not surface an error dialog on a stale/invalid app id.
  if(!appIndexIsValid(app_id, false))
    return {};
  auto path = handleExceptions(&ModdedApplication::getTw3VanillaScriptsRoot, apps_[app_id]);
  if(path)
    return QString::fromStdString(*path);
  return {};
}

void ApplicationManager::mergeTw3Config(int app_id, int deployer)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
  {
    auto result = handleExceptions(&ModdedApplication::mergeTw3Config, apps_[app_id], deployer);
    if(result)
      emit sendGameToolResult("Witcher 3 Config Merge", result->c_str());
  }
  emit completedOperations();
}

void ApplicationManager::getCyberpunkSetupInfo(int app_id, int deployer)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
  {
    auto result =
      handleExceptions(&ModdedApplication::getCyberpunkSetupInfo, apps_[app_id], deployer);
    if(result)
      emit sendGameToolResult("Cyberpunk 2077 Setup", result->c_str());
  }
  emit completedOperations();
}

void ApplicationManager::deployRedMods(int app_id, int deployer)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
  {
    auto command =
      handleExceptions(&ModdedApplication::buildRedmodDeployCommand, apps_[app_id], deployer);
    if(command)
    {
      if(command->empty())
        emit sendGameToolResult("Cyberpunk REDmod Deploy",
                                "No REDmods (mods containing an info.json) were found among the "
                                "enabled mods of this deployer.");
      else
        emit sendRunCommand("REDmod deploy", command->c_str());
    }
  }
  emit completedOperations();
}

void ApplicationManager::sortModsByConflicts(int app_id, int deployer)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
    handleExceptions<&ModdedApplication::sortModsByConflicts>(app_id, deployer);
  emit completedOperations("Mods sorted");
}

// fork #54: deploy restore points (load-order snapshots).
void ApplicationManager::createRestorePoint(int app_id, QString name)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::createRestorePoint>(app_id, name.toStdString());
  emit completedOperations("Restore point created");
}

void ApplicationManager::requestRestorePoints(int app_id)
{
  if(!appIndexIsValid(app_id))
    return;
  auto points = handleExceptions(&ModdedApplication::getRestorePoints, apps_[app_id]);
  if(points)
    emit sendRestorePoints(*points, app_id);
}

// fork #49: compute a dry-run deployment preview for the app and deliver it.
void ApplicationManager::requestDeploymentPreview(int app_id)
{
  if(!appIndexIsValid(app_id))
    return;
  auto plans = handleExceptions(&ModdedApplication::computeDeploymentPlans, apps_[app_id]);
  if(plans)
    emit sendDeploymentPlans(*plans, app_id);
}

// fork #212: gather LOOT dirty/clean info for the app's plugins and deliver it.
void ApplicationManager::requestPluginCleanInfo(int app_id)
{
  if(!appIndexIsValid(app_id))
    return;
  auto info = handleExceptions(&ModdedApplication::getPluginCleanInfo, apps_[app_id]);
  if(info)
    emit sendPluginCleanInfo(*info, app_id);
}

// fork #202: gather plugin ESM/ESL flag info from the app's plugin deployer and deliver it.
void ApplicationManager::requestPluginFlags(int app_id)
{
  if(!appIndexIsValid(app_id))
    return;
  auto plugins = handleExceptions(&ModdedApplication::getPluginFlagInfo, apps_[app_id]);
  if(plugins)
    emit sendPluginFlags(*plugins, app_id);
}

void ApplicationManager::restoreRestorePoint(int app_id, int index)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::restoreRestorePoint>(app_id, index);
  emit completedOperations("Restore point applied");
}

void ApplicationManager::deleteRestorePoint(int app_id, int index)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::deleteRestorePoint>(app_id, index);
  emit completedOperations("Restore point deleted");
}

std::vector<RestorePoint> ApplicationManager::getRestorePoints(int app_id)
{
  if(app_id < 0 || app_id >= static_cast<int>(apps_.size()))
    return {};
  return apps_[app_id].getRestorePoints();
}

void ApplicationManager::extractArchive(ImportModInfo info)
{
  // fork audit: only signal extractionComplete when extraction actually succeeded. On failure
  // handleExceptionsForFunction returns an empty optional (and has already surfaced an error
  // dialog), and info.last_action_was_successful stays false; emitting in that case would let
  // consumers proceed as if a valid archive had been extracted.
  auto result = handleExceptionsForFunction(performExtraction, info, this);
  if(result && *result && info.last_action_was_successful)
    emit extractionComplete(info);
}

void ApplicationManager::addBackupTarget(int app_id,
                                         QString path,
                                         QString name,
                                         QString default_backup,
                                         QString first_backup)
{
  if(appIndexIsValid(app_id))
  {
    std::vector<std::string> backup_names{ default_backup.toStdString() };
    if(!first_backup.isEmpty())
      backup_names.push_back(first_backup.toStdString());
    handleExceptions<&ModdedApplication::addBackupTarget>(
      app_id, path.toStdString(), name.toStdString(), backup_names);
  }
  emit completedOperations("Backup target added");
}

void ApplicationManager::removeBackupTarget(int app_id, int target_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::removeBackupTarget>(app_id, target_id);
  emit completedOperations("Backup target removed");
}

void ApplicationManager::addBackup(int app_id, int target_id, QString name, int source)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::addBackup>(app_id, target_id, name.toStdString(), source);
  emit completedOperations("Backup added");
}

void ApplicationManager::removeBackup(int app_id, int target_id, int backup_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::removeBackup>(app_id, target_id, backup_id);
  emit completedOperations("Backup removed");
}

void ApplicationManager::setActiveBackup(int app_id, int target_id, int backup_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setActiveBackup>(app_id, target_id, backup_id);
  emit completedOperations();
}

void ApplicationManager::getBackupTargets(int app_id)
{
  // fork audit: read-only getter, do not surface an error dialog on a stale/invalid app id.
  if(appIndexIsValid(app_id, false))
    emit sendBackupTargets(apps_[app_id].getBackupTargets());
}

void ApplicationManager::setBackupName(int app_id, int target_id, int backup_id, QString name)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setBackupName>(
      app_id, target_id, backup_id, name.toStdString());
}

void ApplicationManager::setBackupTargetName(int app_id, int target_id, QString name)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setBackupTargetName>(
      app_id, target_id, name.toStdString());
}

void ApplicationManager::overwriteBackup(int app_id,
                                         int target_id,
                                         int source_backup,
                                         int dest_backup)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::overwriteBackup>(
      app_id, target_id, source_backup, dest_backup);
  emit completedOperations("Backup overwritten");
}

void ApplicationManager::onScrollLists()
{
  emit scrollLists();
}

void ApplicationManager::uninstallGroupMembers(int app_id, const std::vector<int>& mod_ids)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::uninstallGroupMembers>(app_id, mod_ids);
  emit completedOperations("Group members removed");
}

void ApplicationManager::addManualTag(int app_id, QString tag_name)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::addManualTag>(app_id, tag_name.toStdString());
}

void ApplicationManager::removeManualTag(int app_id, QString tag_name)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::removeManualTag>(app_id, tag_name.toStdString(), true);
}

void ApplicationManager::changeManualTagName(int app_id, QString old_name, QString new_name)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::changeManualTagName>(
      app_id, old_name.toStdString(), new_name.toStdString(), true);
}

void ApplicationManager::addTagsToMods(int app_id,
                                       QStringList tag_names,
                                       const std::vector<int>& mod_ids)
{
  if(!appIndexIsValid(app_id))
    return;

  std::vector<std::string> tag_vector;
  for(const auto& tag_name : tag_names)
    tag_vector.push_back(tag_name.toStdString());
  handleExceptions<&ModdedApplication::addTagsToMods>(app_id, tag_vector, mod_ids);
}

void ApplicationManager::removeTagsFromMods(int app_id,
                                            QStringList tag_names,
                                            const std::vector<int>& mod_ids)
{
  if(!appIndexIsValid(app_id))
    return;

  std::vector<std::string> tag_vector;
  for(const auto& tag_name : tag_names)
    tag_vector.push_back(tag_name.toStdString());
  handleExceptions<&ModdedApplication::removeTagsFromMods>(app_id, tag_vector, mod_ids);
}

void ApplicationManager::setTagsForMods(int app_id,
                                        QStringList tag_names,
                                        const std::vector<int>& mod_ids)
{
  if(appIndexIsValid(app_id))
  {
    std::vector<std::string> string_vec;
    for(const auto& name : tag_names)
      string_vec.push_back(name.toStdString());
    handleExceptions<&ModdedApplication::setTagsForMods>(app_id, string_vec, mod_ids);
  }
}

void ApplicationManager::editManualTags(int app_id, std::vector<EditManualTagAction> actions)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::editManualTags>(app_id, actions);
}

void ApplicationManager::editAutoTags(int app_id, std::vector<EditAutoTagAction> actions)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::editAutoTags>(app_id, actions);
  emit completedOperations("Auto tags updated");
}

void ApplicationManager::reapplyAutoTags(int app_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::reapplyAutoTags>(app_id);
  emit completedOperations("Auto tags updated");
}

void ApplicationManager::updateAutoTags(int app_id, std::vector<int> mod_ids)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::updateAutoTags>(app_id, mod_ids);
  emit completedOperations("Auto tags updated");
}

void ApplicationManager::editModSources(int app_id,
                                        int mod_id,
                                        QString local_source,
                                        QString remote_source)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::setModSources>(
      app_id, mod_id, local_source.toStdString(), remote_source.toStdString());
}

void ApplicationManager::getNexusPage(int app_id, int mod_id)
{
  // fork audit: read-only getter, do not surface an error dialog on a stale/invalid app id.
  if(appIndexIsValid(app_id, false))
  {
    auto page = handleExceptions(&ModdedApplication::getNexusPage, apps_[app_id], mod_id);
    if(page)
      emit sendNexusPage(app_id, mod_id, *page);
  }
  emit completedOperations();
}

void ApplicationManager::downloadMod(ImportModInfo info)
{
  info.last_action_was_successful = false;

  // fork #8: register (or reuse) a persistent queue item for this download and mark it
  // active. active_download_id_ may have been pre-set by retryDownload(); otherwise we
  // create a fresh queued item here.
  int queue_id = -1;
  {
    std::lock_guard<std::mutex> lock(download_queue_mutex_);
    cancel_active_download_ = false;
    if(active_download_id_ != -1)
    {
      queue_id = active_download_id_;
    }
    else
    {
      DownloadQueueItem item;
      item.id = next_download_id_++;
      item.app_id = info.app_id;
      item.remote_source = info.remote_source;
      item.remote_request_url = info.remote_request_url;
      item.remote_mod_id = info.remote_mod_id;
      item.remote_file_id = info.remote_file_id;
      item.remote_download_url = info.remote_download_url;
      item.download_user_agent = info.download_user_agent;
      item.download_referer = info.download_referer;
      item.version_overwrite = info.version_overwrite;
      item.target_group_id = info.target_group_id;
      item.name = info.remote_file_name.empty()
                    ? (info.remote_mod_name.empty() ? info.remote_source : info.remote_mod_name)
                    : info.remote_file_name;
      item.status = DownloadQueueItem::active;
      download_queue_.push_back(item);
      queue_id = item.id;
      active_download_id_ = queue_id;
    }
    for(auto& it : download_queue_)
      if(it.id == queue_id)
        it.status = DownloadQueueItem::active;
    saveDownloadQueueLocked();
    emitDownloadQueueLocked();
  }

  auto fail = [&](DownloadQueueItem::Status status)
  {
    std::lock_guard<std::mutex> lock(download_queue_mutex_);
    for(auto& it : download_queue_)
      if(it.id == queue_id)
        it.status = status;
    active_download_id_ = -1;
    cancel_active_download_ = false;
    saveDownloadQueueLocked();
    emitDownloadQueueLocked();
  };

  if(!appIndexIsValid(info.app_id))
  {
    fail(DownloadQueueItem::failed);
    emit downloadFailed();
    return;
  }

  // fork #233 / #114: a pre-resolved direct download (paste-a-link importer, OMM repository)
  // already carries its final URL, file name and any required headers. Skip the Nexus-specific
  // URL resolution + metadata lookup entirely so non-Nexus sources are not forced through the
  // Nexus API. Ordinary Nexus downloads never pre-set remote_download_url, so this is additive.
  if(info.remote_download_url.empty())
  {
    if(info.remote_request_url.empty())
    {
      auto download_url = handleExceptionsForFunction(
        static_cast<std::string (*)(const std::string&, long)>(nexus::Api::getDownloadUrl),
        info.remote_source,
        info.remote_file_id);
      if(!download_url)
      {
        fail(DownloadQueueItem::failed);
        emit downloadFailed();
        return;
      }
      info.remote_download_url = *download_url;
    }
    else
    {
      auto download_url = handleExceptionsForFunction(
        static_cast<std::string (*)(const std::string&)>(nexus::Api::getDownloadUrl),
        info.remote_request_url);
      if(!download_url)
      {
        fail(DownloadQueueItem::failed);
        emit downloadFailed();
        return;
      }
      info.remote_download_url = *download_url;
    }
    info.remote_download_url = QUrl(info.remote_download_url.c_str()).toEncoded().toStdString();

    auto init_successful = handleExceptionsForFunction(nexus::Api::initModInfo, info);
    if(!init_successful || !(*init_successful))
    {
      fail(DownloadQueueItem::failed);
      emit downloadFailed();
      return;
    }

    // fork #8: now that the remote file name is known, refresh the queue item's name.
    {
      std::lock_guard<std::mutex> lock(download_queue_mutex_);
      for(auto& it : download_queue_)
        if(it.id == queue_id && !info.remote_file_name.empty())
          it.name = info.remote_file_name;
      saveDownloadQueueLocked();
      emitDownloadQueueLocked();
    }
  }

  info.target_path = apps_[info.app_id].getDownloadDir();
  auto download_successful = handleExceptionsForFunction(performDownload, info, this);
  if(!download_successful)
  {
    // fork #8: distinguish a user cancellation from a genuine failure.
    fail(cancel_active_download_ ? DownloadQueueItem::cancelled : DownloadQueueItem::failed);
    emit downloadFailed();
    return;
  }

  {
    std::lock_guard<std::mutex> lock(download_queue_mutex_);
    for(auto& it : download_queue_)
      if(it.id == queue_id)
      {
        it.status = DownloadQueueItem::done;
        it.bytes_done = it.bytes_total;
        it.speed = 0.0;
      }
    active_download_id_ = -1;
    cancel_active_download_ = false;
    saveDownloadQueueLocked();
    emitDownloadQueueLocked();
  }

  // fork #8: a restored/retried queue item may carry a target_group_id whose group has since
  // been deleted. Validate it against the app's current groups and clear it if it no longer
  // exists so installation does not target a stale or out-of-range group index.
  if(info.target_group_id != -1 && appIndexIsValid(info.app_id))
  {
    const int num_groups = apps_[info.app_id].getNumGroups();
    if(info.target_group_id < 0 || info.target_group_id >= num_groups)
      info.target_group_id = -1;
  }

  info.last_action_was_successful = true;
  emit downloadComplete(info);
}

// ---- fork #8: persistent download queue implementation -----------------------

sfs::path ApplicationManager::getDownloadQueuePath() const
{
  // fork #8: always use a single stable, app-independent location for the queue file. Deriving
  // it from apps_.front() made the path change once the first app was added (or its ordering
  // changed), orphaning a previously persisted queue across restarts.
  sfs::path dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString();
  dir /= DOWNLOAD_DIR_NAME;
  return dir / DOWNLOAD_QUEUE_FILE_NAME;
}

void ApplicationManager::saveDownloadQueueLocked()
{
  try
  {
    const sfs::path path = getDownloadQueuePath();
    if(!sfs::exists(path.parent_path()))
      sfs::create_directories(path.parent_path());
    Json::Value json;
    json["next_id"] = next_download_id_;
    int i = 0;
    for(const auto& item : download_queue_)
    {
      Json::Value entry;
      entry["id"] = item.id;
      entry["app_id"] = item.app_id;
      entry["remote_source"] = item.remote_source;
      entry["remote_request_url"] = item.remote_request_url;
      entry["remote_mod_id"] = (Json::Int64)item.remote_mod_id;
      entry["remote_file_id"] = (Json::Int64)item.remote_file_id;
      entry["remote_download_url"] = item.remote_download_url;
      entry["download_user_agent"] = item.download_user_agent;
      entry["download_referer"] = item.download_referer;
      entry["target_path"] = item.target_path;
      entry["name"] = item.name;
      entry["version_overwrite"] = item.version_overwrite;
      entry["target_group_id"] = item.target_group_id;
      entry["status"] = (int)item.status;
      entry["bytes_done"] = (Json::Int64)item.bytes_done;
      entry["bytes_total"] = (Json::Int64)item.bytes_total;
      entry["retry_count"] = item.retry_count;
      json["items"][i++] = entry;
    }
    std::ofstream file(path, std::fstream::binary);
    file << json;
    file.flush();
    if(!file.good())
      sendLogMessage(Log::LOG_WARNING,
                     std::string("Could not write download queue to \"") + path.string() + "\".");
  }
  catch(const std::exception& error)
  {
    sendLogMessage(Log::LOG_WARNING,
                   std::string("Could not save download queue: ") + error.what());
  }
}

void ApplicationManager::loadDownloadQueue()
{
  std::lock_guard<std::mutex> lock(download_queue_mutex_);
  download_queue_.clear();
  try
  {
    const sfs::path path = getDownloadQueuePath();
    if(!pu::exists(path))
    {
      emitDownloadQueueLocked();
      return;
    }
    Json::Value json;
    std::ifstream file(path, std::fstream::binary);
    if(file.is_open())
      file >> json;
    file.close();
    next_download_id_ = json.get("next_id", 0).asInt();
    for(const auto& entry : json["items"])
    {
      DownloadQueueItem item;
      item.id = entry.get("id", -1).asInt();
      item.app_id = entry.get("app_id", -1).asInt();
      item.remote_source = entry.get("remote_source", "").asString();
      item.remote_request_url = entry.get("remote_request_url", "").asString();
      item.remote_mod_id = entry.get("remote_mod_id", -1).asInt64();
      item.remote_file_id = entry.get("remote_file_id", -1).asInt64();
      item.remote_download_url = entry.get("remote_download_url", "").asString();
      item.download_user_agent = entry.get("download_user_agent", "").asString();
      item.download_referer = entry.get("download_referer", "").asString();
      item.target_path = entry.get("target_path", "").asString();
      item.name = entry.get("name", "").asString();
      item.version_overwrite = entry.get("version_overwrite", "").asString();
      item.target_group_id = entry.get("target_group_id", -1).asInt();
      item.status = (DownloadQueueItem::Status)entry.get("status", 0).asInt();
      item.bytes_done = entry.get("bytes_done", 0).asInt64();
      item.bytes_total = entry.get("bytes_total", 0).asInt64();
      item.retry_count = entry.get("retry_count", 0).asInt();
      // fork #8: an item that was active/queued when we last exited never finished.
      // True byte-resume is not available (cpr restarts the transfer), so we mark these
      // as failed; the user can retry them, which re-queues the whole download.
      if(item.status == DownloadQueueItem::active || item.status == DownloadQueueItem::queued)
      {
        item.status = DownloadQueueItem::failed;
        item.bytes_done = 0;
        item.speed = 0.0;
      }
      if(item.id >= next_download_id_)
        next_download_id_ = item.id + 1;
      download_queue_.push_back(item);
    }
  }
  catch(const std::exception& error)
  {
    sendLogMessage(Log::LOG_WARNING,
                   std::string("Could not load download queue: ") + error.what());
  }
  emitDownloadQueueLocked();
}

void ApplicationManager::emitDownloadQueueLocked()
{
  emit downloadQueueChanged(download_queue_);
}

bool ApplicationManager::reportDownloadProgress(long long bytes_done,
                                                long long bytes_total,
                                                double speed)
{
  std::lock_guard<std::mutex> lock(download_queue_mutex_);
  for(auto& it : download_queue_)
  {
    if(it.id == active_download_id_)
    {
      it.bytes_done = bytes_done;
      it.bytes_total = bytes_total;
      it.speed = speed;
      it.status = DownloadQueueItem::active;
    }
  }
  emitDownloadQueueLocked();
  // returning false aborts the cpr transfer
  return !cancel_active_download_;
}

bool ApplicationManager::downloadCancelRequested()
{
  std::lock_guard<std::mutex> lock(download_queue_mutex_);
  return cancel_active_download_;
}

void ApplicationManager::requestDownloadQueue()
{
  std::lock_guard<std::mutex> lock(download_queue_mutex_);
  emitDownloadQueueLocked();
}

void ApplicationManager::cancelDownload(int id)
{
  std::lock_guard<std::mutex> lock(download_queue_mutex_);
  if(id == active_download_id_)
  {
    // the running transfer will abort at the next progress callback
    cancel_active_download_ = true;
    // Emit a queue snapshot now so the UI can reflect the cancellation request
    // immediately rather than waiting for the next progress tick.
    emitDownloadQueueLocked();
    return;
  }
  for(auto& it : download_queue_)
    if(it.id == id &&
       (it.status == DownloadQueueItem::queued || it.status == DownloadQueueItem::failed))
      it.status = DownloadQueueItem::cancelled;
  saveDownloadQueueLocked();
  emitDownloadQueueLocked();
}

void ApplicationManager::removeDownload(int id)
{
  std::lock_guard<std::mutex> lock(download_queue_mutex_);
  if(id == active_download_id_)
    return;
  std::erase_if(download_queue_, [id](const DownloadQueueItem& it) { return it.id == id; });
  saveDownloadQueueLocked();
  emitDownloadQueueLocked();
}

ImportModInfo ApplicationManager::importInfoForItem(const DownloadQueueItem& item) const
{
  ImportModInfo info;
  info.app_id = item.app_id;
  info.action_type = ImportModInfo::download;
  info.remote_source = item.remote_source;
  info.remote_request_url = item.remote_request_url;
  info.remote_mod_id = item.remote_mod_id;
  info.remote_file_id = item.remote_file_id;
  info.remote_download_url = item.remote_download_url;
  info.download_user_agent = item.download_user_agent;
  info.download_referer = item.download_referer;
  info.version_overwrite = item.version_overwrite;
  info.target_group_id = item.target_group_id;
  // fork #233/#114: a persisted direct-download item retries from its stored URL, not Nexus.
  info.remote_type =
    item.remote_download_url.empty() ? ImportModInfo::nexus : ImportModInfo::local;
  return info;
}

void ApplicationManager::retryDownload(int id)
{
  ImportModInfo info;
  {
    std::lock_guard<std::mutex> lock(download_queue_mutex_);
    if(active_download_id_ != -1)
    {
      sendLogMessage(Log::LOG_WARNING,
                     std::string("Cannot retry while another download is active."));
      return;
    }
    auto iter = std::find_if(download_queue_.begin(),
                             download_queue_.end(),
                             [id](const DownloadQueueItem& it) { return it.id == id; });
    if(iter == download_queue_.end())
      return;
    iter->retry_count++;
    iter->status = DownloadQueueItem::active;
    iter->bytes_done = 0;
    iter->speed = 0.0;
    active_download_id_ = id;
    cancel_active_download_ = false;
    info = importInfoForItem(*iter);
    saveDownloadQueueLocked();
    emitDownloadQueueLocked();
  }
  // active_download_id_ is set, so downloadMod will reuse this queue item.
  downloadMod(info);
}

void ApplicationManager::checkForModUpdates(int app_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::checkForModUpdates>(app_id);
  emit completedOperations();
}

void ApplicationManager::checkModsForUpdates(int app_id, const std::vector<int>& mod_ids)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::checkModsForUpdates>(app_id, mod_ids);
  emit completedOperations();
}

void ApplicationManager::suppressUpdateNotification(int app_id, const std::vector<int>& mod_ids)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::suppressUpdateNotification>(app_id, mod_ids);
  emit completedOperations();
}

void ApplicationManager::getExternalChanges(int app_id, int deployer, bool deploy)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
  {
    auto changes_info =
      handleExceptions(&ModdedApplication::getExternalChanges, apps_[app_id], deployer);
    if(!changes_info)
      emit completedOperations("Checking for external changes failed");
    else
      emit sendExternalChangesInfo(app_id, *changes_info, apps_[app_id].getNumDeployers(), deploy);
  }
  else
    emit completedOperations("Checking for external changes failed");
}

void ApplicationManager::keepOrRevertFileModifications(int app_id,
                                                       int deployer,
                                                       const FileChangeChoices& changes_to_keep,
                                                       bool deploy)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
  {
    const bool has_throw = handleExceptions<&ModdedApplication::keepOrRevertFileModifications>(
      app_id, deployer, changes_to_keep);
    if(has_throw)
      emit completedOperations("Applying external changes failed");
    else
      emit externalChangesHandled(app_id, deployer, apps_[app_id].getNumDeployers(), deploy);
  }
  else
    emit completedOperations("Applying external changes failed");
}

void ApplicationManager::exportAppConfiguration(int app_id,
                                                std::vector<int> deployers,
                                                QStringList auto_tags)
{
  std::vector<std::string> tag_vector;
  for(const auto& tag : auto_tags)
    tag_vector.push_back(tag.toStdString());
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::exportConfiguration>(app_id, deployers, tag_vector);
  emit completedOperations("Configuration exported");
}

void ApplicationManager::updateIgnoredFiles(int app_id, int deployer)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
    handleExceptions<&ModdedApplication::updateIgnoredFiles>(app_id, deployer);
  emit completedOperations("Ignore list updated");
}

// fork #81: re-scan reverse deployers so externally produced files become visible.
void ApplicationManager::refreshReverseDeployers(int app_id)
{
  if(appIndexIsValid(app_id))
    handleExceptions<&ModdedApplication::refreshReverseDeployers>(app_id);
  emit completedOperations("Files refreshed");
}

void ApplicationManager::addModToIgnoreList(int app_id, int deployer, int mod_id)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
    handleExceptions<&ModdedApplication::addModToIgnoreList>(app_id, deployer, mod_id);
}

void ApplicationManager::applyModAction(int app_id, int deployer, int action, int mod_id)
{
  if(appIndexIsValid(app_id) && deployerIndexIsValid(app_id, deployer))
    handleExceptions<&ModdedApplication::applyModAction>(app_id, deployer, action, mod_id);
}

// ---- Headless CLI helpers (fork #44) ----------------------------------------

std::vector<std::string> ApplicationManager::getCliDeployerNames(int app_id) const
{
  if(app_id < 0 || app_id >= static_cast<int>(apps_.size()))
    return {};
  return apps_[app_id].getDeployerNames();
}

std::vector<ModInfo> ApplicationManager::getCliModInfo(int app_id) const
{
  if(app_id < 0 || app_id >= static_cast<int>(apps_.size()))
    return {};
  return apps_[app_id].getModInfo();
}

std::vector<std::string> ApplicationManager::getCliProfileNames(int app_id) const
{
  if(app_id < 0 || app_id >= static_cast<int>(apps_.size()))
    return {};
  return apps_[app_id].getProfileNames();
}

AppInfo ApplicationManager::getCliAppInfo(int app_id) const
{
  if(app_id < 0 || app_id >= static_cast<int>(apps_.size()))
    return {};
  return apps_[app_id].getAppInfo();
}

std::vector<std::tuple<int, bool>> ApplicationManager::getCliLoadorder(int app_id,
                                                                        int deployer) const
{
  if(app_id < 0 || app_id >= static_cast<int>(apps_.size()))
    return {};
  if(deployer < 0 || deployer >= apps_[app_id].getNumDeployers())
    return {};
  // getLoadorder now returns a DeployerEntry tree; flatten it to (mod id, enabled) pairs,
  // skipping separators/root (fork #44 CLI; adapted to the tree-based loadorder API).
  std::vector<std::tuple<int, bool>> result;
  auto root = apps_[app_id].getLoadorder(deployer);
  for(const auto& weak : root->getTraversalItems())
  {
    auto entry = weak.lock();
    if(!entry || entry->isSeparator)
      continue;
    // Non-separator entries are DeployerModInfo (DeployerEntry isn't polymorphic, so cast statically).
    auto mod = std::static_pointer_cast<DeployerModInfo>(entry);
    result.emplace_back(mod->id, mod->enabled != 0);
  }
  return result;
}

// ---- End headless CLI helpers ------------------------------------------------
