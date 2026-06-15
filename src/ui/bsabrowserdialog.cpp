// fork #201: BSA/BA2 archive browser & extractor

#include "bsabrowserdialog.h"
#include "ui_bsabrowserdialog.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <filesystem>
#include <stdexcept>

namespace sfs = std::filesystem;


BsaBrowserDialog::BsaBrowserDialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::BsaBrowserDialog)
{
  ui->setupUi(this);

  ui->entries_table->setColumnCount(3);
  ui->entries_table->setHorizontalHeaderLabels(QStringList{ "Path", "Size", "Compressed" });
  ui->entries_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  ui->entries_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  ui->entries_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  ui->entries_table->verticalHeader()->setVisible(false);

  connect(ui->open_button, &QPushButton::clicked, this, &BsaBrowserDialog::onOpenArchive);
  connect(ui->extract_button, &QPushButton::clicked, this, &BsaBrowserDialog::onExtractSelected);
  connect(ui->entries_table, &QTableWidget::itemSelectionChanged, this,
          &BsaBrowserDialog::onSelectionChanged);
}

BsaBrowserDialog::~BsaBrowserDialog()
{
  delete ui;
}

QString BsaBrowserDialog::humanSize(uint64_t bytes)
{
  constexpr const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
  double value = static_cast<double>(bytes);
  int unit = 0;
  while(value >= 1024.0 && unit < 4)
  {
    value /= 1024.0;
    ++unit;
  }
  if(unit == 0)
    return QString("%1 B").arg(bytes);
  return QString("%1 %2").arg(value, 0, 'f', 1).arg(units[unit]);
}

void BsaBrowserDialog::onOpenArchive()
{
  const QString path = QFileDialog::getOpenFileName(
    this, "Open Archive", QString(), "Bethesda archives (*.bsa *.ba2);;All files (*)");
  if(path.isEmpty())
    return;

  try
  {
    archive_ = std::make_unique<BsaArchive>(sfs::path(path.toStdString()));
  }
  catch(const std::exception& e)
  {
    archive_.reset();
    populateTable();
    QMessageBox::critical(this, "Failed to open archive",
                          QString("Could not read the archive:\n%1").arg(e.what()));
    return;
  }

  setWindowTitle(QString("BSA/BA2 Archive Browser - %1").arg(QFileInfo(path).fileName()));
  populateTable();
}

void BsaBrowserDialog::populateTable()
{
  ui->entries_table->setSortingEnabled(false);
  ui->entries_table->setRowCount(0);

  if(!archive_)
  {
    ui->summary_label->setText("No archive opened.");
    ui->extract_button->setEnabled(false);
    return;
  }

  const auto& entries = archive_->entries();
  ui->entries_table->setRowCount(static_cast<int>(entries.size()));

  uint64_t total_size = 0;
  for(int row = 0; row < static_cast<int>(entries.size()); ++row)
  {
    const auto& entry = entries[row];
    total_size += entry.size;

    auto* path_item = new QTableWidgetItem(QString::fromStdString(entry.path));
    path_item->setFlags(path_item->flags() & ~Qt::ItemIsEditable);
    // Store the internal path so extraction is robust against sorting.
    path_item->setData(Qt::UserRole, QString::fromStdString(entry.path));

    auto* size_item = new QTableWidgetItem(humanSize(entry.size));
    size_item->setFlags(size_item->flags() & ~Qt::ItemIsEditable);
    size_item->setData(Qt::DisplayRole, humanSize(entry.size));
    // Numeric sort key via user role is not used by default sort; keep simple.

    QString comp = entry.listing_only ? "listing only" : (entry.compressed ? "yes" : "no");
    auto* comp_item = new QTableWidgetItem(comp);
    comp_item->setFlags(comp_item->flags() & ~Qt::ItemIsEditable);

    ui->entries_table->setItem(row, 0, path_item);
    ui->entries_table->setItem(row, 1, size_item);
    ui->entries_table->setItem(row, 2, comp_item);
  }

  ui->entries_table->setSortingEnabled(true);
  ui->summary_label->setText(QString("%1: %2 entries, %3 total (uncompressed).")
                               .arg(QString::fromStdString(archive_->formatName()))
                               .arg(static_cast<int>(entries.size()))
                               .arg(humanSize(total_size)));
  onSelectionChanged();
}

void BsaBrowserDialog::onSelectionChanged()
{
  const bool has_selection =
    archive_ && !ui->entries_table->selectionModel()->selectedRows().isEmpty();
  ui->extract_button->setEnabled(has_selection);
}

void BsaBrowserDialog::onExtractSelected()
{
  if(!archive_)
    return;

  const QModelIndexList rows = ui->entries_table->selectionModel()->selectedRows();
  if(rows.isEmpty())
    return;

  const QString dest_dir = QFileDialog::getExistingDirectory(this, "Select destination directory");
  if(dest_dir.isEmpty())
    return;

  int extracted = 0;
  int skipped = 0;
  QStringList errors;

  for(const QModelIndex& index : rows)
  {
    QTableWidgetItem* item = ui->entries_table->item(index.row(), 0);
    if(!item)
      continue;
    const QString internal = item->data(Qt::UserRole).toString();
    const std::string internal_std = internal.toStdString();

    // Preserve the internal relative path under the destination directory.
    const sfs::path dest = sfs::path(dest_dir.toStdString()) / sfs::path(internal_std);
    try
    {
      if(archive_->extractTo(internal_std, dest))
        ++extracted;
      else
        ++skipped;
    }
    catch(const std::exception& e)
    {
      errors << QString("%1: %2").arg(internal).arg(e.what());
    }
  }

  QString message = QString("Extracted %1 file(s).").arg(extracted);
  if(skipped > 0)
    message += QString("\nSkipped %1 unsupported entry(ies) (e.g. DX10 textures).").arg(skipped);
  if(!errors.isEmpty())
  {
    message += QString("\n\n%1 error(s):\n%2").arg(errors.size()).arg(errors.join("\n"));
    QMessageBox::warning(this, "Extraction completed with errors", message);
  }
  else
  {
    QMessageBox::information(this, "Extraction complete", message);
  }
}
