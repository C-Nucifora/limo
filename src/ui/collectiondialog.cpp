#include "collectiondialog.h"
#include "ui_collectiondialog.h"
#include "../core/log.h"
#include "../core/nexus/api.h"
#include <QFileDialog>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidgetItem>


CollectionDialog::CollectionDialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::CollectionDialog)
{
  ui->setupUi(this);
}

CollectionDialog::~CollectionDialog()
{
  delete ui;
}

bool CollectionDialog::setupImport(int app_id)
{
  mode_ = import_mode;
  app_id_ = app_id;
  dialog_completed_ = false;

  const QString path = QFileDialog::getOpenFileName(
    this, "Import Nexus Collection", QString(), "Collection manifest (collection.json *.json)");
  if(path.isEmpty())
    return false;

  try
  {
    collection_ = nexus::Collection::fromFile(path.toStdString());
  }
  catch(const std::exception& error)
  {
    emit collectionError("Failed to import collection",
                         QString("Could not parse '%1':\n%2").arg(path).arg(error.what()));
    return false;
  }

  entries_ = collection_.getEntries();
  game_domain_ = QString::fromStdString(collection_.game_domain);
  collection_name_ = QString::fromStdString(collection_.name);

  setWindowTitle("Import Nexus Collection");
  int nexus_count = 0;
  for(const auto& entry : entries_)
    if(entry.isNexusSource())
      nexus_count++;
  ui->header_label->setText(
    QString("Collection '%1' by %2\n%3 mod(s), %4 will be downloaded from NexusMods.")
      .arg(QString::fromStdString(collection_.name))
      .arg(QString::fromStdString(collection_.author))
      .arg(static_cast<int>(entries_.size()))
      .arg(nexus_count));
  ui->buttonBox->button(QDialogButtonBox::Ok)->setText("Import");
  populateTable();
  return true;
}

void CollectionDialog::setupExport(int app_id,
                                   const QString& game_domain,
                                   const QString& collection_name,
                                   const std::vector<ModInfo>& mods)
{
  mode_ = export_mode;
  app_id_ = app_id;
  game_domain_ = game_domain;
  collection_name_ = collection_name;
  dialog_completed_ = false;

  // Build entries from every mod that carries valid NexusMods identifiers. Mods without
  // a known remote source can not be expressed in a collection manifest and are skipped.
  entries_.clear();
  int skipped = 0;
  int phase = 0;
  for(const ModInfo& info : mods)
  {
    if(info.mod.remote_mod_id <= 0 || info.mod.remote_file_id <= 0)
    {
      skipped++;
      continue;
    }
    nexus::Collection::Entry entry;
    entry.name = info.mod.name;
    entry.version = info.mod.version;
    entry.domain = game_domain.toStdString();
    entry.mod_id = info.mod.remote_mod_id;
    entry.file_id = info.mod.remote_file_id;
    entry.phase = phase++;
    entries_.push_back(entry);
  }

  setWindowTitle("Export Nexus Collection");
  ui->header_label->setText(
    QString("Export collection '%1': %2 mod(s) with NexusMods sources%3.")
      .arg(collection_name)
      .arg(static_cast<int>(entries_.size()))
      .arg(skipped > 0 ? QString(", %1 local mod(s) skipped").arg(skipped) : QString()));
  ui->buttonBox->button(QDialogButtonBox::Ok)->setText("Export");
  populateTable();
}

void CollectionDialog::populateTable()
{
  ui->mod_table->setRowCount(static_cast<int>(entries_.size()));
  for(int row = 0; row < static_cast<int>(entries_.size()); row++)
  {
    const auto& entry = entries_[row];
    ui->mod_table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(entry.name)));
    ui->mod_table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(entry.version)));
    ui->mod_table->setItem(
      row, 2, new QTableWidgetItem(entry.mod_id > 0 ? QString::number(entry.mod_id) : "-"));
    ui->mod_table->setItem(
      row, 3, new QTableWidgetItem(entry.file_id > 0 ? QString::number(entry.file_id) : "-"));
  }
  ui->mod_table->resizeColumnsToContents();
}

void CollectionDialog::on_buttonBox_accepted()
{
  if(dialog_completed_)
    return;
  dialog_completed_ = true;

  if(mode_ == import_mode)
  {
    // Route each Nexus mod through the existing queued download/import flow. Non-Nexus
    // entries are skipped rather than aborting the whole import.
    int queued = 0;
    int skipped = 0;
    for(const auto& entry : entries_)
    {
      if(!entry.isNexusSource() || entry.file_id <= 0)
      {
        skipped++;
        Log::info("Skipping collection mod '" + entry.name + "': no NexusMods file id.");
        continue;
      }
      // The second parameter is the Limo target-group id (see NexusModDialog). Collection
      // mods are installed fresh, so pass -1; the Nexus mod id is carried in the URL.
      emit modDownloadRequested(app_id_,
                                -1,
                                static_cast<int>(entry.file_id),
                                QString::fromStdString(entry.modUrl()),
                                QString::fromStdString(entry.version));
      queued++;
    }
    Log::info("Queued " + std::to_string(queued) + " collection mod(s) for download (" +
              std::to_string(skipped) + " skipped).");
    if(skipped > 0)
      emit collectionError(
        "Collection import",
        QString("%1 mod(s) were skipped because they have no NexusMods source. "
                "The remaining %2 mod(s) were queued for download.")
          .arg(skipped)
          .arg(queued));
  }
  else
  {
    const QString path = QFileDialog::getSaveFileName(
      this,
      "Export Nexus Collection",
      collection_name_.isEmpty() ? "collection.json" : collection_name_ + ".json",
      "Collection manifest (*.json)");
    if(path.isEmpty())
    {
      dialog_completed_ = false;
      return;
    }
    try
    {
      const nexus::Collection collection = nexus::Collection::fromEntries(
        collection_name_.toStdString(), game_domain_.toStdString(), entries_);
      collection.toFile(path.toStdString());
      Log::info("Exported collection with " + std::to_string(entries_.size()) +
                " mod(s) to '" + path.toStdString() + "'.");
    }
    catch(const std::exception& error)
    {
      emit collectionError("Failed to export collection",
                           QString("Could not write '%1':\n%2").arg(path).arg(error.what()));
      dialog_completed_ = false;
      return;
    }
  }

  accept();
}

void CollectionDialog::on_buttonBox_rejected()
{
  if(dialog_completed_)
    return;
  dialog_completed_ = true;

  reject();
}
