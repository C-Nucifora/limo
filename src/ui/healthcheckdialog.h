/*!
 * \file healthcheckdialog.h
 * \brief Header for the HealthCheckDialog class.
 */

// fork #50: 'Problems' / health-check panel

#pragma once

#include "core/deployer.h"
#include <QDialog>
#include <QString>


namespace Ui
{
class HealthCheckDialog;
}

/*!
 * \brief Read-only dialog which presents the aggregated problems found by
 * Deployer::runHealthCheck, grouped by category (orphaned files, broken/incorrect links and
 * conflicts) with per-category counts.
 */
class HealthCheckDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI and fills it with the given health-check result.
   * \param result The result returned by Deployer::runHealthCheck.
   * \param parent Parent for this widget, passed to the constructor of QDialog.
   */
  explicit HealthCheckDialog(const Deployer::HealthCheckResult& result, QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~HealthCheckDialog();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::HealthCheckDialog* ui;

  /*!
   * \brief Appends a titled section listing the given paths to the details view.
   * \param title Section header.
   * \param paths Paths to list under the header.
   */
  void appendSection(const QString& title, const std::vector<std::filesystem::path>& paths);
  /*!
   * \brief Appends the conflict section listing each conflicting path and the mods involved.
   * \param conflicts Conflicts to list.
   */
  void appendConflicts(const std::vector<Deployer::HealthCheckResult::Conflict>& conflicts);
};
