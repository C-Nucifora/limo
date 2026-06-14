#include "managegroupsdialog.h"
#include "ui_managegroupsdialog.h"

#include <QMessageBox>

ManageGroupsDialog::ManageGroupsDialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::ManageGroupsDialog)
{
  ui->setupUi(this);
  setWindowTitle("Manage Groups");
  // Close button connects via .ui connections block
  connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ManageGroupsDialog::~ManageGroupsDialog()
{
  delete ui;
}

void ManageGroupsDialog::setupDialog(int app_id,
                                     int group_count,
                                     const std::vector<std::string>& group_names,
                                     const std::vector<std::string>& group_notes,
                                     const std::vector<std::vector<int>>& group_members,
                                     const std::vector<int>& active_members,
                                     const std::map<int, std::string>& mod_names)
{
  app_id_ = app_id;
  group_names_ = group_names;
  group_notes_ = group_notes;
  group_members_ = group_members;
  active_members_ = active_members;
  mod_names_ = mod_names;

  // Ensure parallel vectors are sized correctly even if caller omits trailing data
  group_names_.resize(group_count);
  group_notes_.resize(group_count);
  group_members_.resize(group_count);
  active_members_.resize(group_count, -1);

  ui->group_list->clear();
  for(int i = 0; i < group_count; i++)
    ui->group_list->addItem(groupDisplayName(i));

  current_group_ = -1;
  refreshDetailPanel();

  if(group_count > 0)
    ui->group_list->setCurrentRow(0);
}

QString ManageGroupsDialog::groupDisplayName(int idx) const
{
  if(idx < 0 || idx >= (int)group_names_.size())
    return QString("Group %1").arg(idx);
  const auto& name = group_names_[idx];
  if(!name.empty())
    return QString::fromStdString(name);
  return QString("Group %1").arg(idx + 1);
}

void ManageGroupsDialog::refreshDetailPanel()
{
  const bool have_group = (current_group_ >= 0 && current_group_ < (int)group_names_.size());

  ui->name_edit->setEnabled(have_group);
  ui->rename_button->setEnabled(have_group);
  ui->notes_edit->setEnabled(have_group);
  ui->save_notes_button->setEnabled(have_group);
  ui->member_list->setEnabled(have_group);
  ui->set_active_button->setEnabled(have_group);
  ui->dissolve_button->setEnabled(have_group);

  if(!have_group)
  {
    ui->name_edit->clear();
    ui->notes_edit->clear();
    ui->member_list->clear();
    return;
  }

  ui->name_edit->setText(QString::fromStdString(group_names_[current_group_]));
  ui->notes_edit->setPlainText(QString::fromStdString(group_notes_[current_group_]));

  ui->member_list->clear();
  const int active = current_group_ < (int)active_members_.size() ? active_members_[current_group_]
                                                                   : -1;
  for(int mod_id : group_members_[current_group_])
  {
    auto it = mod_names_.find(mod_id);
    QString display =
      (it != mod_names_.end()) ? QString::fromStdString(it->second) : QString::number(mod_id);
    if(mod_id == active)
      display += " [active]";
    auto* item = new QListWidgetItem(display);
    item->setData(Qt::UserRole, mod_id);
    ui->member_list->addItem(item);
  }
}

// ---- slots ----------------------------------------------------------------

void ManageGroupsDialog::on_group_list_currentRowChanged(int row)
{
  current_group_ = row;
  refreshDetailPanel();
}

void ManageGroupsDialog::on_rename_button_clicked()
{
  if(current_group_ < 0 || current_group_ >= (int)group_names_.size())
    return;
  const QString new_name = ui->name_edit->text().trimmed();
  group_names_[current_group_] = new_name.toStdString();

  // Update the list widget label
  if(auto* item = ui->group_list->item(current_group_))
    item->setText(groupDisplayName(current_group_));

  emit groupRenamed(app_id_, current_group_, new_name);
}

void ManageGroupsDialog::on_save_notes_button_clicked()
{
  if(current_group_ < 0 || current_group_ >= (int)group_notes_.size())
    return;
  const QString notes = ui->notes_edit->toPlainText();
  group_notes_[current_group_] = notes.toStdString();
  emit groupNotesChanged(app_id_, current_group_, notes);
}

void ManageGroupsDialog::on_set_active_button_clicked()
{
  if(current_group_ < 0)
    return;
  auto* item = ui->member_list->currentItem();
  if(!item)
    return;
  const int mod_id = item->data(Qt::UserRole).toInt();
  if(current_group_ < (int)active_members_.size())
    active_members_[current_group_] = mod_id;
  refreshDetailPanel();
  emit activeGroupMemberChanged(app_id_, current_group_, mod_id);
}

void ManageGroupsDialog::on_dissolve_button_clicked()
{
  if(current_group_ < 0)
    return;
  const int reply =
    QMessageBox::question(this,
                          "Dissolve Group",
                          "Remove all members from this group? The mods themselves are not deleted.",
                          QMessageBox::Yes | QMessageBox::No,
                          QMessageBox::No);
  if(reply != QMessageBox::Yes)
    return;

  const int group = current_group_;

  // Remove from local state
  group_names_.erase(group_names_.begin() + group);
  group_notes_.erase(group_notes_.begin() + group);
  group_members_.erase(group_members_.begin() + group);
  active_members_.erase(active_members_.begin() + group);

  ui->group_list->takeItem(group);

  current_group_ = -1;
  refreshDetailPanel();

  emit groupDissolved(app_id_, group);
}
