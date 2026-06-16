/*!
 * \file wabbajackimportdialog.h
 * \brief Header for the WabbajackImportDialog class.
 *
 * fork #197: Lets the user open a ".wabbajack" modlist, inspect its metadata and
 * source archives, and queue the Nexus-hosted mods for download. The dialog only
 * emits download requests; the actual download is handled by the main window.
 */

#pragma once

#include "core/wabbajackmodlist.h"
#include <QDialog>


namespace Ui
{
class WabbajackImportDialog;
}

/*!
 * \brief Dialog used to import a Wabbajack ".wabbajack" modlist.
 *
 * fork #197
 */
class WabbajackImportDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI.
   * \param parent Parent widget.
   */
  explicit WabbajackImportDialog(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~WabbajackImportDialog();

signals:
  /*!
   * \brief Emitted once per Nexus archive when the user queues the downloads.
   *
   * fork #197: The main window connects this to the existing queued Nexus
   * download/import flow.
   * \param game_name NexusMods game domain name.
   * \param mod_id NexusMods mod id.
   * \param file_id NexusMods file id.
   * \param version Mod version, if known.
   */
  void requestNexusDownload(QString game_name, int mod_id, int file_id, QString version);

private slots:
  /*! \brief Opens a file dialog and parses the selected ".wabbajack" file. */
  void on_open_button_clicked();
  /*! \brief Emits requestNexusDownload for every parsed Nexus archive. */
  void on_queue_button_clicked();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::WabbajackImportDialog* ui;
  /*! \brief The currently loaded modlist. */
  WabbajackModlist modlist_;

  /*! \brief Updates the info label and archive table from modlist_. */
  void updateView();
  /*! \brief Returns the number of Nexus archives in modlist_. */
  int nexusArchiveCount() const;
};
