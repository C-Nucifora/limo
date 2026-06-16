#include "addappdialog.h"
#include "core/autotag.h"
#include "core/consts.h"
#include "core/deployerfactory.h"
#include "core/installer.h"
#include "core/parseerror.h"
#include "core/moddedapplication.h"
#include "importfromsteamdialog.h"
#include "ui_addappdialog.h"
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>

namespace sfs = std::filesystem;
namespace str = std::ranges;


AddAppDialog::AddAppDialog(bool is_flatpak, QWidget* parent) :
  QDialog(parent), ui(new Ui::AddAppDialog), is_flatpak_(is_flatpak)
{
  ui->setupUi(this);
  ui->move_dir_box->setVisible(false);
  ui->import_checkbox->setVisible(false);
  ui->import_tags_checkbox->setVisible(false);
  enableOkButton(false);
  // fork #78: a staging dir that does not exist yet but is safely creatable (single
  // missing parent) counts as valid; it is materialised once on accept, not while typing.
  ui->path_field->setValidationMode(ValidatingLineEdit::VALID_CUSTOM);
  ui->path_field->setCustomValidator([this](QString p)
                                     { return pathExistsOrCreatable(p); });
  dialog_completed_ = false;
  import_from_steam_dialog_ = std::make_unique<ImportFromSteamDialog>();
  connect(import_from_steam_dialog_.get(),
          &ImportFromSteamDialog::applicationImported,
          this,
          &AddAppDialog::onApplicationImported);
  connect(import_from_steam_dialog_.get(),
          &ImportFromSteamDialog::addAllSupportedRequested,
          this,
          &AddAppDialog::onAddAllSupported);
  // Populate GOG template combo at construction so it's ready when setAddMode() is called.
  // (issue #74 / limo-app/limo#51)
  populateGogTemplateCombo();
  // Default to the simple, guided layout; setAddMode()/setEditMode() refine this. (issue #92)
  setAdvancedMode(false);
}

AddAppDialog::~AddAppDialog()
{
  delete ui;
}

void AddAppDialog::on_file_picker_button_clicked()
{
  QString starting_dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  if(pathIsValid())
    starting_dir = ui->path_field->text();
  // Use the directory-only chooser instead of a manually configured QFileDialog.
  // The previous code forced the non-native Qt dialog (via setFilter) which, combined
  // with QDir::Hidden, eagerly enumerated the full contents of the browsed directories
  // on the UI thread and could freeze the dialog. The directory chooser populates lazily.
  const QString path =
    QFileDialog::getExistingDirectory(this,
                                      "Select Staging Directory",
                                      starting_dir,
                                      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  onFileDialogAccepted(path);
}

void AddAppDialog::on_name_field_textChanged(const QString& text)
{
  if(text.isEmpty())
    enableOkButton(false);
  else if(pathIsValid())
    enableOkButton(true);
}

void AddAppDialog::on_path_field_textChanged(const QString& text)
{
  if(!pathIsValid())
    enableOkButton(false);
  else if(!ui->name_field->text().isEmpty()) {
    enableOkButton(true);
    auto src = std::filesystem::path(ui->path_field->text().toStdString());
    std::error_code ec;
    if(std::filesystem::exists(src / ModdedApplication::CONFIG_FILE_NAME, ec)) {
        ui->import_checkbox->setEnabled(false);
        ui->import_checkbox->setChecked(false);
        ui->import_tags_checkbox->setEnabled(false);
        ui->import_tags_checkbox->setChecked(false);
    } else {
        ui->import_checkbox->setEnabled(true);
        ui->import_checkbox->setChecked(true);
        ui->import_tags_checkbox->setEnabled(true);
        ui->import_tags_checkbox->setChecked(true);
    }
  }
}

void AddAppDialog::enableOkButton(bool state)
{
  ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(state);
}

bool AddAppDialog::pathIsValid()
{
  return pathExistsOrCreatable(ui->path_field->text());
}

bool AddAppDialog::pathExistsOrCreatable(const QString& text) const // fork #78
{
  if(text.isEmpty())
    return false;
  std::error_code ec;
  const sfs::path path = text.toStdString();
  if(sfs::exists(path, ec))
    return true;
  // Editing an existing app requires a real, existing staging dir.
  if(edit_mode_)
    return false;
  // Safety: a non-existent staging dir is only acceptable when the immediate parent
  // already exists, or exactly one parent level is missing ("single missing parent").
  // If more than one ancestor is missing the path most likely contains a typo, so it
  // stays invalid rather than being silently materialised as a deep directory tree.
  const sfs::path parent = path.parent_path();
  if(parent.empty())
    return false;
  return sfs::exists(parent, ec) || sfs::exists(parent.parent_path(), ec);
}

bool AddAppDialog::createStagingDirIfNeeded() // fork #78
{
  if(edit_mode_)
    return true;
  const QString text = ui->path_field->text();
  std::error_code ec;
  const sfs::path path = text.toStdString();
  if(text.isEmpty() || sfs::exists(path, ec))
    return true;
  if(!pathExistsOrCreatable(text))
    return false;
  // create_directories fills in the (at most one) missing parent and the staging dir.
  sfs::create_directories(path, ec);
  if(ec)
  {
    Log::error("Failed to create staging directory at: " + path.string() +
               ". Error was: " + ec.message());
    return false;
  }
  Log::info("Created staging directory at: " + path.string());
  return true;
}

bool AddAppDialog::iconIsValid(const QString& path)
{
  QString icon_path = path.isEmpty() ? ui->icon_field->text() : path;
  if(icon_path.isEmpty())
    return false;
  // Cheap pre-checks before the (potentially expensive) full QIcon decode: the path must
  // refer to an existing regular file with a recognised image suffix. This avoids decoding
  // arbitrary / large files on the UI thread just to reject obviously-invalid icons.
  std::error_code ec;
  const sfs::path fs_path(icon_path.toStdString());
  if(!sfs::is_regular_file(fs_path, ec) || ec)
    return false;
  static const std::set<QString> image_suffixes{ "png", "jpg", "jpeg", "bmp",
                                                  "gif", "svg", "svgz", "ico",
                                                  "webp", "tiff", "tif", "xpm" };
  const QString suffix = QFileInfo(icon_path).suffix().toLower();
  if(!image_suffixes.contains(suffix))
    return false;
  return QIcon(icon_path).availableSizes().size() > 0;
}

bool AddAppDialog::targetDirIsSafe(const sfs::path& target_dir) const
{
  if(target_dir.empty() || !target_dir.is_absolute())
    return false;
  // Reject any ".." (or "." trickery) component outright: even a path that ends up under a
  // known root after normalisation should not contain traversal segments coming from config.
  for(const auto& component : target_dir)
  {
    if(component == "..")
      return false;
  }
  // The target must be lexically contained within one of the known Steam install/prefix
  // roots. lexically_normal avoids resolving symlinks (the dirs may not exist yet) while
  // still collapsing any redundant separators.
  const sfs::path normalized = target_dir.lexically_normal();
  const auto is_within = [&normalized](const QString& root_str)
  {
    if(root_str.isEmpty())
      return false;
    const sfs::path root = sfs::path(root_str.toStdString()).lexically_normal();
    if(root.empty())
      return false;
    const sfs::path rel = normalized.lexically_relative(root);
    // lexically_relative yields an empty path when unrelated, and a leading ".." when the
    // target escapes the root.
    return !rel.empty() && *rel.begin() != "..";
  };
  return is_within(steam_install_path_) || is_within(steam_prefix_path_);
}

std::vector<sfs::path> AddAppDialog::gameConfigSearchDirs()
{
  // fork #204: community game definitions can be dropped into a user-writable directory
  // so new games can be supported without rebuilding/patching. The user dir is searched
  // first so its definitions override the bundled ones.
  std::vector<sfs::path> dirs;

  const QString user_loc =
    QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  if(!user_loc.isEmpty())
  {
    sfs::path user_dir = sfs::path(user_loc.toStdString()) / "game_configs";
    // Create lazily; a failure here just means the user dir is treated as empty.
    std::error_code ec;
    sfs::create_directories(user_dir, ec);
    dirs.push_back(user_dir);
  }

  sfs::path bundled_dir =
    sfs::path(is_flatpak_ ? "/app" : APP_INSTALL_PREFIX) / "share/limo/steam_app_configs";
  // Overwrite for local build
  if(!is_flatpak_ && sfs::exists("steam_app_configs"))
    bundled_dir = "steam_app_configs";
  dirs.push_back(bundled_dir);

  return dirs;
}

namespace
{
// Locates "<app_id>.json" without needing an AddAppDialog instance: the user-writable
// config dir first, then the bundled steam_app_configs dir. Uses the global flatpak flag
// (set during MainWindow init) so the bundled path resolves correctly.
std::optional<sfs::path> findGameConfigFile(const std::string& app_id)
{
  if(app_id.empty())
    return std::nullopt;
  const bool is_flatpak = Installer::isAFlatpak();
  std::vector<sfs::path> dirs;
  const QString user_loc = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  if(!user_loc.isEmpty())
    dirs.push_back(sfs::path(user_loc.toStdString()) / "game_configs");
  sfs::path bundled_dir =
    sfs::path(is_flatpak ? "/app" : APP_INSTALL_PREFIX) / "share/limo/steam_app_configs";
  if(!is_flatpak && sfs::exists("steam_app_configs"))
    bundled_dir = "steam_app_configs";
  dirs.push_back(bundled_dir);

  const std::string config_file_name = app_id + ".json";
  std::error_code ec;
  for(const auto& dir : dirs)
  {
    if(sfs::is_regular_file(dir / config_file_name, ec))
      return dir / config_file_name;
  }
  return std::nullopt;
}
}

bool AddAppDialog::hasGameConfig(const std::string& app_id)
{
  return findGameConfigFile(app_id).has_value();
}

int AddAppDialog::presetInstallFlags(const std::string& app_id)
{
  const auto config_file = findGameConfigFile(app_id);
  if(!config_file)
    return 0;
  std::ifstream file(*config_file);
  if(!file.is_open())
    return 0;
  Json::Value root;
  try
  {
    file >> root;
  }
  catch(const std::exception&)
  {
    return 0;
  }
  static const std::map<std::string, Installer::Flag> flag_by_name{
    { "preserve_case", Installer::preserve_case },
    { "lower_case", Installer::lower_case },
    { "upper_case", Installer::upper_case },
    { "preserve_directories", Installer::preserve_directories },
    { "single_directory", Installer::single_directory },
    { "no_extract", Installer::no_extract }
  };
  int flags = 0;
  if(root.isMember("default_install_flags") && root["default_install_flags"].isArray())
  {
    for(const auto& value : root["default_install_flags"])
    {
      if(!value.isString())
        continue;
      const auto it = flag_by_name.find(value.asString());
      if(it != flag_by_name.end())
        flags |= it->second;
    }
  }
  return flags;
}

void AddAppDialog::initConfigForApp()
{
  deployers_.clear();
  auto_tags_.clear();
  // fork #204: resolve "<id>.json" from the user dir first, then the bundled dir.
  // Build the ordered list of existing candidate files so a malformed user file can
  // fall back to the bundled one rather than dropping the game entirely.
  const std::string config_file_name = std::to_string(steam_app_id_) + ".json";
  std::vector<sfs::path> candidates;
  for(const auto& dir : gameConfigSearchDirs())
  {
    if(!sfs::exists(dir))
      continue;
    sfs::path candidate = dir / config_file_name;
    if(sfs::exists(candidate))
      candidates.push_back(candidate);
  }
  if(candidates.empty())
  {
    initDefaultAppConfig();
    return;
  }

  Json::Value json;
  sfs::path config_path;
  bool parsed = false;
  for(const auto& candidate : candidates)
  {
    Json::Value parsed_json;
    std::ifstream file(candidate, std::fstream::binary);
    if(!file.is_open())
    {
      Log::debug("Failed to open app settings file at: " + candidate.string());
      continue;
    }
    try
    {
      file >> parsed_json;
    }
    catch(Json::Exception& e)
    {
      Log::debug("Failed to read from app settings file at: " + candidate.string() +
                 ". Error was: " + e.what());
      continue;
    }
    catch(...)
    {
      Log::debug("Failed to read from app settings file at: " + candidate.string());
      continue;
    }
    json = parsed_json;
    config_path = candidate;
    parsed = true;
    break;
  }
  Log::debug("Config path: " + config_path.string());
  if(!parsed)
  {
    initDefaultAppConfig();
    return;
  }

  Log::debug(std::format("Reading app config for id {}", steam_app_id_));
  try
  {
    std::vector<std::string> skipped_unsafe_targets;
    for(int i = 0; i < json["deployers"].size(); i++)
    {
      Json::Value deployer = json["deployers"][i];
      EditDeployerInfo info;

      for(const auto& key : JSON_DEPLOYER_MANDATORY_KEYS)
      {
        if(deployer[key].isNull())
        {
          Log::debug(std::format(
            "App config for deployer {} for app {} does not contain key {}", i, steam_app_id_, key));
          continue;
        }
      }

      const std::string type = deployer[JSON_DEPLOYERS_TYPE].asString();
      if(str::find(DeployerFactory::DEPLOYER_TYPES, type) == DeployerFactory::DEPLOYER_TYPES.end())
      {
        Log::debug(std::format(
          "App config for deployer {} for app {} contains unknown type {}", i, steam_app_id_, type));
        continue;
      }
      info.type = type;

      info.name = deployer[JSON_DEPLOYERS_NAME].asString();

      QString target_string = deployer[JSON_DEPLOYERS_TARGET].asString().c_str();
      target_string.replace("$STEAM_INSTALL_PATH$", steam_install_path_);
      target_string.replace("$STEAM_PREFIX_PATH$", steam_prefix_path_);
      const std::string target_dir = target_string.toStdString();
      if(!sfs::exists(target_dir))
      {
        // A plugin/Loot deployer's target often lives inside the Proton prefix (e.g. ".../Local
        // Settings/Application Data/<Game>") and is only created once the game has run. Create it
        // rather than silently dropping the deployer, so imports set up every recommended deployer
        // (limo-app/limo#224).
        // Security: the target comes from a (potentially user-supplied/community) game config.
        // Only create it if it resolves to a location inside the known Steam install/prefix
        // roots and contains no ".." traversal, so a malicious config can not create
        // directories at arbitrary filesystem locations.
        if(!targetDirIsSafe(target_dir))
        {
          Log::debug(std::format(
            "App config for deployer {} for app {} has an unsafe target {} outside the install/"
            "prefix roots; skipping deployer",
            i,
            steam_app_id_,
            target_dir));
          skipped_unsafe_targets.push_back(target_dir);
          continue;
        }
        std::error_code ec;
        sfs::create_directories(target_dir, ec);
        if(ec)
        {
          Log::debug(std::format("App config for deployer {} for app {} contains invalid target {}",
                                 i,
                                 steam_app_id_,
                                 target_dir));
          continue;
        }
        Log::debug(std::format(
          "Created missing target directory {} for deployer {} of app {}", target_dir, i, steam_app_id_));
      }
      info.target_dir = target_dir;

      QString deploy_mode = deployer[JSON_DEPLOYERS_MODE].asString().c_str();
      // Accept both the underscore ("hard_link") and space ("hard link") spellings: the bundled
      // steam_app_configs use both forms, and previously only the space form matched here, which
      // silently dropped deployers from underscore-style configs (limo-app/limo#136).
      deploy_mode = deploy_mode.toLower().replace("_", " ");
      if(deploy_mode == "hard link")
        info.deploy_mode = Deployer::hard_link;
      else if(deploy_mode == "sym link" || deploy_mode == "soft link")
        info.deploy_mode = Deployer::sym_link;
      else if(deploy_mode == "copy")
        info.deploy_mode = Deployer::copy;
      else
      {
        Log::debug(std::format("App config for deployer {} for app {} contains invalid mode {}",
                               i,
                               steam_app_id_,
                               deploy_mode.toStdString()));
        continue;
      }

      if(!deployer[JSON_DEPLOYERS_SOURCE].isNull())
      {
        QString home_path = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        QString source_string = deployer[JSON_DEPLOYERS_SOURCE].asString().c_str();
        source_string.replace("$STEAM_INSTALL_PATH$", steam_install_path_);
        source_string.replace("$STEAM_PREFIX_PATH$", steam_prefix_path_);
        source_string.replace("$HOME$", home_path);
        const std::string source_dir = source_string.toStdString();
        if(!sfs::exists(source_dir))
        {
          Log::debug(std::format("App config for deployer {} for app {} contains invalid source {}",
                                 i,
                                 steam_app_id_,
                                 source_dir));
          continue;
        }
        info.source_dir = source_dir;
      }
      if(!deployer[JSON_DEPLOYERS_SEPARATE_DIRS].isNull())
        info.separate_profile_dirs = deployer[JSON_DEPLOYERS_SEPARATE_DIRS].asBool();
      if(!deployer[JSON_DEPLOYERS_UPDATE_IGNORE_LIST].isNull())
        info.update_ignore_list = deployer[JSON_DEPLOYERS_UPDATE_IGNORE_LIST].asBool();
      deployers_.push_back(info);
    }
    Log::debug(std::format("Found {} deployers", deployers_.size()));

    if(!skipped_unsafe_targets.empty())
    {
      QString details;
      for(const auto& target : skipped_unsafe_targets)
        details += "\n- " + QString::fromStdString(target).toHtmlEscaped();
      QMessageBox::warning(
        this,
        "Skipped deployers",
        QString("Some deployers from the game configuration were skipped because their target "
                "directory pointed outside the game's install/prefix directories and was not "
                "created for security reasons:%1")
          .arg(details));
    }

    for(int i = 0; i < json[JSON_AUTO_TAGS_GROUP].size(); i++)
    {
      try
      {
        AutoTag _(json[JSON_AUTO_TAGS_GROUP][i]);
      }
      catch(const ParseError& e)
      {
        Log::debug(std::format(
          "Failed to read auto tag {} for app with id {}.\nError: {}", i, steam_app_id_, e.what()));
        continue;
      }
      catch(...)
      {
        Log::debug(std::format("Failed to read auto tag {} for app with id {}", i, steam_app_id_));
        continue;
      }
      auto_tags_.push_back(json[JSON_AUTO_TAGS_GROUP][i]);
    }
    Log::debug(std::format("Found {} auto tags", auto_tags_.size()));

    if(!json[JSON_NAME].isNull())
      ui->name_field->setText(json[JSON_NAME].asCString());
  }
  catch(...)
  {
    Log::debug("Failed to read from app settings file at: " + config_path.string());
    initDefaultAppConfig();
    return;
  }

  ui->import_checkbox->setToolTip(
    std::format("Import {} recommended deployers", deployers_.size()).c_str());

  ui->import_tags_checkbox->setToolTip(
    std::format("Import {} recommended auto tags", auto_tags_.size()).c_str());

  if(deployers_.empty() && auto_tags_.empty())
    initDefaultAppConfig();
}

void AddAppDialog::initDefaultAppConfig()
{
  deployers_.clear();
  auto_tags_.clear();

  Log::debug(std::format("Using default config for app {}", steam_app_id_));
  deployers_.emplace_back(DeployerFactory::CASEMATCHINGDEPLOYER,
                          "Install",
                          steam_install_path_.toStdString(),
                          Deployer::hard_link);
  deployers_.emplace_back(DeployerFactory::CASEMATCHINGDEPLOYER,
                          "Prefix",
                          steam_prefix_path_.toStdString(),
                          Deployer::hard_link);
  ui->import_checkbox->setToolTip(
    "Import deployers targeting the installation and prefix directories");
}

void AddAppDialog::updateDetectedPath()
{
  ui->detected_path_label->setText(steam_install_path_);
}

void AddAppDialog::setAdvancedMode(bool advanced)
{
  // The simple, guided fields (name, staging directory, game template combo and the
  // "Import from Steam" button) always stay visible. Everything below is advanced
  // detail that new users should not have to deal with. (issue #92)
  ui->label_5->setVisible(advanced);
  ui->version_field->setVisible(advanced);
  ui->label_3->setVisible(advanced);
  ui->icon_field->setVisible(advanced);
  ui->icon_picker_button->setVisible(advanced);
  ui->label_4->setVisible(advanced);
  ui->command_field->setVisible(advanced);
  ui->detected_path_caption->setVisible(advanced);
  ui->detected_path_label->setVisible(advanced);
  ui->hooks_box->setVisible(advanced);
}

void AddAppDialog::on_advanced_checkbox_stateChanged(int state)
{
  setAdvancedMode(state == Qt::Checked);
}

void AddAppDialog::loadHooksFromConfig(const QString& staging_dir)
{
  ui->pre_deploy_field->setText("");
  ui->post_deploy_field->setText("");
  ui->pre_undeploy_field->setText("");
  ui->post_undeploy_field->setText("");

  if(staging_dir.isEmpty())
    return;
  const sfs::path config_path =
    sfs::path(staging_dir.toStdString()) / ModdedApplication::CONFIG_FILE_NAME;
  if(!sfs::exists(config_path))
    return;

  Json::Value json;
  std::ifstream file(config_path, std::fstream::binary);
  if(!file.is_open())
    return;
  try
  {
    file >> json;
  }
  catch(...)
  {
    Log::debug("Failed to read hooks from app config at: " + config_path.string());
    return;
  }

  if(!json.isMember(JSON_HOOKS_GROUP))
    return;
  const Json::Value hooks = json[JSON_HOOKS_GROUP];
  if(hooks.isMember(JSON_HOOK_PRE_DEPLOY))
    ui->pre_deploy_field->setText(hooks[JSON_HOOK_PRE_DEPLOY].asCString());
  if(hooks.isMember(JSON_HOOK_POST_DEPLOY))
    ui->post_deploy_field->setText(hooks[JSON_HOOK_POST_DEPLOY].asCString());
  if(hooks.isMember(JSON_HOOK_PRE_UNDEPLOY))
    ui->pre_undeploy_field->setText(hooks[JSON_HOOK_PRE_UNDEPLOY].asCString());
  if(hooks.isMember(JSON_HOOK_POST_UNDEPLOY))
    ui->post_undeploy_field->setText(hooks[JSON_HOOK_POST_UNDEPLOY].asCString());
}

void AddAppDialog::saveHooksToConfig(const QString& staging_dir)
{
  if(staging_dir.isEmpty())
    return;
  const sfs::path config_path =
    sfs::path(staging_dir.toStdString()) / ModdedApplication::CONFIG_FILE_NAME;
  // Only an existing app config can carry hooks. For freshly created apps the
  // config file does not exist yet at this point; hooks can be set later by
  // editing the application. If the user actually entered hook commands but they
  // cannot be persisted yet, warn them rather than silently discarding the input
  // (limo-app/limo audit F063).
  if(!sfs::exists(config_path))
  {
    const bool has_hooks = !ui->pre_deploy_field->text().isEmpty() ||
                           !ui->post_deploy_field->text().isEmpty() ||
                           !ui->pre_undeploy_field->text().isEmpty() ||
                           !ui->post_undeploy_field->text().isEmpty();
    if(has_hooks)
    {
      Log::debug("App config does not exist yet; deploy hooks could not be saved at: " +
                 config_path.string());
      QMessageBox::warning(
        this,
        "Deploy hooks not saved",
        "The deploy hooks could not be saved yet because the application has not been fully "
        "created. Please re-open this application for editing to set its deploy hooks.");
    }
    return;
  }

  Json::Value json;
  {
    std::ifstream file(config_path, std::fstream::binary);
    if(!file.is_open())
    {
      Log::debug("Failed to open app config to save hooks at: " + config_path.string());
      return;
    }
    try
    {
      file >> json;
    }
    catch(...)
    {
      Log::debug("Failed to parse app config to save hooks at: " + config_path.string());
      return;
    }
  }

  json[JSON_HOOKS_GROUP][JSON_HOOK_PRE_DEPLOY] = ui->pre_deploy_field->text().toStdString();
  json[JSON_HOOKS_GROUP][JSON_HOOK_POST_DEPLOY] = ui->post_deploy_field->text().toStdString();
  json[JSON_HOOKS_GROUP][JSON_HOOK_PRE_UNDEPLOY] = ui->pre_undeploy_field->text().toStdString();
  json[JSON_HOOKS_GROUP][JSON_HOOK_POST_UNDEPLOY] = ui->post_undeploy_field->text().toStdString();

  std::ofstream out(config_path, std::fstream::binary);
  if(!out.is_open())
  {
    Log::debug("Failed to write hooks to app config at: " + config_path.string());
    return;
  }
  out << json;
}

void AddAppDialog::setEditMode(const QString& name,
                               const QString& app_version,
                               const QString& path,
                               const QString& command,
                               const QString& icon_path,
                               int app_id,
                               long steam_app_id)
{
  deployers_.clear();
  auto_tags_.clear();
  steam_prefix_path_ = "";
  steam_install_path_ = "";
  ui->import_checkbox->setVisible(false);
  ui->import_tags_checkbox->setVisible(false);
  ui->import_button->setEnabled(false);
  ui->import_button->setHidden(true);
  ui->gog_group_box->setVisible(false);
  ui->move_dir_box->setCheckState(Qt::Unchecked);
  name_ = name;
  path_ = path;
  command_ = command;
  app_id_ = app_id;
  steam_app_id_ = steam_app_id;
  enableOkButton(true);
  edit_mode_ = true;
  ui->move_dir_box->setVisible(true);
  setWindowTitle("Edit " + name_);
  ui->name_field->setText(name);
  ui->version_field->setText(app_version);
  ui->icon_field->setText(icon_path);
  if(iconIsValid(icon_path))
    ui->icon_picker_button->setIcon(QIcon(icon_path));
  else
    ui->icon_picker_button->setIcon(QIcon::fromTheme("folder-open"));
  ui->path_field->setText(path);
  ui->command_field->setText(command);
  updateDetectedPath();
  loadHooksFromConfig(path);
  // Editing an existing application means dealing with details (command, icon, hooks),
  // so show the advanced fields by default. (issue #92)
  ui->advanced_checkbox->setChecked(true);
  setAdvancedMode(true);
  dialog_completed_ = false;
}

void AddAppDialog::setAddMode()
{
  deployers_.clear();
  auto_tags_.clear();
  steam_prefix_path_ = "";
  steam_install_path_ = "";
  ui->import_checkbox->setVisible(false);
  ui->import_tags_checkbox->setVisible(false);
  ui->import_button->setEnabled(true);
  ui->import_button->setHidden(false);
  // Show the GOG template section only when adding (not editing) an application.
  // (issue #74 / limo-app/limo#51)
  ui->gog_group_box->setVisible(!gog_template_paths_.isEmpty());
  ui->gog_prefix_field->setText("");
  setWindowTitle("New Application");
  ui->name_field->setText("");
  ui->version_field->setText("");
  ui->icon_field->setText("");
  ui->icon_picker_button->setIcon(QIcon::fromTheme("folder-open"));
  ui->path_field->setText("");
  ui->command_field->setText("");
  loadHooksFromConfig("");
  enableOkButton(false);
  edit_mode_ = false;
  ui->move_dir_box->setVisible(false);
  updateDetectedPath();
  // Start new applications in the simple, guided mode. The user fills in a name and a
  // staging directory and then either imports from Steam or applies a bundled game
  // template, both of which auto-fill the command and deployers. Advanced fields stay
  // hidden until the "Advanced setup" box is ticked. (issue #92)
  ui->advanced_checkbox->setChecked(false);
  setAdvancedMode(false);
  dialog_completed_ = false;
}

void AddAppDialog::on_buttonBox_accepted()
{
  if(dialog_completed_)
    return;
  // fork #78: materialise a not-yet-existing (but safely creatable) staging directory now,
  // once, rather than while the user is still typing the path.
  if(!createStagingDirIfNeeded())
  {
    QMessageBox::warning(this,
                         "Could not create staging directory",
                         "The staging directory could not be created. Please choose a "
                         "different location.");
    return;
  }
  dialog_completed_ = true;
  EditApplicationInfo info;
  info.name = ui->name_field->text().toStdString();
  info.app_version = ui->version_field->text().toStdString();
  info.staging_dir = ui->path_field->text().toStdString();
  info.command = ui->command_field->text().toStdString();
  info.icon_path = ui->icon_field->text().toStdString();
  info.steam_app_id = steam_app_id_;
  if(edit_mode_)
  {
    info.move_staging_dir = ui->move_dir_box->checkState() == Qt::Checked;
    // Persist hooks directly into the app config. EditApplicationInfo lives in an
    // out-of-scope header and can not carry the hook strings, so we write them to
    // lmm_mods.json here. ModdedApplication reads them back via its "hooks" key.
    // Note: if the app config is rewritten from the live in-memory object before
    // it reloads these values (e.g. by the edit operation itself), the change
    // takes effect on the next app reload / program start.
    saveHooksToConfig(ui->path_field->text());
    emit applicationEdited(info, app_id_);
  }
  else
  {
    if(ui->import_checkbox->isChecked())
      info.deployers = deployers_;
    if(ui->import_tags_checkbox->isChecked())
      info.auto_tags = auto_tags_;
    emit applicationAdded(info);
  }
}

void AddAppDialog::on_import_button_clicked()
{
  batch_import_done_ = false;
  import_from_steam_dialog_->init();
  import_from_steam_dialog_->exec();
  // If the user chose "Add all supported", the apps were created during the import dialog's
  // signal; close this dialog now that the modal import sub-dialog has returned.
  if(batch_import_done_)
  {
    batch_import_done_ = false;
    accept();
  }
}

void AddAppDialog::openSteamImport()
{
  on_import_button_clicked();
}

void AddAppDialog::addImportedAppDirect(const QString& name,
                                        const QString& app_id,
                                        const QString& install_dir,
                                        const QString& prefix_path,
                                        const QString& icon_path,
                                        const QString& staging_dir)
{
  // Populate the same state the interactive import sets (members + deployers_/auto_tags_
  // via initConfigForApp), then build the app info directly without the visible form.
  onApplicationImported(name, app_id, install_dir, prefix_path, icon_path);
  std::error_code ec;
  sfs::create_directories(staging_dir.toStdString(), ec);
  if(ec)
  {
    Log::error("Batch import: could not create staging dir '" + staging_dir.toStdString() +
               "': " + ec.message() + " — skipping '" + name.toStdString() + "'.");
    return;
  }
  EditApplicationInfo info;
  info.name = name.toStdString();
  info.staging_dir = staging_dir.toStdString();
  info.command = ("xdg-open steam://rungameid/" + app_id).toStdString();
  info.icon_path = icon_path.toStdString();
  info.steam_app_id = steam_app_id_;
  info.deployers = deployers_;
  info.auto_tags = auto_tags_;
  emit applicationAdded(info);
}

void AddAppDialog::onAddAllSupported(const QList<QStringList>& games)
{
  if(games.isEmpty())
    return;
  const QString root = QFileDialog::getExistingDirectory(
    this,
    "Select a parent folder for mod staging",
    QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
    QFileDialog::ShowDirsOnly);
  if(root.isEmpty())
    return;
  int added = 0;
  for(const QStringList& g : games)
  {
    if(g.size() < 5)
      continue;
    QString folder = g[0];
    folder.replace(QRegularExpression("[^A-Za-z0-9._ -]"), "_");
    if(folder.trimmed().isEmpty())
      folder = g[1];
    const sfs::path staging = sfs::path(root.toStdString()) / folder.toStdString();
    addImportedAppDirect(g[0], g[1], g[2], g[3], g[4], QString::fromStdString(staging.string()));
    added++;
  }
  Log::info("Batch import: added " + std::to_string(added) + " supported game(s).");
  batch_import_done_ = true;
}

void AddAppDialog::onApplicationImported(QString name,
                                         QString app_id,
                                         QString install_dir,
                                         QString prefix_path,
                                         QString icon_path)
{
  ui->name_field->setText(name);
  ui->command_field->setText("xdg-open steam://rungameid/" + app_id);
  bool app_id_ok = false;
  const long parsed_app_id = app_id.toLong(&app_id_ok);
  if(app_id_ok && parsed_app_id > 0)
    steam_app_id_ = parsed_app_id;
  else
  {
    steam_app_id_ = -1;
    Log::debug("Imported Steam app id '" + app_id.toStdString() +
               "' is not a valid positive integer; using default config.");
  }
  steam_install_path_ = install_dir;
  steam_prefix_path_ = prefix_path;
  updateDetectedPath();
  ui->icon_field->setText(icon_path);
  ui->icon_picker_button->setIcon(QIcon(icon_path));
  initConfigForApp();
  ui->import_checkbox->setVisible(!deployers_.empty());
  ui->import_tags_checkbox->setVisible(!auto_tags_.empty());
}

void AddAppDialog::onFileDialogAccepted(const QString& path)
{
  if(!path.isEmpty())
    ui->path_field->setText(path);
}

void AddAppDialog::on_icon_picker_button_clicked()
{
  QString starting_dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  QString path = ui->icon_field->text();
  if(!path.isEmpty() && std::filesystem::exists(path.toStdString()))
    starting_dir = std::filesystem::path(path.toStdString()).parent_path().string().c_str();
  auto dialog = new QFileDialog(this);
  dialog->setWindowTitle("Select Icon");
  dialog->setFilter(QDir::AllDirs | QDir::Hidden);
  dialog->setDirectory(starting_dir);
  connect(dialog, &QFileDialog::fileSelected, this, &AddAppDialog::onIconPathDialogComplete);
  dialog->exec();
  dialog->deleteLater();
}

void AddAppDialog::onIconPathDialogComplete(const QString& path)
{
  if(!iconIsValid(path))
  {
    QMessageBox* error_box =
      new QMessageBox(QMessageBox::Critical, "Error", "Invalid icon!", QMessageBox::Ok);
    error_box->exec();
    return;
  }
  ui->icon_field->setText(path);
  ui->icon_picker_button->setIcon(QIcon(path));
}

// ---------------------------------------------------------------------------
// GOG / non-Steam game template support (issue #74 / limo-app/limo#51)
// ---------------------------------------------------------------------------

void AddAppDialog::populateGogTemplateCombo()
{
  ui->gog_template_combo->clear();
  gog_template_paths_.clear();

  // fork #204: scan both the user game-config dir and the bundled dir (user first, so a
  // user definition wins on filename/id collision) so user-added games appear as templates.
  std::vector<sfs::path> config_dirs = gameConfigSearchDirs();
  bool any_dir_found = false;

  // Collect (display_name, file_path) pairs then sort by name for a tidy combo.
  std::vector<std::pair<QString, QString>> entries;
  std::set<std::string> seen_ids; // fork #204: dedupe by file stem; user dir is seen first
  for(const auto& config_dir : config_dirs)
  {
    if(!sfs::exists(config_dir))
      continue;
    any_dir_found = true;
    for(const auto& entry : sfs::directory_iterator(config_dir))
    {
      if(entry.path().extension() != ".json")
        continue;
      // fork #204: skip an id already provided by an earlier (higher-priority) dir.
      if(!seen_ids.insert(entry.path().stem().string()).second)
        continue;
      Json::Value json;
      std::ifstream f(entry.path(), std::fstream::binary);
      if(!f.is_open())
        continue;
      try
      {
        f >> json;
      }
      catch(...)
      {
        continue;
      }
      QString display_name;
      if(!json[JSON_NAME].isNull())
        display_name = json[JSON_NAME].asCString();
      else
        display_name = entry.path().stem().string().c_str();
      entries.emplace_back(display_name, entry.path().string().c_str());
    }
  }
  if(!any_dir_found)
  {
    Log::debug("GOG template: could not find steam_app_configs directory");
    return;
  }
  std::sort(entries.begin(), entries.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for(const auto& [name, path] : entries)
  {
    ui->gog_template_combo->addItem(name);
    gog_template_paths_.append(path);
  }
  Log::debug(std::format("GOG template combo populated with {} entries", entries.size()));
}

void AddAppDialog::initConfigForGog(const QString& install_path,
                                    const QString& prefix_path,
                                    const QString& config_path)
{
  deployers_.clear();
  auto_tags_.clear();

  Json::Value json;
  std::ifstream file(config_path.toStdString(), std::fstream::binary);
  if(!file.is_open())
  {
    Log::debug("GOG template: failed to open config file: " + config_path.toStdString());
    return;
  }
  try
  {
    file >> json;
  }
  catch(Json::Exception& e)
  {
    Log::debug("GOG template: JSON parse error in " + config_path.toStdString() +
               ": " + e.what());
    return;
  }
  catch(...)
  {
    Log::debug("GOG template: unknown error reading " + config_path.toStdString());
    return;
  }

  const bool has_prefix = !prefix_path.isEmpty();
  int skipped_deployers = 0;

  for(int i = 0; i < json[JSON_DEPLOYERS_GROUP].size(); i++)
  {
    Json::Value deployer = json[JSON_DEPLOYERS_GROUP][i];
    EditDeployerInfo info;

    // Validate mandatory keys
    bool keys_ok = true;
    for(const auto& key : JSON_DEPLOYER_MANDATORY_KEYS)
    {
      if(deployer[key].isNull())
      {
        Log::debug(std::format(
          "GOG template: deployer {} in {} is missing key {}", i, config_path.toStdString(), key));
        keys_ok = false;
        break;
      }
    }
    if(!keys_ok)
    {
      skipped_deployers++;
      continue;
    }

    const std::string type = deployer[JSON_DEPLOYERS_TYPE].asString();
    if(str::find(DeployerFactory::DEPLOYER_TYPES, type) == DeployerFactory::DEPLOYER_TYPES.end())
    {
      Log::debug(std::format(
        "GOG template: deployer {} in {} has unknown type {}", i, config_path.toStdString(), type));
      skipped_deployers++;
      continue;
    }
    info.type = type;
    info.name = deployer[JSON_DEPLOYERS_NAME].asString();

    // Resolve target directory placeholders.
    // If the target contains $STEAM_PREFIX_PATH$ but no prefix was supplied, skip this deployer.
    QString target_string = deployer[JSON_DEPLOYERS_TARGET].asString().c_str();
    if(target_string.contains("$STEAM_PREFIX_PATH$"))
    {
      if(!has_prefix)
      {
        Log::debug(std::format(
          "GOG template: skipping deployer {} ('{}') — uses $STEAM_PREFIX_PATH$ but no prefix "
          "path was provided",
          i,
          info.name));
        skipped_deployers++;
        continue;
      }
      target_string.replace("$STEAM_PREFIX_PATH$", prefix_path);
    }
    target_string.replace("$STEAM_INSTALL_PATH$", install_path);
    const std::string target_dir = target_string.toStdString();
    if(!sfs::exists(target_dir))
    {
      Log::debug(std::format(
        "GOG template: deployer {} target '{}' does not exist — skipping", i, target_dir));
      skipped_deployers++;
      continue;
    }
    info.target_dir = target_dir;

    // Deploy mode
    QString deploy_mode = deployer[JSON_DEPLOYERS_MODE].asString().c_str();
    deploy_mode = deploy_mode.toLower().replace("_", " ");
    if(deploy_mode == "hard link")
      info.deploy_mode = Deployer::hard_link;
    else if(deploy_mode == "sym link" || deploy_mode == "soft link")
      info.deploy_mode = Deployer::sym_link;
    else if(deploy_mode == "copy")
      info.deploy_mode = Deployer::copy;
    else
    {
      Log::debug(std::format(
        "GOG template: deployer {} has invalid mode '{}' — skipping",
        i,
        deploy_mode.toStdString()));
      skipped_deployers++;
      continue;
    }

    // Optional source directory
    if(!deployer[JSON_DEPLOYERS_SOURCE].isNull())
    {
      QString source_string = deployer[JSON_DEPLOYERS_SOURCE].asString().c_str();
      if(source_string.contains("$STEAM_PREFIX_PATH$"))
      {
        if(!has_prefix)
        {
          Log::debug(std::format(
            "GOG template: skipping deployer {} ('{}') — source uses $STEAM_PREFIX_PATH$ but no "
            "prefix path was provided",
            i,
            info.name));
          skipped_deployers++;
          continue;
        }
        source_string.replace("$STEAM_PREFIX_PATH$", prefix_path);
      }
      source_string.replace("$STEAM_INSTALL_PATH$", install_path);
      QString home_path = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
      source_string.replace("$HOME$", home_path);
      const std::string source_dir = source_string.toStdString();
      if(!sfs::exists(source_dir))
      {
        Log::debug(std::format(
          "GOG template: deployer {} source '{}' does not exist — skipping", i, source_dir));
        skipped_deployers++;
        continue;
      }
      info.source_dir = source_dir;
    }

    if(!deployer[JSON_DEPLOYERS_SEPARATE_DIRS].isNull())
      info.separate_profile_dirs = deployer[JSON_DEPLOYERS_SEPARATE_DIRS].asBool();
    if(!deployer[JSON_DEPLOYERS_UPDATE_IGNORE_LIST].isNull())
      info.update_ignore_list = deployer[JSON_DEPLOYERS_UPDATE_IGNORE_LIST].asBool();

    deployers_.push_back(info);
  }

  // Auto tags do not reference path placeholders so all of them can be imported.
  for(int i = 0; i < json[JSON_AUTO_TAGS_GROUP].size(); i++)
  {
    try
    {
      AutoTag _(json[JSON_AUTO_TAGS_GROUP][i]);
    }
    catch(const ParseError& e)
    {
      Log::debug(std::format(
        "GOG template: failed to read auto tag {} from {}. Error: {}",
        i,
        config_path.toStdString(),
        e.what()));
      continue;
    }
    catch(...)
    {
      Log::debug(std::format(
        "GOG template: failed to read auto tag {} from {}", i, config_path.toStdString()));
      continue;
    }
    auto_tags_.push_back(json[JSON_AUTO_TAGS_GROUP][i]);
  }

  if(!json[JSON_NAME].isNull() && ui->name_field->text().isEmpty())
    ui->name_field->setText(json[JSON_NAME].asCString());

  Log::debug(std::format(
    "GOG template: loaded {} deployers ({} skipped) and {} auto tags from {}",
    deployers_.size(),
    skipped_deployers,
    auto_tags_.size(),
    config_path.toStdString()));

  ui->import_checkbox->setToolTip(
    std::format("Import {} recommended deployers ({} skipped — path not found or missing prefix)",
                deployers_.size(),
                skipped_deployers)
      .c_str());
  ui->import_tags_checkbox->setToolTip(
    std::format("Import {} recommended auto tags", auto_tags_.size()).c_str());
}

void AddAppDialog::on_gog_apply_button_clicked()
{
  if(!pathIsValid())
  {
    QMessageBox* error_box = new QMessageBox(QMessageBox::Warning,
                                             "No staging directory",
                                             "Please enter a valid staging directory first.",
                                             QMessageBox::Ok);
    error_box->exec();
    return;
  }

  const int idx = ui->gog_template_combo->currentIndex();
  if(idx < 0 || idx >= gog_template_paths_.size())
    return;

  const QString install_path = ui->path_field->text();
  const QString prefix_path = ui->gog_prefix_field->text().trimmed();
  const QString config_path = gog_template_paths_.at(idx);

  // steam_app_id_ remains -1 for GOG installs; that is intentional.
  initConfigForGog(install_path, prefix_path, config_path);

  ui->import_checkbox->setVisible(!deployers_.empty());
  ui->import_tags_checkbox->setVisible(!auto_tags_.empty());
}

void AddAppDialog::on_gog_prefix_picker_button_clicked()
{
  QString starting_dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  const QString current = ui->gog_prefix_field->text().trimmed();
  if(!current.isEmpty() && sfs::exists(current.toStdString()))
    starting_dir = current;
  auto dialog = new QFileDialog(this);
  dialog->setWindowTitle("Select Prefix Directory");
  dialog->setFilter(QDir::AllDirs | QDir::Hidden);
  dialog->setFileMode(QFileDialog::Directory);
  dialog->setDirectory(starting_dir);
  connect(dialog, &QFileDialog::fileSelected, this, &AddAppDialog::onGogPrefixDialogAccepted);
  dialog->exec();
  dialog->deleteLater();
}

void AddAppDialog::onGogPrefixDialogAccepted(const QString& path)
{
  if(!path.isEmpty())
    ui->gog_prefix_field->setText(path);
}
