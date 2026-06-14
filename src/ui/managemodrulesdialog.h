/*!
 * \file managemodrulesdialog.h
 * \brief Header for the ManageModRulesDialog class.
 */

#pragma once

#include "../core/modrule.h"
#include <QDialog>
#include <vector>

namespace Ui
{
class ManageModRulesDialog;
}

/*!
 * \brief Dialog for viewing and editing the dependency / conflict rules of a single mod.
 *
 * Rules are stored app-globally via ModdedApplication. The dialog emits rulesChanged
 * with the complete new rule list for the source mod whenever the user adds or removes a rule,
 * allowing the parent widget (e.g. ApplicationManager) to call
 * ModdedApplication::setModRulesFor.
 */
class ManageModRulesDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI.
   * \param parent Parent widget.
   */
  explicit ManageModRulesDialog(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~ManageModRulesDialog();

  /*!
   * \brief Populates the dialog for a given source mod.
   * \param app_id Target application id.
   * \param source_mod_id Id of the mod whose rules are being edited.
   * \param source_mod_name Display name of the source mod.
   * \param all_mods A vector of (mod_id, mod_name) pairs for every installed mod except
   *                 source_mod_id. Used to populate the target selector.
   * \param current_rules Current rules already stored for source_mod_id.
   */
  void setupDialog(int app_id,
                   int source_mod_id,
                   const QString& source_mod_name,
                   const std::vector<std::pair<int, QString>>& all_mods,
                   const std::vector<ModRule>& current_rules);

signals:
  /*!
   * \brief Emitted when the rule list for the source mod has changed.
   * \param app_id Target application id.
   * \param source_mod_id The mod whose rules changed.
   * \param rules The complete updated rule list for that mod.
   */
  void rulesChanged(int app_id, int source_mod_id, std::vector<ModRule> rules);

private slots:
  /*! \brief Adds the rule described by the current UI state. */
  void on_add_rule_button_clicked();
  /*! \brief Removes the currently selected rule. */
  void on_remove_rule_button_clicked();

private:
  /*! \brief Auto-generated UI. */
  Ui::ManageModRulesDialog* ui;
  /*! \brief Application id passed to setupDialog. */
  int app_id_ = -1;
  /*! \brief Id of the mod whose rules are being edited. */
  int source_mod_id_ = -1;
  /*! \brief Current rules for source_mod_id_. Modified in-place and emitted on every change. */
  std::vector<ModRule> rules_;
  /*! \brief Maps combo box index to mod id for the target selector. */
  std::vector<int> target_ids_;

  /*! \brief Rebuilds the rules table from rules_. */
  void refreshTable();
  /*! \brief Emits rulesChanged with the current rules_. */
  void emitChanged();
};
