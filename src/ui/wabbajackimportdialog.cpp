/*!
 * \file wabbajackimportdialog.cpp
 * \brief Implementation of the WabbajackImportDialog class.
 *
 * fork #197
 */

#include "wabbajackimportdialog.h"
#include "ui_wabbajackimportdialog.h"
#include <QFileDialog>
#include <QHeaderView>
#include <QLocale>
#include <QTableWidgetItem>


WabbajackImportDialog::WabbajackImportDialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::WabbajackImportDialog)
{
  ui->setupUi(this);
  setWindowTitle("Import Wabbajack Modlist");

  ui->archive_table->setColumnCount(5);
  ui->archive_table->setHorizontalHeaderLabels(
    { "Name", "Source", "Mod ID", "File ID", "Size" });
  ui->archive_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
}

WabbajackImportDialog::~WabbajackImportDialog()
{
  delete ui;
}

int WabbajackImportDialog::nexusArchiveCount() const
{
  int count = 0;
  for(const auto& archive : modlist_.archives)
  {
    if(archive.is_nexus)
      count++;
  }
  return count;
}

void WabbajackImportDialog::updateView()
{
  ui->archive_table->setRowCount(0);

  if(!modlist_.ok)
  {
    ui->info_label->setText(
      QString("Failed to load modlist: %1").arg(QString::fromStdString(modlist_.error)));
    ui->queue_button->setEnabled(false);
    return;
  }

  const int nexus_count = nexusArchiveCount();
  ui->info_label->setText(
    QString("<b>%1</b> by %2<br/>Game: %3 &nbsp; Version: %4<br/>"
            "%5 archives (%6 Nexus) &nbsp; %7 directives")
      .arg(QString::fromStdString(modlist_.name).toHtmlEscaped())
      .arg(QString::fromStdString(modlist_.author).toHtmlEscaped())
      .arg(QString::fromStdString(modlist_.game_type).toHtmlEscaped())
      .arg(QString::fromStdString(modlist_.version).toHtmlEscaped())
      .arg(modlist_.archives.size())
      .arg(nexus_count)
      .arg(modlist_.directive_count));

  ui->archive_table->setRowCount(static_cast<int>(modlist_.archives.size()));
  QLocale locale;
  for(int row = 0; row < static_cast<int>(modlist_.archives.size()); row++)
  {
    const WabbajackArchive& archive = modlist_.archives[row];
    ui->archive_table->setItem(
      row, 0, new QTableWidgetItem(QString::fromStdString(archive.name)));
    ui->archive_table->setItem(
      row,
      1,
      new QTableWidgetItem(archive.is_nexus ? QString::fromStdString(archive.game_name)
                                            : QString("non-Nexus")));
    ui->archive_table->setItem(
      row,
      2,
      new QTableWidgetItem(archive.is_nexus && archive.mod_id >= 0
                             ? QString::number(archive.mod_id)
                             : QString()));
    ui->archive_table->setItem(
      row,
      3,
      new QTableWidgetItem(archive.is_nexus && archive.file_id >= 0
                             ? QString::number(archive.file_id)
                             : QString()));
    ui->archive_table->setItem(
      row,
      4,
      new QTableWidgetItem(locale.formattedDataSize(static_cast<qint64>(archive.size))));
  }

  ui->queue_button->setEnabled(nexus_count > 0);
}

void WabbajackImportDialog::on_open_button_clicked()
{
  const QString path = QFileDialog::getOpenFileName(
    this, "Open Wabbajack Modlist", QString(), "Wabbajack Modlists (*.wabbajack)");
  if(path.isEmpty())
    return;
  modlist_ = parseWabbajack(path.toStdString());
  updateView();
}

void WabbajackImportDialog::on_queue_button_clicked()
{
  if(!modlist_.ok)
    return;
  for(const auto& archive : modlist_.archives)
  {
    if(!archive.is_nexus || archive.mod_id < 0 || archive.file_id < 0)
      continue;
    emit requestNexusDownload(QString::fromStdString(archive.game_name),
                              static_cast<int>(archive.mod_id),
                              static_cast<int>(archive.file_id),
                              QString::fromStdString(archive.version));
  }
  accept();
}
