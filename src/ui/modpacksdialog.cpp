#include "modpacksdialog.h"
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>


ModpacksDialog::ModpacksDialog(QWidget* parent) : QDialog(parent)
{
  setWindowTitle("Modpacks");
  resize(360, 420);

  auto* layout = new QVBoxLayout(this);

  auto* intro = new QLabel(
    tr("Toggle modpacks for the current profile. Several can be active at once; the deployed "
       "set is the union of all active packs. A pack is a tag — assign mods to it by tagging "
       "them (right-click a mod → Edit Tags). Click Deploy to apply."),
    this);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  list_ = new QListWidget(this);
  layout->addWidget(list_, 1);
  connect(list_, &QListWidget::itemChanged, this, &ModpacksDialog::onItemChanged);

  empty_label_ = new QLabel(tr("No packs yet. Create one, then tag mods to add them."), this);
  empty_label_->setWordWrap(true);
  empty_label_->setEnabled(false);
  empty_label_->hide();
  layout->addWidget(empty_label_);

  auto* button_box = new QDialogButtonBox(this);
  auto* new_pack_button = button_box->addButton(tr("New Pack..."), QDialogButtonBox::ActionRole);
  button_box->addButton(QDialogButtonBox::Close);
  connect(new_pack_button, &QPushButton::clicked, this, &ModpacksDialog::onNewPackClicked);
  connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(button_box);
}

void ModpacksDialog::setPacks(const QStringList& all_packs, const QStringList& active_packs)
{
  updating_ = true;
  list_->clear();
  for(const QString& pack : all_packs)
  {
    auto* item = new QListWidgetItem(pack, list_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(active_packs.contains(pack) ? Qt::Checked : Qt::Unchecked);
  }
  updating_ = false;
  const bool empty = all_packs.isEmpty();
  list_->setVisible(!empty);
  empty_label_->setVisible(empty);
}

void ModpacksDialog::onItemChanged(QListWidgetItem* item)
{
  if(updating_ || item == nullptr)
    return;
  emit packToggled(item->text(), item->checkState() == Qt::Checked);
}

void ModpacksDialog::onNewPackClicked()
{
  bool ok = false;
  const QString name =
    QInputDialog::getText(
      this, tr("New Pack"), tr("Pack name:"), QLineEdit::Normal, QString(), &ok)
      .trimmed();
  if(ok && !name.isEmpty())
    emit newPackRequested(name);
}
