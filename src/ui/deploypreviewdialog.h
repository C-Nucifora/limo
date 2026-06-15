/*!
 * \file deploypreviewdialog.h
 * \brief Header for the DeployPreviewDialog class.
 */

#pragma once

#include "../core/deployer.h"
#include <QDialog>
#include <vector>


namespace Ui
{
class DeployPreviewDialog;
}

/*!
 * \brief Dialog which previews the changes a deployment would make before committing it.
 *
 * Given one or more \ref Deployer::DeploymentPlan instances (computed without touching the
 * disk via \ref Deployer::computeDeploymentPlan), it shows the number of files which would be
 * created, overwritten and removed together with the corresponding file lists in a tree, and
 * lets the user proceed (OK) or abort (Cancel).
 */
class DeployPreviewDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the ui from the given deployment plans.
   * \param plans One plan per deployer that would be deployed.
   * \param parent Parent of this widget.
   */
  explicit DeployPreviewDialog(const std::vector<Deployer::DeploymentPlan>& plans,
                               QWidget* parent = nullptr);
  /*!
   * \brief Convenience constructor for a single deployment plan.
   * \param plan The plan to preview.
   * \param parent Parent of this widget.
   */
  explicit DeployPreviewDialog(const Deployer::DeploymentPlan& plan, QWidget* parent = nullptr);
  /*! \brief Deletes the ui. */
  ~DeployPreviewDialog();

private:
  /*! \brief Auto generated ui elements. */
  Ui::DeployPreviewDialog* ui;

  /*!
   * \brief Populates the summary label and the tree from the given plans.
   * \param plans The plans to display.
   */
  void populate(const std::vector<Deployer::DeploymentPlan>& plans);
};
