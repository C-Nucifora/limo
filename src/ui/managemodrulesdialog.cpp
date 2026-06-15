#include "managemodrulesdialog.h"
#include "ui_managemodrulesdialog.h"
#include <QHeaderView>

ManageModRulesDialog::ManageModRulesDialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::ManageModRulesDialog)
{
  ui->setupUi(this);
  ui->rule_type_box->addItems({ "requires", "conflicts with" });
  ui->rules_table->horizontalHeader()->setStretchLastSection(true);
  setWindowTitle("Manage Mod Rules");
}

ManageModRulesDialog::~ManageModRulesDialog()
{
  delete ui;
}

void ManageModRulesDialog::setupDialog(int app_id,
                                       int source_mod_id,
                                       const QString& source_mod_name,
                                       const std::vector<std::pair<int, QString>>& all_mods,
                                       const std::vector<ModRule>& current_rules)
{
  app_id_ = app_id;
  source_mod_id_ = source_mod_id;
  rules_ = current_rules;

  ui->title_label->setText(QString("Rules for \"%1\":").arg(source_mod_name));

  ui->target_mod_box->clear();
  target_ids_.clear();
  id_to_name_.clear();
  for(const auto& [id, name] : all_mods)
  {
    ui->target_mod_box->addItem(name);
    target_ids_.push_back(id);
    id_to_name_[id] = name;
  }

  refreshTable();
}

void ManageModRulesDialog::refreshTable()
{
  ui->rules_table->setRowCount(0);
  for(const auto& rule : rules_)
  {
    int row = ui->rules_table->rowCount();
    ui->rules_table->insertRow(row);
    ui->rules_table->setItem(row, 0,
      new QTableWidgetItem(QString::fromStdString(ModRule::typeLabel(rule.type))));
    auto it = id_to_name_.find(rule.target_mod_id);
    const QString target_name = (it != id_to_name_.end())
                                  ? it->second
                                  : QString("(id %1)").arg(rule.target_mod_id);
    ui->rules_table->setItem(row, 1, new QTableWidgetItem(target_name));
  }
}

void ManageModRulesDialog::emitChanged()
{
  emit rulesChanged(app_id_, source_mod_id_, rules_);
}

void ManageModRulesDialog::on_add_rule_button_clicked()
{
  if(target_ids_.empty())
    return;

  const int combo_idx = ui->target_mod_box->currentIndex();
  if(combo_idx < 0 || combo_idx >= static_cast<int>(target_ids_.size()))
    return;

  const int target_id = target_ids_[combo_idx];
  const RuleType type = ui->rule_type_box->currentIndex() == 0
                          ? RuleType::requires_mod
                          : RuleType::conflicts_with;

  ModRule new_rule(source_mod_id_, type, target_id);

  // Prevent duplicates.
  for(const auto& r : rules_)
  {
    if(r == new_rule)
      return;
  }

  rules_.push_back(new_rule);
  refreshTable();
  emitChanged();
}

void ManageModRulesDialog::on_remove_rule_button_clicked()
{
  const int row = ui->rules_table->currentRow();
  if(row < 0 || row >= static_cast<int>(rules_.size()))
    return;

  rules_.erase(rules_.begin() + row);
  refreshTable();
  emitChanged();
}
