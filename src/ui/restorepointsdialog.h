/*!
 * \file restorepointsdialog.h
 * \brief Header for the RestorePointsDialog class.
 */

// fork #54: dialog listing deploy restore points (load-order snapshots) with
// one-click restore / delete.

#pragma once

#include "core/moddedapplication.h"
#include <QDialog>
#include <vector>


namespace Ui
{
class RestorePointsDialog;
}

/*!
 * \brief Dialog used to list deploy restore points and request restore / delete.
 *
 * The dialog is purely a view: it emits \ref restoreRequested / \ref deleteRequested
 * with the index of the selected restore point and leaves the actual work to the caller.
 */
class RestorePointsDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Populates the list with the given restore points.
   * \param points Restore point metadata; the index of each entry is used to address it.
   * \param parent Parent widget.
   */
  explicit RestorePointsDialog(const std::vector<RestorePoint>& points,
                               QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~RestorePointsDialog();

signals:
  /*!
   * \brief Emitted when the user requests restoring the snapshot at the given index.
   * \param index Index into the restore point list.
   */
  void restoreRequested(int index);
  /*!
   * \brief Emitted when the user requests deleting the snapshot at the given index.
   * \param index Index into the restore point list.
   */
  void deleteRequested(int index);

private slots:
  /*! \brief Emits restoreRequested for the selected row, then closes. */
  void on_restore_button_clicked();
  /*! \brief Emits deleteRequested for the selected row, then closes. */
  void on_delete_button_clicked();
  /*! \brief Closes the dialog. */
  void on_close_button_clicked();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::RestorePointsDialog* ui;
};
