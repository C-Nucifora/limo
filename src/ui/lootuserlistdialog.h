/*!
 * \file lootuserlistdialog.h
 * \brief Header for the LootUserlistDialog class.
 */

#pragma once

#include "../core/lootdeployer.h"
#include <QDialog>
#include <filesystem>
#include <memory>
#include <vector>


namespace Ui
{
class LootUserlistDialog;
}

/*!
 * \brief Dialog for editing LOOT user metadata (per-plugin group and load-after
 * rules) and writing the result back to userlist.yaml.
 *
 * fork #31: Lets the user assign each plugin to a LOOT group and define load-after
 * rules. The edits are applied through \ref LootDeployer::writePluginUserMetadata,
 * which preserves any user metadata Limo does not model.
 */
class LootUserlistDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Constructs the dialog for a LOOT deployer located at the given paths.
   * \param source_path Directory containing the installed plugins (used to detect the game type).
   * \param dest_path Directory containing plugins.txt/loadorder.txt and userlist.yaml.
   * \param parent Parent of this widget.
   */
  LootUserlistDialog(const std::filesystem::path& source_path,
                     const std::filesystem::path& dest_path,
                     QWidget* parent = nullptr);
  /*! \brief Deletes the ui. */
  ~LootUserlistDialog();

private:
  /*! \brief Auto generated ui elements. */
  Ui::LootUserlistDialog* ui;
  /*! \brief Deployer used to read and write the user metadata. */
  std::unique_ptr<LootDeployer> deployer_;
  /*! \brief Currently edited per-plugin metadata, mirrored from/to the ui. */
  std::vector<LootDeployer::PluginUserMetadata> metadata_;
  /*! \brief Index of the plugin currently shown in the editor, or -1 if none. */
  int current_index_ = -1;

  /*! \brief Copies the editor fields for the currently selected plugin into \ref metadata_. */
  void storeCurrentEdits();
  /*! \brief Populates the editor fields from \ref metadata_ for the given plugin index. */
  void showPlugin(int index);

private slots:
  /*! \brief Stores the previous plugin's edits and shows the newly selected plugin. */
  void onPluginSelectionChanged(int index);
  /*! \brief Adds the plugin name in the load-after field to the current plugin's rules. */
  void onAddAfter();
  /*! \brief Removes the selected entry from the current plugin's load-after rules. */
  void onRemoveAfter();
  /*! \brief Writes all edited metadata to userlist.yaml, then closes the dialog. */
  void onSave();
};
