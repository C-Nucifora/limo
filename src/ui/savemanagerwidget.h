/*!
 * \file savemanagerwidget.h
 * \brief Header for the SaveManagerWidget class.
 *
 * Fork feature #24: save-game manager.
 */

#pragma once

#include "../core/savemanager.h"
#include <QWidget>
#include <vector>


namespace Ui
{
class SaveManagerWidget;
}


/*!
 * \brief A QWidget showing a table of save files for the current application.
 *
 * Provides a configurable, per-app persisted saves directory path, plus actions to refresh
 * the list, open the directory in the system file manager and delete save files (with
 * confirmation). The saves directory is supplied entirely through the UI and persisted via
 * QSettings; this widget does not depend on ModdedApplication internals.
 */
class SaveManagerWidget : public QWidget
{
  Q_OBJECT
public:
  /*!
   * \brief Initializes the UI.
   * \param parent Parent for this widget, this is passed to the constructor of QWidget.
   */
  explicit SaveManagerWidget(QWidget* parent = nullptr);
  /*! \brief Destructor. */
  ~SaveManagerWidget();

  /*!
   * \brief Sets the application context.
   *
   * Switches the widget to use the per-app persisted saves directory for the given app and
   * refreshes the table. An optional suggested directory is used as the default when no path
   * has been persisted for this app yet.
   * \param app_id The currently selected application's id.
   * \param suggested_dir A suggestion for the saves directory (e.g. the app's staging dir).
   * Used only when no path has been persisted for this app.
   */
  void setAppContext(int app_id, const QString& suggested_dir = "");

private:
  /*! \brief Column index of the save name. */
  static constexpr int COL_NAME = 0;
  /*! \brief Column index of the last modification date. */
  static constexpr int COL_DATE = 1;
  /*! \brief Column index of the file size. */
  static constexpr int COL_SIZE = 2;

  /*! \brief Contains all auto-generated UI elements. */
  Ui::SaveManagerWidget* ui;
  /*! \brief Id of the application whose saves are currently shown. -1 if none. */
  int app_id_ = -1;
  /*! \brief Save files currently shown in the table, in table row order. */
  std::vector<SaveManager::SaveFile> saves_;

  /*! \brief Returns the QSettings key under which the saves dir for the current app is stored. */
  QString settingsKey() const;
  /*! \brief Loads the persisted saves dir for the current app into the path field. */
  void loadPersistedPath(const QString& suggested_dir);
  /*! \brief Persists the current saves dir for the current app. */
  void persistPath();
  /*! \brief Re-enumerates the saves dir and fills the table. */
  void refresh();
  /*! \brief Formats a byte count into a human readable string. */
  static QString formatSize(std::uintmax_t bytes);

private slots:
  /*! \brief Opens a directory picker and updates the saves dir. */
  void onPathPickerClicked();
  /*! \brief Handles manual edits to the saves dir field. */
  void onPathEdited();
  /*! \brief Refreshes the save list. */
  void onRefreshClicked();
  /*! \brief Opens the saves dir in the system file manager. */
  void onOpenClicked();
  /*! \brief Deletes the selected save files after confirmation. */
  void onDeleteClicked();
};
