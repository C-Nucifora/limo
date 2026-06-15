/*!
 * \file nexusbrowserdialog.h
 * \brief Header for the NexusBrowserDialog class.
 */

#pragma once

#include "src/core/nexus/api.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <vector>


namespace Ui
{
class NexusBrowserDialog;
}

/*!
 * \brief Dialog used to search and browse mods on NexusMods and route a chosen mod
 * into the existing download/import flow.
 *
 * The actual download is driven by ApplicationManager/MainWindow, so this dialog only
 * emits \ref installModRequested with the data needed by the existing import path. It
 * does not reimplement downloading.
 */
class NexusBrowserDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI.
   * \param parent Parent for this widget, passed to the QDialog constructor.
   */
  explicit NexusBrowserDialog(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~NexusBrowserDialog();

  /*!
   * \brief Prepares the dialog for the given application and NexusMods domain.
   * \param app_id ModdedApplication the browser is opened for.
   * \param domain_name NexusMods domain (game) to search in.
   */
  void setupDialog(int app_id, const QString& domain_name);

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::NexusBrowserDialog* ui;
  /*! \brief ModdedApplication this browser was opened for. */
  int app_id_ = -1;
  /*! \brief NexusMods domain to search in. */
  QString domain_name_;
  /*! \brief Current search results. */
  std::vector<nexus::SearchResult> results_;
  /*! \brief Watches the asynchronous search request. */
  QFutureWatcher<std::vector<nexus::SearchResult>> search_watcher_;
  /*! \brief Watches the asynchronous file lookup performed for installation. */
  QFutureWatcher<std::vector<nexus::File>> install_watcher_;
  /*! \brief Used to lazily load result thumbnails off the UI thread. */
  QNetworkAccessManager network_manager_;
  /*! \brief Maps the sort combo box index to a nexus::SortOrder. */
  nexus::SortOrder currentSortOrder() const;
  /*! \brief Returns the currently selected category id, or -1 for "any". */
  int currentCategoryId() const;
  /*! \brief Updates UI enabled state while a request is running. */
  void setBusy(bool busy);
  /*! \brief Mod id of the result currently queued for installation. */
  long pending_install_mod_id_ = -1;

private slots:
  /*! \brief Starts an asynchronous search using the current controls. */
  void onSearchClicked();
  /*! \brief Populates the results list once the search request finishes. */
  void onSearchFinished();
  /*! \brief Updates the detail pane when the selected result changes. */
  void onResultSelectionChanged();
  /*! \brief Looks up the files for the selected result and emits the install signal. */
  void onInstallClicked();
  /*! \brief Emits the install signal once the file lookup finishes. */
  void onInstallFilesFetched();

signals:
  /*!
   * \brief Requests installation of the chosen mod through the existing import path.
   *
   * The arguments mirror MainWindow::onModDownloadRequested so the dialog can be wired
   * into the existing download/import flow without reimplementing it.
   *
   * \param app_id ModdedApplication the mod should be installed for.
   * \param mod_id NexusMods mod id (also used as target group id by the import path).
   * \param file_id NexusMods file id to download.
   * \param mod_url URL of the mod page on NexusMods.
   * \param version Mod version used to overwrite the default version.
   */
  void installModRequested(int app_id, int mod_id, int file_id, QString mod_url, QString version);
};
