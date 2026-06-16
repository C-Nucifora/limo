/*!
 * \file addappdialog.h
 * \brief Header for the AddAppDialog class.
 */

#pragma once

#include "../core/editapplicationinfo.h"
#include "../core/editdeployerinfo.h"
#include "importfromsteamdialog.h"
#include <QDialog>
#include <json/json.h>
#include <filesystem>
#include <vector>


namespace Ui
{
class AddAppDialog;
}

/*!
 * \brief Dialog for creating and editing \ref ModdedApplication "applications".
 */
class AddAppDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI.
   * \param parent Parent for this widget, this is passed to the constructor of QDialog.
   */
  explicit AddAppDialog(bool is_flatpak, QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~AddAppDialog();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::AddAppDialog* ui;

  /*! \brief Name of the key used to identify deployers in the apps config file. */
  constexpr static inline std::string JSON_DEPLOYERS_GROUP = "deployers";
  /*! \brief Name of the key used to identify deployer type in the apps config file. */
  constexpr static inline std::string JSON_DEPLOYERS_TYPE = "type";
  /*! \brief Name of the key used to identify deployer name in the apps config file. */
  constexpr static inline std::string JSON_DEPLOYERS_NAME = "name";
  /*! \brief Name of the key used to identify deployer target dir in the apps config file. */
  constexpr static inline std::string JSON_DEPLOYERS_TARGET = "target_dir";
  /*! \brief Name of the key used to identify deployer mode in the apps config file. */
  constexpr static inline std::string JSON_DEPLOYERS_MODE = "deploy_mode";
  /*! \brief Name of the key used to identify deployer source dir in the apps config file. */
  constexpr static inline std::string JSON_DEPLOYERS_SOURCE = "source_dir";
  /*! \brief
   *  Name of the key used to determine whether a reverse deployer
   *  uses separate dirctories for profiles.
   */
  constexpr static inline char JSON_DEPLOYERS_SEPARATE_DIRS[] = "uses_separate_dirs";
  /*! \brief
   *  Name of the key used to determine whether a reverse deployer
   *  should update the ignore list upon creation.
   */
  constexpr static inline char JSON_DEPLOYERS_UPDATE_IGNORE_LIST[] = "update_ignore_list";
  /*! \brief Contains all mandatory valid keys used in a deployer group in the apps config file. */
  constexpr static std::array<std::string, 4> JSON_DEPLOYER_MANDATORY_KEYS{ JSON_DEPLOYERS_TYPE,
                                                                            JSON_DEPLOYERS_NAME,
                                                                            JSON_DEPLOYERS_TARGET,
                                                                            JSON_DEPLOYERS_MODE };
  /*! \brief Name of the key used to identify auto tags in the apps config file. */
  constexpr static inline std::string JSON_AUTO_TAGS_GROUP = "auto_tags";
  /*! \brief Name of the key used to identify the apps name in the apps config file. */
  constexpr static inline std::string JSON_NAME = "name";
  /*! \brief Name of the key used to group the deploy hooks in the apps config file. */
  constexpr static inline std::string JSON_HOOKS_GROUP = "hooks";
  /*! \brief Name of the key used to identify the pre-deploy hook in the apps config file. */
  constexpr static inline std::string JSON_HOOK_PRE_DEPLOY = "pre_deploy";
  /*! \brief Name of the key used to identify the post-deploy hook in the apps config file. */
  constexpr static inline std::string JSON_HOOK_POST_DEPLOY = "post_deploy";
  /*! \brief Name of the key used to identify the pre-undeploy hook in the apps config file. */
  constexpr static inline std::string JSON_HOOK_PRE_UNDEPLOY = "pre_undeploy";
  /*! \brief Name of the key used to identify the post-undeploy hook in the apps config file. */
  constexpr static inline std::string JSON_HOOK_POST_UNDEPLOY = "post_undeploy";

  /*! \brief If true: Dialog is used to edit, else: Dialog is used to create. */
  bool edit_mode_ = false;
  /*! \brief Current name of the edited \ref ModdedApplication "application". */
  QString name_;
  /*! \brief Current staging directory path of the edited \ref ModdedApplication "application". */
  QString path_;
  /*! \brief Current command to run the edited \ref ModdedApplication "application". */
  QString command_;
  /*! \brief Id of the edited \ref ModdedApplication "application". */
  int app_id_;
  /*! \brief Steam app id of the new application, or -1 if not a steam app. */
  long steam_app_id_;
  /*! \brief Path to imported steam applications installation directory. */
  QString steam_install_path_ = "";
  /*! \brief Path to imported steam applications prefix directory. */
  QString steam_prefix_path_ = "";
  /*! \brief Indicates whether the dialog has been completed. */
  bool dialog_completed_ = false;
  /*! \brief Contains deployers which will be created upon adding a new application. */
  std::vector<EditDeployerInfo> deployers_;
  /*! \brief Contains Json objects representing imported auto tags. */
  std::vector<Json::Value> auto_tags_;
  /*! \brief Whether or not this application is running as a flatpak. */
  bool is_flatpak_;
  /*! \brief Reusable dialog for importing data from installed Steam apps. */
  std::unique_ptr<ImportFromSteamDialog> import_from_steam_dialog_;
  /*!
   * \brief Maps display names shown in the GOG template combo box to their config file paths.
   * Populated by populateGogTemplateCombo(). (issue #74 / limo-app/limo#51)
   */
  QStringList gog_template_paths_;

  /*!
   * \brief Set the enabled state of this dialogs OK button.
   * \param state
   */
  void enableOkButton(bool state);
  /*! \brief Checks whether the currently entered path exists. */
  bool pathIsValid();
  /*!
   * \brief fork #78: Whether the given path is a valid staging dir: it already exists, or
   * (in add mode) does not exist but is safely creatable — its immediate parent exists or
   * exactly one parent level is missing ("single missing parent"). A mistyped deep path is
   * rejected. Pure predicate with no side effects; the directory is created on accept.
   */
  bool pathExistsOrCreatable(const QString& text) const; // fork #78
  /*!
   * \brief fork #78: In add mode, creates the staging directory if it does not yet exist and
   * is safely creatable (see \ref pathExistsOrCreatable). Never touches an existing directory
   * and is a no-op in edit mode.
   * \return true if the directory exists or was created, false on failure.
   */
  bool createStagingDirIfNeeded(); // fork #78
  /*!
   * \brief Checks whether the currently entered icon path refers to a valid icon file.
   * \param Path to an icon. If this checked instead of ui->icon_field if this is not empty.
   */
  bool iconIsValid(const QString& path = "");
  /*!
   * \brief Security guard for deployer target directories coming from (community) game
   * configs: a resolved target path may only be created on disk if it is lexically
   * contained within one of the known Steam install/prefix roots and contains no ".."
   * components. This prevents an untrusted config from causing directory creation at
   * arbitrary filesystem locations.
   * \param target_dir Resolved (placeholder-expanded) target directory.
   * \return true if the path is safe to create, false otherwise.
   */
  bool targetDirIsSafe(const std::filesystem::path& target_dir) const;
  /*!
   * \brief Initializes default settings for deployers and auto tags from a file named "app_id_.json".
   * If no such file exists, creates generic deployers targeting installation directory and prefix.
   */
  void initConfigForApp();
  /*! \brief
   *  Initializes deployers targeting the currently selected steam app's installation and,
   *  if present, it's prefix directory.
   */
  void initDefaultAppConfig();
  // fork #204: Returns the ordered list of directories to search for a "<id>.json" game
  // definition: the user-writable config dir first (so user defs override bundled), then
  // the bundled steam_app_configs dir. Missing dirs are still returned; callers check
  // existence. The user dir is created lazily here.
  std::vector<std::filesystem::path> gameConfigSearchDirs();
  /*!
   * \brief Populates the GOG template combo box with names from all bundled steam_app_configs.
   * Also fills gog_template_paths_ with the corresponding file paths. (issue #74)
   */
  void populateGogTemplateCombo();
  /*!
   * \brief Applies the selected GOG game template. Uses the staging directory as
   * $STEAM_INSTALL_PATH$ and the optional prefix field as $STEAM_PREFIX_PATH$.
   * Deployers whose resolved paths do not exist are logged and skipped rather than
   * aborting the whole import. (issue #74 / limo-app/limo#51)
   * \param install_path Path to the game's installation directory.
   * \param prefix_path  Optional prefix path; may be empty.
   * \param config_path  Path to the JSON config file.
   */
  void initConfigForGog(const QString& install_path,
                        const QString& prefix_path,
                        const QString& config_path);
  /*!
   * \brief Updates the read-only display showing the detected game install path.
   * Shows the resolved Steam install path, or a neutral placeholder if none was detected.
   */
  void updateDetectedPath();
  /*!
   * \brief Shows or hides the advanced setup fields (version, icon, launch command,
   * detected-path display and deploy hooks). The simple, guided fields (name, staging
   * directory, game template and Steam import) always stay visible. (issue #92)
   * \param advanced Whether advanced fields should be shown.
   */
  void setAdvancedMode(bool advanced);
  /*!
   * \brief Loads the four deploy hook commands from the given app's config file
   * into the hook line edits. Clears the fields if the file or hooks are absent.
   * \param staging_dir Staging directory of the app whose config should be read.
   */
  void loadHooksFromConfig(const QString& staging_dir);
  /*!
   * \brief Merges the hook commands entered in the dialog into the given app's
   * config file under the "hooks" key, leaving all other settings untouched.
   * \param staging_dir Staging directory of the app whose config should be updated.
   */
  void saveHooksToConfig(const QString& staging_dir);

public:
  /*!
   * \brief Initializes this dialog to allow editing of an existing
   * \ref ModdedApplication "application".
   * \param name Current name of the edited \ref ModdedApplication "application".
   * \param app_version Current app app_version.
   * \param path Current staging directory path of the edited
   * \ref ModdedApplication "application".
   * \param command Current command to run the edited \ref ModdedApplication "application".
   * \param app_id Id of the edited \ref ModdedApplication "application".
   * \param steam_app_id Steam app id. Or -1 if not a Steam app.
   */
  void setEditMode(const QString& name,
                   const QString& app_version,
                   const QString& path,
                   const QString& command,
                   const QString& icon_path,
                   int app_id,
                   long steam_app_id);
  /*!
   *  \brief Initializes this dialog to allow creating a new
   *  \ref ModdedApplication "application".
   */
  void setAddMode();

private slots:
  /*! \brief Shows a file dialog for the staging directory path. */
  void on_file_picker_button_clicked();
  /*! \brief Only enable the OK button if a name has been entered. */
  void on_name_field_textChanged(const QString& text);
  /*! \brief Only enable the OK button if a valid staging directory path has been entered. */
  void on_path_field_textChanged(const QString& text);
  /*! \brief Closes the dialog and emits a signal for completion. */
  void on_buttonBox_accepted();
  /*! \brief Opens a dialog to import currently installed steam app. */
  void on_import_button_clicked();
  /*!
   * \brief Called when the import steam application dialog has been completed.
   * \param name Name of the imported application.
   * \param app_id Steam app_id of the imported application.
   * \param install_dir Name of the directory under steamapps which contains the
   * new applications files.
   * \param prefix_path Path to the applications Proton prefix, or empty if none exists.
   * \param icon_path Path to the applications icon.
   */
  void onApplicationImported(QString name,
                             QString app_id,
                             QString install_dir,
                             QString prefix_path,
                             QString icon_path);
  /*!
   * \brief Updates the staging directory path to given path.
   * \param path The new path.
   */
  void onFileDialogAccepted(const QString& path);
  /*! \brief Called when icon path picker button is clicked. */
  void on_icon_picker_button_clicked();
  /*!
   * \brief Updates the icon path to the given path if the given path refers to a valid icon.
   * \param path The new path.
   */
  void onIconPathDialogComplete(const QString& path);
  /*! \brief Applies the currently selected GOG game template. (issue #74 / limo-app/limo#51) */
  void on_gog_apply_button_clicked();
  /*! \brief Shows a file dialog for the GOG prefix directory. */
  void on_gog_prefix_picker_button_clicked();
  /*!
   * \brief Updates the GOG prefix path field with the selected directory.
   * \param path The chosen directory path.
   */
  void onGogPrefixDialogAccepted(const QString& path);
  /*!
   * \brief Toggles the advanced setup fields when the "Advanced setup" checkbox changes.
   * \param state Qt::Checked when advanced fields should be shown. (issue #92)
   */
  void on_advanced_checkbox_stateChanged(int state);

signals:
  /*!
   * \brief Signals completion of the dialog in add mode.
   * \param info Contains all data entered in the dialog.
   */
  void applicationAdded(EditApplicationInfo edit_app_info);
  /*!
   * \brief Signals completion of the dialog in edit mode.
   * \param info Contains all data entered in the dialog.
   * \param app_id Id of the edited \ref ModdedApplication "application".
   */
  void applicationEdited(EditApplicationInfo edit_app_info, int app_id);
};
