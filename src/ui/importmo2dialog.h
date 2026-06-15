/*!
 * \file importmo2dialog.h
 * \brief Header for the ImportMo2Dialog class.
 *
 * Implements the UI entry point for fork issue #45 / limo-app/limo#92:
 * import a Mod Organizer 2 instance into Limo.
 */

#pragma once

#include "../core/editapplicationinfo.h"
#include "../core/editdeployerinfo.h"
#include "../core/importers/mo2importer.h"
#include <QDialog>
#include <filesystem>


namespace Ui
{
class ImportMo2Dialog;
}

/*!
 * \brief Dialog that lets the user pick a Mod Organizer 2 instance path,
 * select a profile, and trigger the import into Limo.
 *
 * On acceptance the dialog emits \ref importAccepted with an
 * \ref EditApplicationInfo pre-filled with:
 *   - one Simple Deployer (target directory chosen by the user),
 *   - the staging directory set to the MO2 mods/ folder so that no files
 *     need to be moved.
 *
 * The caller (MainWindow) is responsible for calling addApplication() and,
 * after the application has been created, calling installMod() for each
 * entry in the provided \ref Mo2ParseResult to register the mods in the
 * correct load-order with the correct enabled state.
 */
class ImportMo2Dialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI.
   * \param parent Parent widget.
   */
  explicit ImportMo2Dialog(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~ImportMo2Dialog();

  /*! \brief Resets dialog state so it can be shown again cleanly. */
  void init();

private:
  /*! \brief Auto-generated UI elements. */
  Ui::ImportMo2Dialog* ui;

  /*!
   * \brief Updates the profile combo-box from the currently entered path.
   * \param instance_path Path to the MO2 instance.
   */
  void updateProfileList(const std::filesystem::path& instance_path);

  /*!
   * \brief Displays an error message box with the given title and text.
   * \param title Title for the error box.
   * \param message Message to be shown.
   */
  void showError(const QString& title, const QString& message) const;

private slots:
  /*! \brief Opens a directory picker to select the MO2 instance path. */
  void on_pick_path_button_clicked();
  /*! \brief Opens a directory picker to select the Limo deploy target. */
  void on_pick_target_button_clicked();
  /*! \brief Refreshes the profile list when the path field is edited. */
  void on_path_field_editingFinished();
  /*! \brief Validates input and emits importAccepted on success. */
  void on_buttonBox_accepted();

signals:
  /*!
   * \brief Emitted when the user confirms the import.
   *
   * \param app_info    Pre-filled application info (name, staging_dir, deployers).
   * \param parse_result Parsed MO2 mods in load-order, ready for registration.
   */
  void importAccepted(EditApplicationInfo app_info, Mo2ParseResult parse_result);
};
