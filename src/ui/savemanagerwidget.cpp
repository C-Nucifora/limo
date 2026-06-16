/*!
 * \file savemanagerwidget.cpp
 * \brief Implements the SaveManagerWidget class.
 *
 * Fork feature #24: save-game manager.
 */

#include "savemanagerwidget.h"
#include "ui_savemanagerwidget.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QSettings>
#include <QTableWidgetItem>
#include <QUrl>
#include <algorithm>

namespace sfs = std::filesystem;


SaveManagerWidget::SaveManagerWidget(QWidget* parent) :
  QWidget(parent), ui(new Ui::SaveManagerWidget)
{
  ui->setupUi(this);
  ui->save_table->setColumnCount(3);
  ui->save_table->setHorizontalHeaderLabels({ "Name", "Last modified", "Size" });
  ui->save_table->horizontalHeader()->setSectionResizeMode(COL_NAME, QHeaderView::Stretch);
  ui->save_table->horizontalHeader()->setSectionResizeMode(COL_DATE,
                                                           QHeaderView::ResizeToContents);
  ui->save_table->horizontalHeader()->setSectionResizeMode(COL_SIZE,
                                                           QHeaderView::ResizeToContents);
  ui->save_table->sortByColumn(COL_DATE, Qt::DescendingOrder);

  connect(ui->path_picker_button, &QPushButton::clicked, this,
          &SaveManagerWidget::onPathPickerClicked);
  connect(ui->path_field, &QLineEdit::editingFinished, this, &SaveManagerWidget::onPathEdited);
  connect(ui->refresh_button, &QPushButton::clicked, this, &SaveManagerWidget::onRefreshClicked);
  connect(ui->open_button, &QPushButton::clicked, this, &SaveManagerWidget::onOpenClicked);
  connect(ui->delete_button, &QPushButton::clicked, this, &SaveManagerWidget::onDeleteClicked);
}

SaveManagerWidget::~SaveManagerWidget()
{
  delete ui;
}

void SaveManagerWidget::setAppContext(int app_id, const QString& suggested_dir)
{
  app_id_ = app_id;
  loadPersistedPath(suggested_dir);
  refresh();
}

QString SaveManagerWidget::settingsKey() const
{
  // Per-app key; mirrors the per-app QSettings convention used elsewhere in the main window.
  return QString("saves/%1/dir").arg(app_id_);
}

void SaveManagerWidget::loadPersistedPath(const QString& suggested_dir)
{
  if(app_id_ < 0)
  {
    ui->path_field->clear();
    return;
  }
  QSettings settings = QSettings(QCoreApplication::applicationName());
  const QString path = settings.value(settingsKey(), suggested_dir).toString();
  ui->path_field->setText(path);
}

void SaveManagerWidget::persistPath()
{
  if(app_id_ < 0)
    return;
  QSettings settings = QSettings(QCoreApplication::applicationName());
  settings.setValue(settingsKey(), ui->path_field->text());
}

void SaveManagerWidget::refresh()
{
  const bool was_sorting = ui->save_table->isSortingEnabled();
  ui->save_table->setSortingEnabled(false);
  ui->save_table->setRowCount(0);

  const sfs::path dir = ui->path_field->text().toStdString();
  saves_ = SaveManager::listSaves(dir);

  ui->save_table->setRowCount(static_cast<int>(saves_.size()));
  for(int row = 0; row < static_cast<int>(saves_.size()); row++)
  {
    const SaveManager::SaveFile& save = saves_[row];

    auto* name_item = new QTableWidgetItem(QString::fromStdString(save.name));
    // Store the absolute path as a stable key so that selection survives sorting and
    // deletion always targets the correct file regardless of the current row order.
    name_item->setData(Qt::UserRole, QString::fromStdString(save.path.string()));
    ui->save_table->setItem(row, COL_NAME, name_item);

    const QDateTime dt = QDateTime::fromSecsSinceEpoch(save.timestamp);
    auto* date_item = new QTableWidgetItem(dt.toString("yyyy-MM-dd hh:mm:ss"));
    // Sort the date column by the raw timestamp instead of the formatted string.
    date_item->setData(Qt::UserRole, QVariant::fromValue<qint64>(save.timestamp));
    ui->save_table->setItem(row, COL_DATE, date_item);

    auto* size_item = new QTableWidgetItem(formatSize(save.size));
    size_item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(save.size));
    ui->save_table->setItem(row, COL_SIZE, size_item);
  }

  ui->save_table->setSortingEnabled(was_sorting);
}

QString SaveManagerWidget::formatSize(std::uintmax_t bytes)
{
  constexpr double unit = 1024.0;
  const char* suffixes[] = { "B", "KiB", "MiB", "GiB", "TiB" };
  double size = static_cast<double>(bytes);
  int idx = 0;
  while(size >= unit && idx < 4)
  {
    size /= unit;
    idx++;
  }
  if(idx == 0)
    return QString("%1 B").arg(bytes);
  return QString("%1 %2").arg(size, 0, 'f', 1).arg(suffixes[idx]);
}

void SaveManagerWidget::onPathPickerClicked()
{
  const QString start = ui->path_field->text().isEmpty() ? QString() : ui->path_field->text();
  const QString dir =
    QFileDialog::getExistingDirectory(this, "Select saves directory", start);
  if(dir.isEmpty())
    return;
  ui->path_field->setText(dir);
  persistPath();
  refresh();
}

void SaveManagerWidget::onPathEdited()
{
  persistPath();
  refresh();
}

void SaveManagerWidget::onRefreshClicked()
{
  refresh();
}

void SaveManagerWidget::onOpenClicked()
{
  const sfs::path dir = SaveManager::resolveSavesDir(ui->path_field->text().toStdString());
  if(dir.empty())
  {
    QMessageBox::warning(
      this, "Invalid directory", "The configured saves directory does not exist.");
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(dir.string())));
}

void SaveManagerWidget::onDeleteClicked()
{
  QList<QTableWidgetItem*> selected = ui->save_table->selectedItems();
  // Resolve the selected saves by their stable absolute-path key (stored in UserRole)
  // rather than by a row index, which can become stale once the table is sorted.
  std::vector<const SaveManager::SaveFile*> targets;
  std::vector<std::string> seen_paths;
  for(QTableWidgetItem* item : selected)
  {
    if(item->column() != COL_NAME)
      continue;
    const std::string key = item->data(Qt::UserRole).toString().toStdString();
    if(key.empty())
      continue;
    // Guard against selecting the same logical save twice.
    if(std::find(seen_paths.begin(), seen_paths.end(), key) != seen_paths.end())
      continue;
    for(const SaveManager::SaveFile& save : saves_)
    {
      if(save.path.string() == key)
      {
        targets.push_back(&save);
        seen_paths.push_back(key);
        break;
      }
    }
  }
  if(targets.empty())
    return;

  // List the actual file names so the user can verify before a destructive,
  // non-undoable delete.
  QStringList names;
  for(const SaveManager::SaveFile* save : targets)
    names << QString::fromStdString(save->name).toHtmlEscaped();
  const QString message =
    targets.size() == 1
      ? QString("Permanently delete the following save file?\n\n%1").arg(names.front())
      : QString("Permanently delete the following %1 save files?\n\n%2")
          .arg(targets.size())
          .arg(names.join('\n'));
  if(QMessageBox::question(this, "Delete saves", message) != QMessageBox::Yes)
    return;

  for(const SaveManager::SaveFile* save : targets)
  {
    try
    {
      SaveManager::deleteSave(save->path);
    }
    catch(const std::exception& e)
    {
      QMessageBox::warning(this, "Deletion failed", e.what());
    }
  }
  refresh();
}
