/*!
 * \file deployverifydialog.h
 * \brief Header for the DeployVerifyDialog class.
 */

// fork #53: deployment integrity verification

#pragma once

#include "core/deployer.h"
#include <QDialog>
#include <QString>


namespace Ui
{
class DeployVerifyDialog;
}

/*!
 * \brief Read-only dialog which presents the result of a deployment integrity check
 * (see Deployer::verifyDeployment): counts and lists of missing, modified and
 * wrong-link-type files.
 */
class DeployVerifyDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI and fills it with the given verification result.
   * \param deployer_name Name of the deployer which was verified.
   * \param result The result returned by Deployer::verifyDeployment.
   * \param parent Parent for this widget, passed to the constructor of QDialog.
   */
  explicit DeployVerifyDialog(const QString& deployer_name,
                              const Deployer::VerificationResult& result,
                              QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~DeployVerifyDialog();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::DeployVerifyDialog* ui;

  /*!
   * \brief Appends a titled section listing the given paths to the details view.
   * \param title Section header.
   * \param paths Paths to list under the header.
   */
  void appendSection(const QString& title, const std::vector<std::filesystem::path>& paths);
};
