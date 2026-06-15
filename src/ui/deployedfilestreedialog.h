/*!
 * \file deployedfilestreedialog.h
 * \brief Header for the DeployedFilesTreeDialog class.
 */

// fork #11: virtual deployed-file tree with per-file mod origin

#pragma once

#include "core/deployer.h"
#include <QDialog>
#include <QString>
#include <map>
#include <vector>


namespace Ui
{
class DeployedFilesTreeDialog;
}

class QTreeWidgetItem;

/*!
 * \brief Read-only dialog which renders the currently deployed file layout as a tree of
 * directories and files, showing for every file which mod provides it (the winner) and any
 * conflicting mods. See Deployer::getDeployedFileOrigins.
 */
class DeployedFilesTreeDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI and fills the tree with the given deployed file origins.
   * \param deployer_name Name of the deployer whose files are shown.
   * \param origins The deployed files and their providing mods.
   * \param mod_names Maps mod ids to human readable names. Ids not present fall back to the id.
   * \param parent Parent for this widget, passed to the constructor of QDialog.
   */
  explicit DeployedFilesTreeDialog(const QString& deployer_name,
                                   const std::vector<Deployer::FileOrigin>& origins,
                                   const std::map<int, QString>& mod_names,
                                   QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~DeployedFilesTreeDialog();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::DeployedFilesTreeDialog* ui;

  /*!
   * \brief Returns a display string for the given mod id, using the name map if available.
   * \param mod_id The mod id to resolve.
   * \param mod_names Maps mod ids to names.
   * \return The resolved name, or "Mod <id>" as a fallback.
   */
  static QString modLabel(int mod_id, const std::map<int, QString>& mod_names);
};
