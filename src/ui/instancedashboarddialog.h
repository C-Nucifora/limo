/*!
 * \file instancedashboarddialog.h
 * \brief Header for the InstanceDashboardDialog class.
 */

// fork #203: instance dashboard with mod/plugin counts, total size and quick status

#pragma once

#include <QDialog>
#include <QString>
#include <QtGlobal>


namespace Ui
{
class InstanceDashboardDialog;
}

/*!
 * \brief Pre-computed snapshot statistics for the active app/profile, displayed by
 * InstanceDashboardDialog. The caller (main window) computes these from ModdedApplication
 * and passes them in.
 */
struct InstanceDashboardStats
{
  /*! \brief Name of the active app/profile. */
  QString app_name;
  /*! \brief Total number of installed mods. */
  int total_mods = 0;
  /*! \brief Number of enabled mods. */
  int enabled_mods = 0;
  /*! \brief Number of disabled mods. */
  int disabled_mods = 0;
  /*! \brief Number of plugins, or -1 if unknown/not applicable. */
  int plugin_count = 0;
  /*! \brief Total staging directory size in bytes, or <0 if unknown. */
  qint64 total_staging_bytes = 0;
  /*! \brief Last deploy time as a human-readable string, or empty if unknown. */
  QString last_deploy;
  /*! \brief Short human-readable status line. */
  QString status_line;
};

/*!
 * \brief Read-only snapshot dialog presenting an at-a-glance overview of the active
 * app/profile: mod counts, plugin count, total staging size, last deploy time and a status line.
 */
class InstanceDashboardDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI and fills it with the given pre-computed stats.
   * \param stats The snapshot statistics to display.
   * \param parent Parent for this widget, passed to the constructor of QDialog.
   */
  explicit InstanceDashboardDialog(const InstanceDashboardStats& stats, QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~InstanceDashboardDialog();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::InstanceDashboardDialog* ui;

  /*!
   * \brief Formats a byte count as a human-readable string (B/KiB/MiB/GiB/TiB).
   * \param bytes Byte count; negative values yield "unknown".
   * \return Human-readable size string.
   */
  static QString formatSize(qint64 bytes);
};
