#include "lootuserlistdialog.h"
#include "ui_lootuserlistdialog.h"
#include <QMessageBox>
#include <QPushButton>


LootUserlistDialog::LootUserlistDialog(const std::filesystem::path& source_path,
                                       const std::filesystem::path& dest_path,
                                       QWidget* parent) :
  QDialog(parent), ui(new Ui::LootUserlistDialog)
{
  ui->setupUi(this);
  setWindowTitle("Edit LOOT User Metadata");

  deployer_ = std::make_unique<LootDeployer>(source_path, dest_path, "LOOT user metadata editor");
  metadata_ = deployer_->getPluginUserMetadata();

  for(const auto& entry : metadata_)
    ui->plugin_list->addItem(QString::fromStdString(entry.plugin));

  connect(ui->plugin_list,
          &QListWidget::currentRowChanged,
          this,
          &LootUserlistDialog::onPluginSelectionChanged);
  connect(ui->add_after_button, &QPushButton::clicked, this, &LootUserlistDialog::onAddAfter);
  connect(
    ui->remove_after_button, &QPushButton::clicked, this, &LootUserlistDialog::onRemoveAfter);
  connect(ui->buttonBox->button(QDialogButtonBox::Save),
          &QPushButton::clicked,
          this,
          &LootUserlistDialog::onSave);

  if(!metadata_.empty())
    ui->plugin_list->setCurrentRow(0);
  else
    showPlugin(-1);
}

LootUserlistDialog::~LootUserlistDialog()
{
  delete ui;
}

void LootUserlistDialog::storeCurrentEdits()
{
  if(current_index_ < 0 || current_index_ >= static_cast<int>(metadata_.size()))
    return;
  auto& entry = metadata_[current_index_];
  entry.group = ui->group_field->text().trimmed().toStdString();
  entry.load_after.clear();
  for(int i = 0; i < ui->after_list->count(); i++)
    entry.load_after.push_back(ui->after_list->item(i)->text().toStdString());
}

void LootUserlistDialog::showPlugin(int index)
{
  ui->group_field->clear();
  ui->after_list->clear();
  ui->after_field->clear();
  const bool valid = index >= 0 && index < static_cast<int>(metadata_.size());
  ui->group_field->setEnabled(valid);
  ui->after_list->setEnabled(valid);
  ui->after_field->setEnabled(valid);
  ui->add_after_button->setEnabled(valid);
  ui->remove_after_button->setEnabled(valid);
  if(!valid)
  {
    current_index_ = -1;
    return;
  }
  const auto& entry = metadata_[index];
  ui->group_field->setText(QString::fromStdString(entry.group));
  for(const auto& after : entry.load_after)
    ui->after_list->addItem(QString::fromStdString(after));
  current_index_ = index;
}

void LootUserlistDialog::onPluginSelectionChanged(int index)
{
  storeCurrentEdits();
  showPlugin(index);
}

void LootUserlistDialog::onAddAfter()
{
  const QString name = ui->after_field->text().trimmed();
  if(name.isEmpty())
    return;
  const auto existing = ui->after_list->findItems(name, Qt::MatchFixedString);
  if(existing.isEmpty())
    ui->after_list->addItem(name);
  ui->after_field->clear();
}

void LootUserlistDialog::onRemoveAfter()
{
  const int row = ui->after_list->currentRow();
  if(row >= 0)
    delete ui->after_list->takeItem(row);
}

void LootUserlistDialog::onSave()
{
  storeCurrentEdits();
  try
  {
    deployer_->writePluginUserMetadata(metadata_);
  }
  catch(const std::exception& e)
  {
    QMessageBox::critical(
      this, "Error", QString("Could not write userlist.yaml:\n") + e.what());
    return;
  }
  accept();
}
