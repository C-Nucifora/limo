// fork #11: virtual deployed-file tree with per-file mod origin

#include "deployedfilestreedialog.h"
#include "ui_deployedfilestreedialog.h"
#include <QStringList>
#include <QTreeWidgetItem>
#include <map>


DeployedFilesTreeDialog::DeployedFilesTreeDialog(const QString& deployer_name,
                                                 const std::vector<Deployer::FileOrigin>& origins,
                                                 const std::map<int, QString>& mod_names,
                                                 QWidget* parent) :
  QDialog(parent), ui(new Ui::DeployedFilesTreeDialog)
{
  ui->setupUi(this);
  setWindowTitle(QString("Deployed Files - %1").arg(deployer_name));

  ui->summary_label->setText(QString("Deployment of '%1': %2 deployed files.")
                               .arg(deployer_name)
                               .arg(static_cast<int>(origins.size())));

  ui->files_tree->setColumnCount(3);
  ui->files_tree->setHeaderLabels(QStringList{ "File", "Provided by", "Overwrites" });

  // Maps the relative directory path of a node to its tree item, so that directories are only
  // created once and files end up under the correct parent.
  std::map<std::string, QTreeWidgetItem*> dir_items;

  for(const auto& origin : origins)
  {
    QTreeWidgetItem* parent_item = nullptr;
    std::string accumulated;
    const auto parent_path = origin.path.parent_path();
    for(const auto& component : parent_path)
    {
      const std::string component_string = component.string();
      if(component_string.empty())
        continue;
      if(!accumulated.empty())
        accumulated += "/";
      accumulated += component_string;

      auto it = dir_items.find(accumulated);
      if(it == dir_items.end())
      {
        auto* dir_item = new QTreeWidgetItem(QStringList{
          QString::fromStdString(component_string), QString(), QString() });
        if(parent_item)
          parent_item->addChild(dir_item);
        else
          ui->files_tree->addTopLevelItem(dir_item);
        dir_items[accumulated] = dir_item;
        parent_item = dir_item;
      }
      else
        parent_item = it->second;
    }

    QString conflicts;
    if(!origin.conflicting_mod_ids.empty())
    {
      QStringList labels;
      for(int conflicting : origin.conflicting_mod_ids)
        labels << modLabel(conflicting, mod_names);
      conflicts = labels.join(", ");
    }

    auto* file_item = new QTreeWidgetItem(
      QStringList{ QString::fromStdString(origin.path.filename().string()),
                   modLabel(origin.mod_id, mod_names),
                   conflicts });
    if(parent_item)
      parent_item->addChild(file_item);
    else
      ui->files_tree->addTopLevelItem(file_item);
  }

  ui->files_tree->expandAll();
  ui->files_tree->resizeColumnToContents(0);
  ui->files_tree->resizeColumnToContents(1);
}

DeployedFilesTreeDialog::~DeployedFilesTreeDialog()
{
  delete ui;
}

QString DeployedFilesTreeDialog::modLabel(int mod_id, const std::map<int, QString>& mod_names)
{
  const auto it = mod_names.find(mod_id);
  if(it != mod_names.end() && !it->second.isEmpty())
    return QString("%1 (%2)").arg(it->second).arg(mod_id);
  return QString("Mod %1").arg(mod_id);
}
