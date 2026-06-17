#include "modpacksdialog.h"
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>


ModpacksDialog::ModpacksDialog(QWidget* parent) : QDialog(parent)
{
  setWindowTitle("Modpacks");
  resize(440, 560);

  auto* layout = new QVBoxLayout(this);

  auto* intro = new QLabel(
    tr("Packs are named, ordered sets of mods. Tick a pack to activate it; several can be active "
       "at once and the deployed set is their union, ordered by pack priority then in-pack order. "
       "Click Deploy to apply."),
    this);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  layout->addWidget(new QLabel(tr("Packs:"), this));
  pack_list_ = new QListWidget(this);
  layout->addWidget(pack_list_, 1);
  connect(pack_list_, &QListWidget::itemChanged, this, &ModpacksDialog::onPackItemChanged);
  connect(
    pack_list_, &QListWidget::itemSelectionChanged, this, &ModpacksDialog::onPackSelectionChanged);

  auto* pack_buttons = new QHBoxLayout();
  auto* new_button = new QPushButton(tr("New"), this);
  rename_button_ = new QPushButton(tr("Rename"), this);
  remove_button_ = new QPushButton(tr("Delete"), this);
  notes_button_ = new QPushButton(tr("Notes"), this);
  pack_buttons->addWidget(new_button);
  pack_buttons->addWidget(rename_button_);
  pack_buttons->addWidget(remove_button_);
  pack_buttons->addWidget(notes_button_);
  layout->addLayout(pack_buttons);
  connect(new_button, &QPushButton::clicked, this, &ModpacksDialog::onNewPackClicked);
  connect(rename_button_, &QPushButton::clicked, this, &ModpacksDialog::onRenamePackClicked);
  connect(remove_button_, &QPushButton::clicked, this, &ModpacksDialog::onRemovePackClicked);
  connect(notes_button_, &QPushButton::clicked, this, &ModpacksDialog::onEditNotesClicked);

  notes_label_ = new QLabel(this);
  notes_label_->setWordWrap(true);
  notes_label_->setEnabled(false);
  layout->addWidget(notes_label_);

  layout->addWidget(new QLabel(tr("Mods in the selected pack (deploy order):"), this));
  member_list_ = new QListWidget(this);
  layout->addWidget(member_list_, 1);

  auto* member_buttons = new QHBoxLayout();
  add_mods_button_ = new QPushButton(tr("Add Mods..."), this);
  remove_mod_button_ = new QPushButton(tr("Remove"), this);
  up_button_ = new QPushButton(tr("Up"), this);
  down_button_ = new QPushButton(tr("Down"), this);
  member_buttons->addWidget(add_mods_button_);
  member_buttons->addWidget(remove_mod_button_);
  member_buttons->addWidget(up_button_);
  member_buttons->addWidget(down_button_);
  layout->addLayout(member_buttons);
  connect(add_mods_button_, &QPushButton::clicked, this, &ModpacksDialog::onAddModsClicked);
  connect(remove_mod_button_, &QPushButton::clicked, this, &ModpacksDialog::onRemoveModClicked);
  connect(up_button_, &QPushButton::clicked, this, &ModpacksDialog::onMoveModUpClicked);
  connect(down_button_, &QPushButton::clicked, this, &ModpacksDialog::onMoveModDownClicked);

  auto* button_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(button_box);

  onPackSelectionChanged();
}

void ModpacksDialog::setPackData(const QString& json)
{
  const QString previously_selected = selectedPack();

  pack_order_.clear();
  active_.clear();
  pack_notes_.clear();
  pack_mods_.clear();
  mod_names_.clear();

  const QJsonObject root = QJsonDocument::fromJson(json.toUtf8()).object();
  for(const QJsonValue& name : root.value("active").toArray())
    active_.append(name.toString());
  for(const QJsonValue& mod : root.value("mods").toArray())
  {
    const QJsonObject obj = mod.toObject();
    mod_names_[obj.value("id").toInt()] = obj.value("name").toString();
  }
  for(const QJsonValue& pack : root.value("packs").toArray())
  {
    const QJsonObject obj = pack.toObject();
    const QString name = obj.value("name").toString();
    pack_order_.append(name);
    pack_notes_[name] = obj.value("notes").toString();
    QList<int> mods;
    for(const QJsonValue& id : obj.value("mods").toArray())
      mods.append(id.toInt());
    pack_mods_[name] = mods;
  }

  updating_ = true;
  pack_list_->clear();
  for(const QString& name : pack_order_)
  {
    auto* item = new QListWidgetItem(name, pack_list_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(active_.contains(name) ? Qt::Checked : Qt::Unchecked);
    if(name == previously_selected)
      pack_list_->setCurrentItem(item);
  }
  updating_ = false;
  onPackSelectionChanged();
}

QString ModpacksDialog::selectedPack() const
{
  QListWidgetItem* item = pack_list_->currentItem();
  return item ? item->text() : QString();
}

void ModpacksDialog::onPackItemChanged(QListWidgetItem* item)
{
  if(updating_ || item == nullptr)
    return;
  emit packToggled(item->text(), item->checkState() == Qt::Checked);
}

void ModpacksDialog::onPackSelectionChanged()
{
  const QString pack = selectedPack();
  const bool has_pack = !pack.isEmpty();
  rename_button_->setEnabled(has_pack);
  remove_button_->setEnabled(has_pack);
  notes_button_->setEnabled(has_pack);
  add_mods_button_->setEnabled(has_pack);
  remove_mod_button_->setEnabled(has_pack);
  up_button_->setEnabled(has_pack);
  down_button_->setEnabled(has_pack);
  const QString notes = pack_notes_.value(pack);
  notes_label_->setText(notes.isEmpty() ? tr("(no notes)") : notes);
  refreshMembers();
}

void ModpacksDialog::refreshMembers()
{
  member_list_->clear();
  const QString pack = selectedPack();
  if(pack.isEmpty())
    return;
  for(int id : pack_mods_.value(pack))
  {
    const QString name = mod_names_.value(id, tr("Mod %1").arg(id));
    auto* item = new QListWidgetItem(name, member_list_);
    item->setData(Qt::UserRole, id);
  }
}

void ModpacksDialog::commitMembers()
{
  const QString pack = selectedPack();
  if(pack.isEmpty())
    return;
  QList<int> ids;
  for(int i = 0; i < member_list_->count(); i++)
    ids.append(member_list_->item(i)->data(Qt::UserRole).toInt());
  pack_mods_[pack] = ids;
  emit setPackModsRequested(pack, ids);
}

void ModpacksDialog::onNewPackClicked()
{
  bool ok = false;
  const QString name =
    QInputDialog::getText(this, tr("New Pack"), tr("Pack name:"), QLineEdit::Normal, QString(), &ok)
      .trimmed();
  if(ok && !name.isEmpty())
    emit newPackRequested(name);
}

void ModpacksDialog::onRenamePackClicked()
{
  const QString pack = selectedPack();
  if(pack.isEmpty())
    return;
  bool ok = false;
  const QString name =
    QInputDialog::getText(this, tr("Rename Pack"), tr("New name:"), QLineEdit::Normal, pack, &ok)
      .trimmed();
  if(ok && !name.isEmpty() && name != pack)
    emit renamePackRequested(pack, name);
}

void ModpacksDialog::onRemovePackClicked()
{
  const QString pack = selectedPack();
  if(!pack.isEmpty())
    emit removePackRequested(pack);
}

void ModpacksDialog::onEditNotesClicked()
{
  const QString pack = selectedPack();
  if(pack.isEmpty())
    return;
  bool ok = false;
  const QString notes = QInputDialog::getMultiLineText(
    this, tr("Pack Notes"), tr("Notes for '%1':").arg(pack), pack_notes_.value(pack), &ok);
  if(ok)
    emit setPackNotesRequested(pack, notes);
}

void ModpacksDialog::onAddModsClicked()
{
  const QString pack = selectedPack();
  if(pack.isEmpty())
    return;
  const QList<int> current = pack_mods_.value(pack);

  QDialog picker(this);
  picker.setWindowTitle(tr("Add Mods to '%1'").arg(pack));
  picker.resize(360, 420);
  auto* picker_layout = new QVBoxLayout(&picker);
  picker_layout->addWidget(new QLabel(tr("Select mods to add:"), &picker));
  auto* picker_list = new QListWidget(&picker);
  picker_layout->addWidget(picker_list, 1);
  // Offer every mod not already in the pack.
  for(auto it = mod_names_.constBegin(); it != mod_names_.constEnd(); ++it)
  {
    if(current.contains(it.key()))
      continue;
    auto* item = new QListWidgetItem(it.value(), picker_list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    item->setData(Qt::UserRole, it.key());
  }
  auto* picker_buttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &picker);
  connect(picker_buttons, &QDialogButtonBox::accepted, &picker, &QDialog::accept);
  connect(picker_buttons, &QDialogButtonBox::rejected, &picker, &QDialog::reject);
  picker_layout->addWidget(picker_buttons);

  if(picker.exec() != QDialog::Accepted)
    return;
  QList<int> ids = current;
  for(int i = 0; i < picker_list->count(); i++)
  {
    QListWidgetItem* item = picker_list->item(i);
    if(item->checkState() == Qt::Checked)
      ids.append(item->data(Qt::UserRole).toInt());
  }
  pack_mods_[pack] = ids;
  emit setPackModsRequested(pack, ids);
}

void ModpacksDialog::onRemoveModClicked()
{
  const int row = member_list_->currentRow();
  if(row < 0)
    return;
  delete member_list_->takeItem(row);
  commitMembers();
}

void ModpacksDialog::onMoveModUpClicked()
{
  const int row = member_list_->currentRow();
  if(row <= 0)
    return;
  member_list_->insertItem(row - 1, member_list_->takeItem(row));
  member_list_->setCurrentRow(row - 1);
  commitMembers();
}

void ModpacksDialog::onMoveModDownClicked()
{
  const int row = member_list_->currentRow();
  if(row < 0 || row >= member_list_->count() - 1)
    return;
  member_list_->insertItem(row + 1, member_list_->takeItem(row));
  member_list_->setCurrentRow(row + 1);
  commitMembers();
}
