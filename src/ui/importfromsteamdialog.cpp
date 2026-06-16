#include "importfromsteamdialog.h"
#include "../core/log.h"
#include "addappdialog.h"
#include "ui_importfromsteamdialog.h"
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace sfs = std::filesystem;


ImportFromSteamDialog::ImportFromSteamDialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::ImportFromSteamDialog)
{
  ui->setupUi(this);
  setWindowTitle("Import App");
  // Batch action: add every game Limo has a preset for in one go.
  QPushButton* add_all_button =
    ui->buttonBox->addButton("Add all supported", QDialogButtonBox::ActionRole);
  add_all_button->setToolTip("Add every detected game that Limo has a preset for");
  connect(add_all_button,
          &QPushButton::clicked,
          this,
          &ImportFromSteamDialog::onAddAllSupportedClicked);
  QString path = QSettings(QCoreApplication::applicationName()).value("import/path", "").toString();
  if(!path.isEmpty() && pathIsValid(path.toStdString()))
    ui->path_field->setText(path);
  else
  {
    const sfs::path flatpak_path(".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps");
    const sfs::path native_path(".steam/steam/steamapps");
    sfs::path default_path =
      QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdString();
    if(pathIsValid(default_path / flatpak_path))
      default_path /= flatpak_path;
    else
      default_path /= native_path;
    if(pathIsValid(default_path))
    {
      ui->path_field->setText(default_path.string().c_str());
    }
  }
}

ImportFromSteamDialog::~ImportFromSteamDialog()
{
  delete ui;
}

void ImportFromSteamDialog::init()
{
  dialog_completed_ = false;
  ui->search_field->clear();
  for(int i = 0; i < ui->app_table->rowCount(); i++)
    ui->app_table->setRowHidden(i, false);
  ui->search_field->setFocus();
  updateTable(ui->path_field->text().toStdString());
}

void ImportFromSteamDialog::on_pick_path_button_clicked()
{
  QString starting_dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  if(sfs::exists(ui->path_field->text().toStdString()))
    starting_dir = ui->path_field->text();
  QString path = QFileDialog::getExistingDirectory(
    this, "Select steamapps Directory", starting_dir, QFileDialog::ShowDirsOnly);
  updateTable(path.toStdString());
}

bool ImportFromSteamDialog::pathIsValid(sfs::path path) const
{
  return sfs::is_regular_file(path / "libraryfolders.vdf");
}

void ImportFromSteamDialog::updateTable(sfs::path steam_dir)
{
  ui->app_table->setSortingEnabled(false);
  ui->app_table->setRowCount(0);
  if(!pathIsValid(steam_dir))
  {
    showError(
      "Invalid Path",
      std::format("Could not find \"libraryfolders.vdf\" in \"{}\"!", steam_dir.string()).c_str());
    return;
  }
  ui->path_field->setText(steam_dir.c_str());
  QSettings settings(QCoreApplication::applicationName());
  settings.setValue("import/path", steam_dir.c_str());

  std::ifstream file(steam_dir / library_file_name_);
  if(!file.is_open())
  {
    showError(
      "IO Error",
      std::format("Could not open \"{}\"!", (steam_dir / library_file_name_).string()).c_str());
    return;
  }
  // Collect every library folder recorded in libraryfolders.vdf. Both the old
  // (single library) and the new (numbered library objects, each with its own
  // "path" key) schema simply expose a quoted "path" entry per library, so we
  // gather all of them and scan each library's steamapps directory below. This
  // ensures games installed in additional / custom Steam library folders (e.g.
  // on other drives) are detected, not only those in the primary library.
  std::vector<sfs::path> library_paths;
  std::set<std::string> seen_paths;
  std::string line;
  std::regex path_regex("\\s*\"path\"\\s*\"([^\"]+)\"");
  while(std::getline(file, line))
  {
    std::smatch match;
    if(std::regex_search(line, match, path_regex))
    {
      std::string lib_path = match[1].str();
      if(seen_paths.insert(lib_path).second)
        library_paths.emplace_back(lib_path);
    }
  }
  // Fall back to the selected library if the file did not list any path entries.
  if(library_paths.empty())
    library_paths.emplace_back(sfs::path(steam_dir).parent_path());

  std::regex app_manifest_regex(R"(appmanifest_(\d+)\.acf)");
  for(const auto& library_path : library_paths)
  {
    const sfs::path steamapps_path = library_path / "steamapps";
    std::error_code ec;
    if(!sfs::is_directory(steamapps_path, ec))
      continue;
    for(const auto& dir_entry : sfs::directory_iterator(steamapps_path, ec))
    {
      std::smatch match;
      const std::string entry_name = dir_entry.path().filename().string();
      if(std::regex_match(entry_name, match, app_manifest_regex))
        addTableRow(match[1].str(), library_path, library_paths);
    }
  }
  ui->app_table->resizeColumnToContents(0);
  ui->app_table->resizeColumnToContents(1);
  ui->app_table->resizeColumnToContents(2);
  ui->app_table->resizeColumnToContents(4);
  ui->app_table->sortByColumn(0, Qt::AscendingOrder);
  ui->app_table->setSortingEnabled(true);
  applyFilters();
  ui->search_field->setFocus();
}

void ImportFromSteamDialog::applyFilters()
{
  const QString search = ui->search_field->text();
  const bool supported_only = ui->supported_only_checkbox->isChecked();
  for(int i = 0; i < ui->app_table->rowCount(); i++)
  {
    const QTableWidgetItem* name_item = ui->app_table->item(i, 0);
    const QTableWidgetItem* preset_item = ui->app_table->item(i, 4);
    bool hide = false;
    if(!search.isEmpty() && name_item && !name_item->text().contains(search, Qt::CaseInsensitive))
      hide = true;
    if(supported_only && !(preset_item && preset_item->data(Qt::UserRole).toBool()))
      hide = true;
    ui->app_table->setRowHidden(i, hide);
  }
}

QString ImportFromSteamDialog::autoPrefixForRow(int row) const
{
  if(row < 0 || row >= ui->app_table->rowCount())
    return "";
  const QTableWidgetItem* app_id_item = ui->app_table->item(row, 1);
  const QTableWidgetItem* prefix_item = ui->app_table->item(row, 2);
  const QTableWidgetItem* path_item = ui->app_table->item(row, 3);
  if(!app_id_item || !prefix_item || !path_item || prefix_item->text() != "True")
    return "";
  sfs::path path(path_item->text().toStdString());
  return (path.parent_path().parent_path() / "compatdata" / app_id_item->text().toStdString() /
          "pfx" / "drive_c")
    .c_str();
}

std::optional<std::string> ImportFromSteamDialog::parseVdfValue(const std::string& line,
                                                               const std::string& key) const
{
  // Matches a VDF "key" "value" pair tolerant of surrounding whitespace and tabs.
  // The key is matched case-insensitively and the value may contain anything but a quote.
  const std::regex key_value_regex(
    "\\s*\"" + key + "\"\\s*\"([^\"]*)\"", std::regex::icase);
  std::smatch match;
  if(std::regex_search(line, match, key_value_regex))
    return match[1].str();
  return std::nullopt;
}

std::optional<sfs::path> ImportFromSteamDialog::locateAppManifest(
  const std::string& app_id,
  const sfs::path& preferred_path,
  const std::vector<sfs::path>& library_paths) const
{
  const std::string file_name = std::string("appmanifest_") + app_id + ".acf";
  // Check the library folder this app was listed under first, then fall back to every
  // known library folder, since the appmanifest may actually live in a different one.
  const sfs::path preferred = preferred_path / "steamapps" / file_name;
  std::error_code ec;
  if(sfs::is_regular_file(preferred, ec))
    return preferred;
  for(const auto& library_path : library_paths)
  {
    const sfs::path candidate = library_path / "steamapps" / file_name;
    if(sfs::is_regular_file(candidate, ec))
      return candidate;
  }
  return std::nullopt;
}

bool ImportFromSteamDialog::addTableRow(std::string app_id,
                                        sfs::path path,
                                        const std::vector<sfs::path>& library_paths)
{
  // Name | App ID | Prefix | Path
  const std::optional<sfs::path> file_path = locateAppManifest(app_id, path, library_paths);
  if(!file_path)
  {
    Log::warning(std::string("Could not find appmanifest for app \"") + app_id +
                 "\" in any Steam library; skipping.");
    return false;
  }
  // Use the library folder that actually contains the appmanifest for all derived paths.
  path = file_path->parent_path().parent_path();
  std::ifstream file(*file_path);
  if(!file.is_open())
  {
    Log::warning(std::string("Could not open \"") + file_path->string() +
                 "\"; skipping app \"" + app_id + "\".");
    return false;
  }
  std::string line;
  std::string name = "";
  std::string install_dir = "";
  while(std::getline(file, line))
  {
    if(name.empty())
    {
      if(auto value = parseVdfValue(line, "name"))
        name = *value;
    }
    if(install_dir.empty())
    {
      if(auto value = parseVdfValue(line, "installdir"))
        install_dir = *value;
    }
    if(!name.empty() && !install_dir.empty())
      break;
  }
  if(install_dir.empty())
  {
    Log::warning(std::string("Appmanifest \"") + file_path->string() +
                 "\" is missing an \"installdir\" entry; skipping app \"" + app_id + "\".");
    return false;
  }
  if(name.empty())
    name = install_dir;
  sfs::path full_path = path / "steamapps" / "common" / install_dir;
  QString has_prefix = "False";
  if(sfs::exists(path / "steamapps" / "compatdata" / app_id))
    has_prefix = "True";
  sfs::path icon_path = path / "appcache" / "librarycache" / (app_id + "_icon.jpg");
  if(!sfs::exists(icon_path))
  {
    icon_path = path / "appcache" / "librarycache" / app_id;
    std::regex name_regex(R"(([0-9a-fA-F]{40})\.jpg)");
    bool found = false;
    if(sfs::exists(icon_path))
    {
      for(const auto& dir_entry : sfs::directory_iterator(icon_path))
      {
        const std::string file_name = dir_entry.path().filename();
        if(std::regex_match(file_name, name_regex))
        {
          icon_path /= file_name;
          found = true;
          break;
        }
      }
    }
    if(!found)
      icon_path.clear();
  }
  int row = ui->app_table->rowCount();
  ui->app_table->insertRow(row);
  ui->app_table->setItem(
    row, 0, new QTableWidgetItem(QIcon(icon_path.string().c_str()), name.c_str()));
  ui->app_table->item(row, 0)->setData(Qt::UserRole, QString(icon_path.string().c_str()));
  ui->app_table->setItem(row, 1, new QTableWidgetItem(app_id.c_str()));
  ui->app_table->setItem(row, 2, new QTableWidgetItem(has_prefix));
  ui->app_table->setItem(row, 3, new QTableWidgetItem(full_path.c_str()));
  // Column 4: flag whether Limo ships a preset for this app so the user can see at a
  // glance which installed games can be auto-configured (and filter to just those).
  const bool supported = AddAppDialog::hasGameConfig(app_id);
  auto* preset_item = new QTableWidgetItem(supported ? QString("✓ Yes") : QString());
  preset_item->setData(Qt::UserRole, supported);
  ui->app_table->setItem(row, 4, preset_item);
  return true;
}

void ImportFromSteamDialog::showError(QString title, QString message)
{
  Log::error(message.toStdString());
  QMessageBox* error_box = new QMessageBox(QMessageBox::Critical, title, message, QMessageBox::Ok);
  error_box->exec();
}

void ImportFromSteamDialog::on_buttonBox_accepted()
{
  if(dialog_completed_)
    return;
  dialog_completed_ = true;

  int row = ui->app_table->currentRow();
  if(row < 0 || row >= ui->app_table->rowCount())
    return;
  QTableWidgetItem* name_item = ui->app_table->item(row, 0);
  QTableWidgetItem* app_id_item = ui->app_table->item(row, 1);
  QTableWidgetItem* prefix_item = ui->app_table->item(row, 2);
  QTableWidgetItem* path_item = ui->app_table->item(row, 3);
  if(!name_item || !app_id_item || !prefix_item || !path_item)
    return;
  QString name = name_item->text();
  QString icon_path = name_item->data(Qt::UserRole).toString();
  if(!sfs::exists(icon_path.toStdString()))
    icon_path = "";
  QString app_id = app_id_item->text();
  sfs::path path(path_item->text().toStdString());
  // Use whatever is in the prefix field: it is auto-populated from the detected Proton
  // prefix on selection, but the user may have overridden it for a non-standard prefix.
  QString prefix_path = ui->prefix_field->text().trimmed();
  (void)prefix_item;
  emit applicationImported(name, app_id, path.string().c_str(), prefix_path, icon_path);
}

void ImportFromSteamDialog::on_path_field_editingFinished()
{
  updateTable(ui->path_field->text().toStdString());
}

void ImportFromSteamDialog::on_search_field_textEdited(const QString& /*new_text*/)
{
  applyFilters();
}

void ImportFromSteamDialog::on_supported_only_checkbox_toggled(bool /*checked*/)
{
  applyFilters();
}

void ImportFromSteamDialog::on_app_table_itemSelectionChanged()
{
  // Show the auto-detected prefix for the selected game so the user can review or
  // override it (e.g. for a non-standard STEAM_COMPAT_DATA_PATH or a manual WINEPREFIX).
  ui->prefix_field->setText(autoPrefixForRow(ui->app_table->currentRow()));
}

void ImportFromSteamDialog::on_pick_prefix_button_clicked()
{
  QString starting_dir = ui->prefix_field->text();
  if(starting_dir.isEmpty() || !sfs::exists(starting_dir.toStdString()))
    starting_dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  const QString path = QFileDialog::getExistingDirectory(
    this, "Select prefix drive_c Directory", starting_dir, QFileDialog::ShowDirsOnly);
  if(!path.isEmpty())
    ui->prefix_field->setText(path);
}

void ImportFromSteamDialog::onAddAllSupportedClicked()
{
  QList<QStringList> games;
  for(int i = 0; i < ui->app_table->rowCount(); i++)
  {
    const QTableWidgetItem* preset_item = ui->app_table->item(i, 4);
    if(!preset_item || !preset_item->data(Qt::UserRole).toBool())
      continue;
    const QTableWidgetItem* name_item = ui->app_table->item(i, 0);
    const QTableWidgetItem* app_id_item = ui->app_table->item(i, 1);
    const QTableWidgetItem* path_item = ui->app_table->item(i, 3);
    if(!name_item || !app_id_item || !path_item)
      continue;
    QString icon_path = name_item->data(Qt::UserRole).toString();
    if(!sfs::exists(icon_path.toStdString()))
      icon_path = "";
    games.append(QStringList{
      name_item->text(), app_id_item->text(), path_item->text(), autoPrefixForRow(i), icon_path });
  }
  if(games.isEmpty())
  {
    QMessageBox::information(
      this, "No supported games", "None of the detected games have a Limo preset to import.");
    return;
  }
  if(QMessageBox::question(
       this,
       "Add all supported games",
       QString("Add %1 supported game(s)? You will be asked for a parent folder to hold each "
               "game's mod staging directory.")
         .arg(games.size())) != QMessageBox::Yes)
    return;
  dialog_completed_ = true;
  emit addAllSupportedRequested(games);
  accept();
}
