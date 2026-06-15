#include "deploypreviewdialog.h"
#include "ui_deploypreviewdialog.h"
#include <QString>
#include <QTreeWidgetItem>


DeployPreviewDialog::DeployPreviewDialog(const std::vector<Deployer::DeploymentPlan>& plans,
                                         QWidget* parent) :
  QDialog(parent), ui(new Ui::DeployPreviewDialog)
{
  ui->setupUi(this);
  setWindowTitle("Deployment Preview");
  populate(plans);
}

DeployPreviewDialog::DeployPreviewDialog(const Deployer::DeploymentPlan& plan, QWidget* parent) :
  DeployPreviewDialog(std::vector<Deployer::DeploymentPlan>{ plan }, parent)
{}

DeployPreviewDialog::~DeployPreviewDialog()
{
  delete ui;
}

void DeployPreviewDialog::populate(const std::vector<Deployer::DeploymentPlan>& plans)
{
  size_t total_create = 0;
  size_t total_overwrite = 0;
  size_t total_remove = 0;

  ui->change_tree->clear();
  ui->change_tree->setColumnCount(2);
  ui->change_tree->setHeaderLabels({ "File", "Details" });

  // Helper which adds a category node with one child per entry.
  const auto add_category =
    [this](QTreeWidgetItem* parent,
           const QString& label,
           const std::vector<Deployer::DeploymentPlan::Entry>& entries,
           const auto& detail_for)
  {
    auto* category = new QTreeWidgetItem(parent);
    category->setText(0, QString("%1 (%2)").arg(label).arg(entries.size()));
    category->setFirstColumnSpanned(true);
    for(const auto& entry : entries)
    {
      auto* item = new QTreeWidgetItem(category);
      item->setText(0, QString::fromStdString(entry.path.string()));
      item->setText(1, detail_for(entry));
    }
  };

  for(const auto& plan : plans)
  {
    total_create += plan.to_create.size();
    total_overwrite += plan.to_overwrite.size();
    total_remove += plan.to_remove.size();

    auto* deployer_node = new QTreeWidgetItem(ui->change_tree);
    deployer_node->setText(0,
                           QString("%1  (%2 changes)")
                             .arg(QString::fromStdString(plan.deployer_name))
                             .arg(plan.numChanges()));
    deployer_node->setFirstColumnSpanned(true);

    add_category(deployer_node,
                 "To create",
                 plan.to_create,
                 [](const Deployer::DeploymentPlan::Entry& e)
                 { return QString("from mod %1").arg(e.mod_id); });
    add_category(deployer_node,
                 "To overwrite",
                 plan.to_overwrite,
                 [](const Deployer::DeploymentPlan::Entry& e) {
                   return QString("mod %1 -> mod %2").arg(e.previous_mod_id).arg(e.mod_id);
                 });
    add_category(deployer_node,
                 "To remove",
                 plan.to_remove,
                 [](const Deployer::DeploymentPlan::Entry& e)
                 { return QString("was from mod %1").arg(e.previous_mod_id); });

    deployer_node->setExpanded(true);
  }

  ui->summary_label->setText(QString("Create: %1    Overwrite: %2    Remove: %3")
                               .arg(total_create)
                               .arg(total_overwrite)
                               .arg(total_remove));
}
