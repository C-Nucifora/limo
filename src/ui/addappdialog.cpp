#include "addappdialog.h"
#include "core/autotag.h"
#include "core/consts.h"
#include "core/deployerfactory.h"
#include "core/parseerror.h"
#include "core/moddedapplication.h"
#include "importfromsteamdialog.h"
#include "ui_addappdialog.h"
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <filesystem>
#include <fstream>

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
  ui->path_field->setValidationMode(ValidatingLineEdit::VALID_PATH_EXISTS);
  dialog_completed_ = false;
  import_from_steam_dialog_ = std::make_unique<ImportFromSteamDialog>();
  connect(import_from_steam_dialog_.get(),
          &ImportFromSteamDialog::applicationImported,
          this,
          &AddAppDialog::onApplicationImported);
  // Populate GOG template combo at construction so it's ready when setAddMode() is called.
  // (issue #74 / limo-app/limo#51)
  populateGogTemplateCombo();
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
  QString path = ui->path_field->text();
  if(path.isEmpty())
    return false;
  std::error_code ec;
  return std::filesystem::exists(path.toStdString(), ec);
}

bool AddAppDialog::iconIsValid(const QString& path)
{
  QString icon_path = path.isEmpty() ? ui->icon_field->text() : path;
  return QIcon(icon_path).availableSizes().size() > 0;
}

void AddAppDialog::initConfigForApp()
{
  deployers_.clear();
  auto_tags_.clear();
  sfs::path config_path =
    sfs::path(is_flatpak_ ? "/app" : APP_INSTALL_PREFIX) / "share/limo/steam_app_configs";
  // Overwrite for local build
  if(!is_flatpak_ && sfs::exists("steam_app_configs"))
    config_path = "steam_app_configs";
  Log::debug("Config path: " + config_path.string());
  if(!sfs::exists(config_path))
  {
    Log::error("Could not find \"steam_app_configs\" directory. "
               "Make sure Limo is installed correctly");
    initDefaultAppConfig();
    return;
  }

  config_path /= (std::to_string(steam_app_id_) + ".json");
  if(!sfs::exists(config_path))
  {
    initDefaultAppConfig();
    return;
  }

  Json::Value json;
  std::ifstream file(config_path, std::fstream::binary);
  if(!file.is_open())
  {
    Log::debug("Failed to open app settings file at: " + config_path.string());
    initDefaultAppConfig();
    return;
  }
  try
  {
    file >> json;
  }
  catch(Json::Exception& e)
  {
    Log::debug("Failed to read from app settings file at: " + config_path.string() +
               ". Error was: " + e.what());
    initDefaultAppConfig();
    return;
  }
  catch(...)
  {
    Log::debug("Failed to read from app settings file at: " + config_path.string());
    initDefaultAppConfig();
    return;
  }

  Log::debug(std::format("Reading app config for id {}", steam_app_id_));
  try
  {
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
        info.separate_profile_dirs = deployer[JSON_DEPLOYERS_UPDATE_IGNORE_LIST].asBool();
      deployers_.push_back(info);
    }
    Log::debug(std::format("Found {} deployers", deployers_.size()));

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
  enableOkButton(false);
  edit_mode_ = false;
  ui->move_dir_box->setVisible(false);
  updateDetectedPath();
  dialog_completed_ = false;
}

void AddAppDialog::on_buttonBox_accepted()
{
  if(dialog_completed_)
    return;
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
  import_from_steam_dialog_->init();
  import_from_steam_dialog_->exec();
}

void AddAppDialog::onApplicationImported(QString name,
                                         QString app_id,
                                         QString install_dir,
                                         QString prefix_path,
                                         QString icon_path)
{
  ui->name_field->setText(name);
  ui->command_field->setText("xdg-open steam://rungameid/" + app_id);
  steam_app_id_ = app_id.toLong();
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
  auto dialog = new QFileDialog;
  dialog->setWindowTitle("Select Icon");
  dialog->setFilter(QDir::AllDirs | QDir::Hidden);
  dialog->setDirectory(starting_dir);
  connect(dialog, &QFileDialog::fileSelected, this, &AddAppDialog::onIconPathDialogComplete);
  dialog->exec();
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

  sfs::path config_dir =
    sfs::path(is_flatpak_ ? "/app" : APP_INSTALL_PREFIX) / "share/limo/steam_app_configs";
  if(!is_flatpak_ && sfs::exists("steam_app_configs"))
    config_dir = "steam_app_configs";

  if(!sfs::exists(config_dir))
  {
    Log::debug("GOG template: could not find steam_app_configs directory");
    return;
  }

  // Collect (display_name, file_path) pairs then sort by name for a tidy combo.
  std::vector<std::pair<QString, QString>> entries;
  for(const auto& entry : sfs::directory_iterator(config_dir))
  {
    if(entry.path().extension() != ".json")
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
      info.separate_profile_dirs = deployer[JSON_DEPLOYERS_UPDATE_IGNORE_LIST].asBool();

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
  auto dialog = new QFileDialog;
  dialog->setWindowTitle("Select Prefix Directory");
  dialog->setFilter(QDir::AllDirs | QDir::Hidden);
  dialog->setFileMode(QFileDialog::Directory);
  dialog->setDirectory(starting_dir);
  connect(dialog, &QFileDialog::fileSelected, this, &AddAppDialog::onGogPrefixDialogAccepted);
  dialog->exec();
}

void AddAppDialog::onGogPrefixDialogAccepted(const QString& path)
{
  if(!path.isEmpty())
    ui->gog_prefix_field->setText(path);
}
