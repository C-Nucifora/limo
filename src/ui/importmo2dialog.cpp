/*!
 * \file importmo2dialog.cpp
 * \brief Implementation of the ImportMo2Dialog class.
 *
 * Implements fork issue #45 / limo-app/limo#92.
 */

#include "importmo2dialog.h"
#include "../core/deployerfactory.h"
#include "../core/log.h"
#include "ui_importmo2dialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>

namespace sfs = std::filesystem;


ImportMo2Dialog::ImportMo2Dialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::ImportMo2Dialog)
{
  ui->setupUi(this);
  setWindowTitle("Import Mod Organizer 2 Setup");

  // Restore last-used path from QSettings
  const QString saved_path =
    QSettings(QCoreApplication::applicationName()).value("mo2import/path", "").toString();
  if(!saved_path.isEmpty())
  {
    ui->path_field->setText(saved_path);
    updateProfileList(saved_path.toStdString());
  }
}

ImportMo2Dialog::~ImportMo2Dialog()
{
  delete ui;
}

void ImportMo2Dialog::init()
{
  ui->warning_label->clear();
  ui->app_name_field->clear();
  ui->target_field->clear();

  const QString current_path = ui->path_field->text();
  if(!current_path.isEmpty())
    updateProfileList(current_path.toStdString());
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ImportMo2Dialog::updateProfileList(const sfs::path& instance_path)
{
  ui->profile_box->clear();
  ui->warning_label->clear();

  if(!Mo2Importer::isValidInstance(instance_path))
  {
    if(!instance_path.empty())
      ui->warning_label->setText(
        "The selected path does not look like a valid Mod Organizer 2 instance "
        "(missing mods/ directory or no profile with modlist.txt).");
    return;
  }

  const auto profiles = Mo2Importer::listProfiles(instance_path);
  for(const auto& name : profiles)
    ui->profile_box->addItem(QString::fromStdString(name));

  // Pre-fill the app name with the instance directory name
  if(ui->app_name_field->text().isEmpty())
    ui->app_name_field->setText(
      QString::fromStdString(sfs::path(instance_path).filename().string()));
}

void ImportMo2Dialog::showError(const QString& title, const QString& message) const
{
  Log::error(message.toStdString());
  QMessageBox* box =
    new QMessageBox(QMessageBox::Critical, title, message, QMessageBox::Ok,
                    const_cast<ImportMo2Dialog*>(this));
  box->exec();
  delete box;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ImportMo2Dialog::on_pick_path_button_clicked()
{
  const QString starting_dir =
    ui->path_field->text().isEmpty()
      ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
      : ui->path_field->text();

  const QString path =
    QFileDialog::getExistingDirectory(this, "Select MO2 Instance Directory", starting_dir,
                                      QFileDialog::ShowDirsOnly);
  if(path.isEmpty())
    return;

  ui->path_field->setText(path);
  QSettings(QCoreApplication::applicationName()).setValue("mo2import/path", path);
  updateProfileList(path.toStdString());
}

void ImportMo2Dialog::on_pick_target_button_clicked()
{
  const QString starting_dir =
    ui->target_field->text().isEmpty()
      ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
      : ui->target_field->text();

  const QString path =
    QFileDialog::getExistingDirectory(this, "Select Deploy Target Directory", starting_dir,
                                      QFileDialog::ShowDirsOnly);
  if(!path.isEmpty())
    ui->target_field->setText(path);
}

void ImportMo2Dialog::on_path_field_editingFinished()
{
  updateProfileList(ui->path_field->text().toStdString());
}

void ImportMo2Dialog::on_buttonBox_accepted()
{
  const sfs::path instance_path = ui->path_field->text().toStdString();
  const std::string profile_name = ui->profile_box->currentText().toStdString();
  const std::string app_name = ui->app_name_field->text().trimmed().toStdString();
  const std::string target_dir = ui->target_field->text().toStdString();

  // --- Validate ---
  if(!Mo2Importer::isValidInstance(instance_path))
  {
    showError("Invalid Path",
              "The selected path is not a valid Mod Organizer 2 instance.");
    return;
  }
  if(profile_name.empty())
  {
    showError("No Profile", "Please select a Mod Organizer 2 profile.");
    return;
  }
  if(app_name.empty())
  {
    showError("Missing Name", "Please enter a name for the new Limo application.");
    return;
  }
  if(target_dir.empty() || !sfs::is_directory(target_dir))
  {
    showError("Invalid Target",
              "Please select an existing directory as the deploy target.");
    return;
  }

  // --- Parse MO2 profile ---
  Mo2ParseResult parse_result;
  try
  {
    parse_result = Mo2Importer::parseProfile(instance_path, profile_name);
  }
  catch(const std::exception& ex)
  {
    showError("Import Error", QString("Failed to parse MO2 profile:\n") + ex.what());
    return;
  }

  if(!parse_result.warnings.empty())
  {
    QString warn_text = "Import completed with warnings:";
    for(const auto& w : parse_result.warnings)
      warn_text += "\n• " + QString::fromStdString(w);
    ui->warning_label->setText(warn_text);
    Log::warning("MO2 import warnings: " + std::to_string(parse_result.warnings.size()));
    for(const auto& w : parse_result.warnings)
      Log::warning("  " + w);
  }

  // --- Build EditApplicationInfo ---
  // The staging directory is MO2's mods/ folder.  Each numbered Limo mod
  // directory will be a symlink or copy of the respective MO2 mod folder
  // (the actual copying is performed by installMod in the caller).
  // We point staging_dir at the parent of mods/ (the instance root) so that
  // Limo's own lmm_mods.json sits next to the MO2 data rather than inside
  // the mods/ folder, which would confuse MO2 if the instance is reused.
  const sfs::path staging_dir = instance_path / "limo_staging";

  EditDeployerInfo deployer_info;
  deployer_info.type = DeployerFactory::SIMPLEDEPLOYER;
  deployer_info.name = "MO2 Import";
  deployer_info.target_dir = target_dir;
  deployer_info.deploy_mode = Deployer::hard_link;
  deployer_info.enable_unsafe_sorting = true;

  EditApplicationInfo app_info;
  app_info.name = app_name;
  app_info.staging_dir = staging_dir.string();
  app_info.command = "";
  app_info.icon_path = "";
  app_info.app_version = "";
  app_info.steam_app_id = -1;
  app_info.deployers = { deployer_info };

  emit importAccepted(app_info, parse_result);
  accept();
}
