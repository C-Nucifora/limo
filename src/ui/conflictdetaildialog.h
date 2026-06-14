/*!
 * \file conflictdetaildialog.h
 * \brief Header for the ConflictDetailDialog class.
 */

#pragma once

#include "../core/conflictinfo.h"
#include <QDialog>
#include <QString>
#include <vector>


namespace Ui
{
class ConflictDetailDialog;
}

/*!
 * \brief Dialog that shows per-file win/loss conflict detail for a single mod in a deployer.
 *
 * "Files this mod wins" lists every conflicting file where the selected mod is the last
 * (highest-priority) entry in the load order and therefore has its version deployed.
 * "Files this mod loses" lists every conflicting file where a later mod overwrites the
 * selected mod, together with the name of that winning mod.
 *
 * The winner is determined by the last element of ConflictInfo::mod_ids, which matches
 * the deployment rule in Deployer::getDeploymentSourceFilesAndModSizes (last mod in load
 * order wins).
 */
class ConflictDetailDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Constructs the dialog and populates the two lists from the provided conflict data.
   * \param mod_id    Id of the mod whose conflicts are being displayed.
   * \param mod_name  Human-readable name of that mod (used in the window title).
   * \param conflicts Result of Deployer::getFileConflicts / ModdedApplication::getFileConflicts
   *                  for this mod. Each entry's mod_ids and mod_names are already in load-order
   *                  sequence with mod_names filled in by ModdedApplication.
   * \param parent    Optional parent widget.
   */
  explicit ConflictDetailDialog(int mod_id,
                                const QString& mod_name,
                                const std::vector<ConflictInfo>& conflicts,
                                QWidget* parent = nullptr);

  /*! \brief Deletes the UI. */
  ~ConflictDetailDialog();

private:
  /*! \brief Auto-generated UI elements. */
  Ui::ConflictDetailDialog* ui;
};
