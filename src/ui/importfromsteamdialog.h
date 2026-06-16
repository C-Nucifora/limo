/*!
 * \file importfromsteamdialog.h
 * \brief Header for the ImportFromSteamDialog class.
 */

#pragma once

#include <QDialog>
#include <QList>
#include <QStringList>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>


namespace Ui
{
class ImportFromSteamDialog;
}

class ImportFromSteamDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI.
   * \param parent Parent for this widget, this is passed to the constructor of QDialog.
   */
  explicit ImportFromSteamDialog(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~ImportFromSteamDialog();
  /*! \brief Initializes the dialog. */
  void init();

private:
  /*! \brief Contains auto-generated ui elements. */
  Ui::ImportFromSteamDialog* ui;
  /*! \brief Name of the file containing all installed steam apps. */
  const std::string library_file_name_ = "libraryfolders.vdf";
  /*! \brief Indicates whether the dialog has been completed. */
  bool dialog_completed_ = false;

  /*!
   * \brief Checks if given path contains the library file.
   * \param path Path to be checked.
   * \return True if path contains a file named library_file_name, else False.
   */
  bool pathIsValid(std::filesystem::path path) const;
  /*!
   * \brief Updates ui->app_table with all apps listed in the library file.
   * \param steam_dir Path to steam installation.
   */
  void updateTable(std::filesystem::path steam_dir);
  /*!
   * \brief Adds a row to ui->app_table containing information about the app pertaining
   * the given app_id.
   * \param app_id Target app_id.
   * \param path Path to the apps library folder (the library folder that is expected to
   * contain the app). If the appmanifest is not found there, all paths in library_paths
   * are searched as well.
   * \param library_paths All known Steam library folder paths, used as a fallback when
   * the appmanifest is not located in the expected library folder.
   * \return True if a new row has been added.
   */
  bool addTableRow(std::string app_id,
                   std::filesystem::path path,
                   const std::vector<std::filesystem::path>& library_paths);
  /*!
   * \brief Locates the appmanifest_<app_id>.acf file for the given app by checking the
   * preferred library folder first, then falling back to every known library folder.
   * \param app_id Target app_id.
   * \param preferred_path Library folder that is expected to contain the app.
   * \param library_paths All known Steam library folder paths.
   * \return Path to the appmanifest file if one was found, else std::nullopt.
   */
  std::optional<std::filesystem::path> locateAppManifest(
    const std::string& app_id,
    const std::filesystem::path& preferred_path,
    const std::vector<std::filesystem::path>& library_paths) const;
  /*!
   * \brief Performs a tolerant lookup of a VDF key in a single line.
   *
   * The match is case-insensitive for the key and tolerant of surrounding whitespace
   * (tabs or spaces) between tokens.
   * \param line Line to be searched.
   * \param key Key to look for, e.g. "name" or "installdir".
   * \return The associated value if the key was found on this line, else std::nullopt.
   */
  std::optional<std::string> parseVdfValue(const std::string& line,
                                           const std::string& key) const;
  /*!
   * \brief Shows an error in a QMessageBox with given title and message.
   * \param title Title of the error box.
   * \param message Error message to be displayed.
   */
  void showError(QString title, QString message);
  /*!
   * \brief Hides/shows table rows according to the current search text and the
   * "Supported only" checkbox.
   */
  void applyFilters();
  /*!
   * \brief Computes the auto-detected Proton prefix drive_c for a table row, or an
   * empty string if the game has no prefix.
   * \param row Row index in ui->app_table.
   */
  QString autoPrefixForRow(int row) const;

private slots:
  /*! \brief Opens a file dialog to chose the steam path. */
  void on_pick_path_button_clicked();
  /*! \brief Closes the dialog and emits a signal for completion. */
  void on_buttonBox_accepted();
  /*! \brief Updates ui->app_table with information from the selected directory. */
  void on_path_field_editingFinished();
  /*!
   * \brief Called when the text in ui->search_field has been edited by the user.
   * \param new_text The newly entered text.
   */
  void on_search_field_textEdited(const QString& new_text);
  /*! \brief Re-applies the row filter when the "Supported only" checkbox is toggled. */
  void on_supported_only_checkbox_toggled(bool checked);
  /*! \brief Populates the prefix override field from the selected row's auto-detected prefix. */
  void on_app_table_itemSelectionChanged();
  /*! \brief Opens a file dialog to choose a custom Proton/WINE prefix drive_c directory. */
  void on_pick_prefix_button_clicked();
  /*! \brief Collects every supported (preset) game and requests a batch add. */
  void onAddAllSupportedClicked();

signals:
  /*!
   * \brief Signals completion of the dialog.
   * \param name Name of the imported application.
   * \param app_id Steam app_id of the imported application.
   * \param install_dir Name of the directory under steamapps which contains the
   * new applications files.
   * \param prefix_path Path to the applications Proton prefix, or empty if none exists.
   * \param icon_path Path to the applications icon.
   */
  void applicationImported(QString name,
                           QString app_id,
                           QString install_dir,
                           QString prefix_path,
                           QString icon_path);
  /*!
   * \brief Requests batch import of every supported game.
   * \param games List of [name, app_id, install_dir, prefix_path, icon_path] per game.
   */
  void addAllSupportedRequested(QList<QStringList> games);
};
