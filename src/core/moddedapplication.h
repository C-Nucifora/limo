/*!
 * \file moddedapplication.h
 * \brief Header for the ModdedApplication class.
 */

#pragma once

#include "appinfo.h"
#include "autotag.h"
#include "backupmanager.h"
#include "deployer.h"
#include "deployerinfo.h"
#include "editautotagaction.h"
#include "editdeployerinfo.h"
#include "editmanualtagaction.h"
#include "editprofileinfo.h"
#include "externalchangesinfo.h"
#include "log.h"
#include "manualtag.h"
#include "modinfo.h"
#include "modrule.h"
#include "nexus/api.h"
#include "tool.h"
#include <filesystem>
#include <json/json.h>
#include <set>
#include <string>
#include <vector>


/*!
 * \brief Contains all mods and Deployer objects used for one target application.
 * Stores internal state in a JSON file.
 */
class ModdedApplication
{
public:
  /*!
   * \brief If a JSON settings file already exists in app_mod_dir, it is
   * used to construct this object.
   * \param staging_dir Path to staging directory where all installed mods are stored.
   * \param name Name of target application.
   * \param command Command used to run target application.
   * \param icon_path Path to an icon for this application.
   * \throws Json::LogicError Indicates a logic error, e.g. trying to convert "123" to a bool,
   * while parsing.
   * \throws Json::RuntimeError Indicates a syntax error in the JSON file.
   * \throws ParseError Indicates a semantic error while parsing the JSON file, e.g.
   * the active member of a group is not part of that group.
   */
  ModdedApplication(std::filesystem::path staging_dir,
                    std::string name = "",
                    std::string command = "",
                    std::filesystem::path icon_path = "",
                    std::string app_version = "");

  /*! \brief Name of the file used to store this objects internal state. */
  inline static const std::string CONFIG_FILE_NAME = "lmm_mods.json";

  /*! \brief Deploys mods using all Deployer objects of this application. */
  void deployMods();
  /*!
   * \brief Deploys mods using Deployer objects with given ids.
   * \param deployers The Deployer ids used for deployment.
   */
  void deployModsFor(std::vector<int> deployers);
  /*! \brief Undeploys mods for all managed deployers. */
  void unDeployMods();
  /*!
   * \brief Undeploys mods for the given deployers.
   * \param deployers Target deployers.
   */
  void unDeployModsFor(std::vector<int> deployers);
  /*!
   * \brief Installs a new mod using the given Installer type.
   * \param info Contains all data needed to install the mod.
   */
  void installMod(const ImportModInfo& info);
  /*!
   * \brief Uninstalls the given mods, this includes deleting all installed files.
   * \param mod_id Ids of the mods to be uninstalled.
   * \param installer_type The Installer type used. If an empty string is given, the Installer
   * used during installation is used.
   */
  void uninstallMods(const std::vector<int>& mod_ids, const std::string& installer_type = "");
  void commitChanges();
  /*!
   * \brief Appends a new mod to the load order for given Deployer.
   * \param deployer The target Deployer
   * \param mod_id Id of the mod to be added.
   * \param update_conflicts Updates the target deployers conflict groups only if this is true.
   * \param progress_node Used to inform about the current progress.
   */
  void addModToDeployer(int deployer,
                        int mod_id,
                        bool update_conflicts = true,
                        std::optional<ProgressNode*> progress_node = {});
  /*!
   * \brief Removes a mod from the load order for given Deployer.
   * \param deployer The target Deployer
   * \param mod_id Id of the mod to be removed.
   * \param update_conflicts Updates the target deployers conflict groups only if this is true.
   * \param progress_node Used to inform about the current progress.
   */
  void removeNodeFromDeployer(int deployer,
                             void *node_ptr,
                             bool update_conflicts = true,
                             std::optional<ProgressNode*> progress_node = {});
  void removeModFromDeployer(int deployer,
                             int mod_id,
                             bool update_conflicts = true,
                             std::optional<ProgressNode*> progress_node = {});
  /*!
   * \brief Enables or disables the given mod in the load order for given Deployer.
   * \param deployer The target Deployer
   * \param mod_id Mod to be edited.
   * \param status The new status.
   */
  void setModStatus(int deployer, int mod_id, bool status);
  /*!
   * \brief Adds a new Deployer of given type.
   * \param info Contains all data needed to create a deployer, e.g. its name.
   */
  void addDeployer(const EditDeployerInfo& info);
  /*!
   * \brief Removes a Deployer.
   * \param deployer The Deployer.
   * \param cleanup If true: Remove all currently deployed files and restore backups.
   */
  void removeDeployer(int deployer, bool cleanup);
  /*!
   * \brief Creates a vector containing the names of all Deployer objects.
   * \return The vector.
   */
  std::vector<std::string> getDeployerNames() const;
  /*!
   * \brief Creates a vector containing information about all installed mods, stored in ModInfo
   * objects.
   * \return The vector.
   */
  std::vector<ModInfo> getModInfo() const;
  /*!
   * \brief Getter for the current mod load order of one Deployer.
   * \param deployer The target Deployer.
   * \return The load order.
   */
  std::shared_ptr<TreeItem<DeployerEntry>> getLoadorder(int deployer) const;
  /*!
   * \brief Getter for the path to the staging directory. This is where all installed
   * mods are stored.
   * \return The path.
   */
  const std::filesystem::path& getStagingDir() const;
  /*!
   * \brief Setter for the path to the staging directory. This is where all installed
   * mods are stored.
   * \param staging_dir The new staging directory path.
   * \param move_existing If true: Move all installed mods to the new directory.
   * \throws Json::LogicError Indicates a logic error, e.g. trying to convert "123" to a bool,
   * while parsing.
   * \throws Json::RuntimeError Indicates a syntax error in the JSON file.
   * \throws ParseError Indicates a semantic error while parsing the JSON file, e.g.
   * the active member of a group is not part of that group.
   */
  void setStagingDir(std::string staging_dir, bool move_existing);
  /*!
   * \brief Getter for the name of this application.
   * \return The name.
   */
  const std::string& name() const;
  /*!
   * \brief Setter for the name of this application.
   * \param newName The new name.
   */
  void setName(const std::string& newName);
  /*!
   * \brief Returns the number of Deployer objects for this application.
   * \return The number of Deployers.
   */
  int getNumDeployers() const;
  /*!
   * \brief Getter for the name of the file used to store this objects internal state.
   * \return The name.
   */
  const std::string& getConfigFileName() const;
  /*!
   * \brief Changes the name of an installed mod.
   * \param mod_id Id of the target mod.
   * \param new_name The new name.
   */
  void changeModName(int mod_id, const std::string& new_name);
  /*!
   * \brief Exports the staged files of an installed mod into a single zip archive.
   *
   * Walks the mod's staging directory and writes every regular file into the target
   * archive using paths relative to the staging directory. Uses libarchive's write API.
   * \param mod_id Id of the mod to be exported.
   * \param target_archive Path of the zip archive to be written.
   * \throw std::runtime_error If the mod id is unknown, the staging directory is missing
   * or any libarchive operation fails.
   */
  void exportModArchive(int mod_id, const std::filesystem::path& target_archive) const;
  /*!
   * \brief Checks for file conflicts of given mod with all other mods in the load order for
   * one Deployer.
   * \param deployer The target Deployer
   * \param mod_id Mod to be checked.
   * \param show_disabled If true: Also check for conflicts with disabled mods.
   * \return A vector with information about conflicts with every other mod.
   */
  std::vector<ConflictInfo> getFileConflicts(int deployer, int mod_id, bool show_disabled) const;
  /*!
   * \brief Fills an AppInfo object with information about this object.
   * \return The AppInfo object.
   */
  AppInfo getAppInfo() const;
  /*!
   * \brief Adds a new tool to this application.
   * \param tool The new tool.
   */
  void addTool(const Tool& tool);
  /*!
   * \brief Removes a tool.
   * \param tool_id The tool's id.
   */
  void removeTool(int tool_id);
  /*!
   * \brief Getter for the tools of this application.
   * \return A vector of tools.
   */
  std::vector<Tool> getTools() const;
  /*!
   * \brief Getter for the command used to run this application.
   * \return The command.
   */
  const std::string& command() const;
  /*!
   * \brief Setter for the command used to run this application.
   * \param newCommand The new command.
   */
  void setCommand(const std::string& newCommand);
  /*!
   * \brief Used to set type, name and target directory for one deployer.
   * \param deployer Target Deployer.
   * \param info Contains all data needed to edit a deployer, e.g. its new name.
   */
  void editDeployer(int deployer, const EditDeployerInfo& info);
  /*!
   * \brief Checks for conflicts with other mods for one Deployer.
   * Two mods are conflicting if they share at least one file.
   * \param deployer Target Deployer.
   * \param mod_id The mod to be checked.
   * \return A set of mod ids which conflict with the given mod.
   */
  std::unordered_set<int> getModConflicts(int deployer, int mod_id);
  /*!
   * \brief Sets the currently active profile.
   * \param profile The new profile.
   */
  void setProfile(int profile);
  /*!
   * \brief Adds a new profile and optionally copies it's load order from an existing profile.
   * \param info Contains the data for the new profile.
   */
  void addProfile(const EditProfileInfo& info);
  /*!
   * \brief Removes a profile.
   * \param profile The profile to be removed.
   */
  void removeProfile(int profile);
  /*!
   * \brief Returns a vector containing the names of all profiles.
   * \return The vector.
   */
  std::vector<std::string> getProfileNames() const;
  /*!
   * \brief Used to set the name of a profile.
   * \param profile Target Profile
   * \param info Contains the new profile data.
   */
  void editProfile(int profile, const EditProfileInfo& info);
  /*!
   * \brief Used to replace an existing tool with a new tool.
   * \param tool_id Target tool to be replaced.
   * \param new_tool The new tool.
   */
  void editTool(int tool_id, const Tool& new_tool);
  /*!
   * \brief Checks if files can be deployed.
   * \return A tuple containing:
   * A return code: 0: No error, 1: Error while writing to a deployer's source directory,
   *  2: Error while creating a hard link, 3: Error while writing to a deployer's target directory.
   * The deployer's target path (if an error occured, else "").
   * A more detailed error message (if an error occured, else "").
   */
  std::tuple<int, std::string, std::string> verifyDeployerDirectories();
  /*!
   * \brief Adds a mod to an existing group and makes the mod the active member of that group.
   * \param mod_id The mod's id.
   * \param group The target group.
   * \param progress_node Used to inform about the current progress.
   */
  void addModToGroup(int mod_id, int group, std::optional<ProgressNode*> progress_node = {});
  /*!
   * \brief Removes a mod from it's group.
   * \param mod_id Target mod.
   * \param update_conflicts If true: Update relevant conflict groups.
   * \param progress_node Used to inform about the current progress.
   */
  void removeModFromGroup(int mod_id,
                          bool update_conflicts = true,
                          std::optional<ProgressNode*> progress_node = {});
  /*!
   * \brief Creates a new group containing the two given mods. A group is a set of mods
   * where only one member, the active member, will be deployed.
   * \param first_mod_id First mod. This will be the active member of the new group.
   * \param second_mod_id Second mod.
   * \param progress_node Used to inform about the current progress.
   */
  void createGroup(int first_mod_id,
                   int second_mod_id,
                   std::optional<ProgressNode*> progress_node = {});
  /*!
   * \brief Changes the active member of given group to given mod.
   * \param group Target group.
   * \param mod_id The new active member.
   * \param progress_node Used to inform about the current progress.
   */
  void changeActiveGroupMember(int group,
                               int mod_id,
                               std::optional<ProgressNode*> progress_node = {});
  /*!
   * \brief Sets the given mod's version to the given new version.
   * \param mod_id Target mod.
   * \param new_version The new version.
   */
  void changeModVersion(int mod_id, const std::string& new_version);
  /*!
   * \brief Returns the number of groups.
   * \return The number of groups.
   */
  int getNumGroups();
  /*!
   * \brief Returns the user-visible name for the given group.
   * \param group Target group index.
   * \return The name, or an empty string if out of range.
   */
  std::string getGroupName(int group) const;
  /*!
   * \brief Sets the user-visible name for the given group and persists the change.
   * \param group Target group index.
   * \param name The new name.
   */
  void setGroupName(int group, const std::string& name);
  /*!
   * \brief Returns the notes for the given group.
   * \param group Target group index.
   * \return The notes, or an empty string if out of range.
   */
  std::string getGroupNotes(int group) const;
  /*!
   * \brief Sets the notes for the given group and persists the change.
   * \param group Target group index.
   * \param notes The new notes.
   */
  void setGroupNotes(int group, const std::string& notes);
  /*!
   * \brief Returns names and notes for every group, indexed parallel to groups_.
   * Each element is a pair of (name, notes).
   * \return The vector of (name, notes) pairs.
   */
  std::vector<std::pair<std::string, std::string>> getGroupMetadata() const;
  /*!
   * \brief Returns the members (mod ids) of the given group.
   * \param group Target group index.
   * \return The mod id vector, or empty if out of range.
   */
  std::vector<int> getGroupMembers(int group) const;
  /*!
   * \brief Returns the active member mod id of the given group.
   * \param group Target group index.
   * \return The active member mod id, or -1 if out of range.
   */
  int getActiveGroupMember(int group) const;
  /*!
   * \brief Checks if given mod belongs to any group.
   * \param mod_id Target mod.
   * \return True if mod belongs to a group, else: False.
   */
  bool modHasGroup(int mod_id);
  /*!
   * \brief Returns the group to which the given mod belongs.
   * \param mod_id Target mod.
   * \return The group, or -1 if the mod has no group.
   */
  int getModGroup(int mod_id);
  /*!
   * \brief Sorts the load order by grouping mods which contain conflicting files.
   * \param deployer Deployer for which the currently active load order is to be sorted.
   */
  void sortModsByConflicts(int deployer);
  /*!
   * \brief Returns the conflicts groups for the current profile of given deployer.
   * \param deployer Target Deployer.
   * \return The conflict info.
   */
  std::vector<std::vector<int>> getConflictGroups(int deployer);
  /*!
   * \brief Updates which \ref Deployer "deployer" should manage given mods.
   * \param mod_id Vector of mod ids to be added.
   * \param deployers Bool for every deployer, indicating if the mods should be managed
   * by that deployer.
   */
  void updateModDeployers(const std::vector<int>& mod_ids, const std::vector<bool>& deployers);
  /*! \brief Getter for icon_path_. */
  std::filesystem::path iconPath() const;
  /*!
   * \brief Setter for icon_path_.
   * \param icon_path The new icon path
   */
  void setIconPath(const std::filesystem::path& icon_path);

  /*!
   * \brief Verifies if reading/ writing to the staging directory is possible and if the
   * JSON file containing information about installed mods can be parsed.
   * \param staging_dir Path to the staging directory.
   * \return A code indicating success(0), an IO error(1) or an error during JSON parsing(2).
   */
  static int verifyStagingDir(std::filesystem::path staging_dir);
  /*!
   * \brief Creates DeployerInfo for one Deployer.
   * \param deployer Target deployer.
   */
  DeployerInfo getDeployerInfo(int deployer);
  /*! \brief Setter for log callback. */
  void setLog(const std::function<void(Log::LogLevel, const std::string&)>& newLog);
  /*! \brief Getter for the pre-deploy hook command. */
  std::string getPreDeployHook() const;
  /*! \brief Getter for the post-deploy hook command. */
  std::string getPostDeployHook() const;
  /*! \brief Getter for the pre-undeploy hook command. */
  std::string getPreUnDeployHook() const;
  /*! \brief Getter for the post-undeploy hook command. */
  std::string getPostUnDeployHook() const;
  /*!
   * \brief Sets the shell commands run automatically around (un-)deployment.
   * Each command is treated as a complete, user-authored command line (like a
   * Tool command overwrite) and is run via the existing safe runner without
   * additional shell-wrapping beyond what the user typed. Empty strings are no-ops.
   * \param pre_deploy Command run before deployment.
   * \param post_deploy Command run after deployment.
   * \param pre_undeploy Command run before undeployment.
   * \param post_undeploy Command run after undeployment.
   */
  void setDeployHooks(const std::string& pre_deploy,
                      const std::string& post_deploy,
                      const std::string& pre_undeploy,
                      const std::string& post_undeploy);
  /*!
   * \brief Adds a new target file or directory to be managed by the BackupManager.
   * \param path Path to the target file or directory.
   * \param name Display name for this target.
   * \param backup_names Display names for initial backups. Must contain at least one.
   */
  void addBackupTarget(const std::filesystem::path& path,
                       const std::string& name,
                       const std::vector<std::string>& backup_names);
  /*!
   * \brief Removes the given backup target by deleting all backups, except for the active one,
   * and all config files.
   * \param target_id Target to remove.
   */
  void removeBackupTarget(int target_id);
  /*!
   * \brief Removes all targets by deleting all backups, except for the active ones,
   * and all config files.
   */
  void removeAllBackupTargets();
  /*!
   * \brief Adds a new backup for the given target by copying the currently active backup.
   * \param target_id Target for which to create a new backup.
   * \param name Display name for the new backup.
   * \param source Backup from which to copy files to create the new backup. If -1:
   * copy currently active backup.
   */
  void addBackup(int target_id, const std::string& name, int source);
  /*!
   * \brief Deletes the given backup for given target.
   * \param target_id Target from which to delete a backup.
   * \param backup_id Backup to remove.
   */
  void removeBackup(int target_id, int backup_id);
  /*!
   * \brief Changes the currently active backup for the given target.
   * \param target_id Target for which to change the active backup.
   * \param backup_id New active backup.
   */
  void setActiveBackup(int target_id, int backup_id);
  /*!
   * \brief Returns a vector containing information about all managed backup targets.
   * \return The vector.
   */
  std::vector<BackupTarget> getBackupTargets() const;
  /*!
   * \brief Changes the name of the given backup for the given target
   * \param target_id Backup target.
   * \param backup_id Backup to be edited.
   * \param name The new name.
   */
  void setBackupName(int target_id, int backup_id, const std::string& name);
  /*!
   * \brief Changes the name of the given backup target
   * \param target_id Backup target.
   * \param name The new name.
   */
  void setBackupTargetName(int target_id, const std::string& name);
  /*!
   * \brief Deletes all files in the dest backup and replaces them with the files
   * from the source backup.
   * \param target_id Backup target.
   * \param source_backup Backup from which to copy files.
   * \param dest_backup Target for data deletion.
   */
  void overwriteBackup(int target_id, int source_backup, int dest_backup);
  /*! \brief Performs a cleanup for the previous installation. */
  void cleanupFailedInstallation();
  /*!
   * \brief Sets the callback function used to inform about the current task's progress.
   * \param progress_callback The function.
   */
  void setProgressCallback(const std::function<void(float)>& progress_callback);
  /*!
   * \brief Uninstalls all mods which are inactive group members of any group which contains
   * any of the given mods.
   * \param mod_ids Ids of the mods for which to uninstall group members.
   */
  void uninstallGroupMembers(const std::vector<int>& mod_ids);
  /*!
   * \brief Adds a new tag with the given name. Fails if a tag by that name already exists.
   * \param tag_name Name for the new tag.
   * \throw std::runtime_error If a tag by that name exists.
   */
  void addManualTag(const std::string& tag_name);
  /*!
   * \brief Removes the tag with the given name, if it exists.
   * \param tag_name Tag to be removed.
   * \param update_map If true: Update the manual tag map.
   */
  void removeManualTag(const std::string& tag_name, bool update_map = true);
  /*!
   * \brief Changes the name of the given tag to the given new name.
   * Fails if a tag by the given name exists.
   * \param old_name Name of the target tag.
   * \param new_name Target tags new name.
   * \param update_map If true: Update the manual tag map.
   * \throw std::runtime_error If a tag with the given new_name exists.
   */
  void changeManualTagName(const std::string& old_name,
                           const std::string& new_name,
                           bool update_map = true);
  /*!
   * \brief Adds the given tags to all given mods.
   * \param tag_name Target tags name.
   * \param mod_ids Target mod ids.
   */
  void addTagsToMods(const std::vector<std::string>& tag_names, const std::vector<int>& mod_ids);
  /*!
   * \brief Removes the given tags from the given mods.
   * \param tag_name Target tags name.
   * \param mod_ids Target mod ids.
   */
  void removeTagsFromMods(const std::vector<std::string>& tag_names,
                          const std::vector<int>& mod_ids);
  /*!
   * \brief Sets the tags for all given mods to the given tags.
   * \param tag_names Names of the new tags.
   * \param mod_ids Target mod ids.
   */
  void setTagsForMods(const std::vector<std::string>& tag_names, const std::vector<int> mod_ids);
  /*!
   * \brief Performs the given editing actions on the manual tags.
   * \param actions Editing actions.
   */
  void editManualTags(const std::vector<EditManualTagAction>& actions);
  /*!
   * \brief Adds a new auto tag.
   * \param name The new tags name.
   * \param expression Expression used for the new tags evaluator.
   * \param conditions Conditions used for the new tags evaluator.
   * \param update If true: Update the auto tag map and the settings.
   * \throw std::runtime_error If a tag by that name exists.
   */
  void addAutoTag(const std::string& tag_name,
                  const std::string& expression,
                  const std::vector<TagCondition>& conditions,
                  bool update);
  /*!
   * \brief Adds a new auto tag from the given Json object.
   * \param json_tag Json object representing the new auto tag.
   * \param update If true: Update the auto tag map and the settings.
   * \throw std::runtime_error If a tag by that name exists.
   */
  void addAutoTag(const Json::Value& json_tag, bool update);
  /*!
   * \brief Removes the given auto tag.
   * \param name Tag to be removed.
   * \param update If true: Update the auto tag map and the settings.
   */
  void removeAutoTag(const std::string& tag_name, bool update);
  /*!
   * \brief Changes the name of the given auto tag to the given new name.
   * Fails if a tag by the given name exists.
   * \param old_name Name of the target tag.
   * \param new_name Target tags new name.
   * \param update If true: Update the auto tag map.
   * \throw std::runtime_error If a tag with the given new_name exists.
   */
  void renameAutoTag(const std::string& old_name, const std::string& new_name, bool update);
  /*!
   * \brief Changes the given tags evaluator according to the given expression and conditions.
   * \param tag_name Target auto tag.
   * \param expression New expression to be used.
   * \param conditions Conditions for the new expression.
   * \param update If true: Update the auto tag map.
   */
  void changeAutoTagEvaluator(const std::string& tag_name,
                              const std::string& expression,
                              const std::vector<TagCondition>& conditions,
                              bool update);
  /*!
   * \brief Performs the given editing actions on the auto tags.
   * \param actions Editing actions.
   */
  void editAutoTags(const std::vector<EditAutoTagAction>& actions);
  /*! \brief Reapply all auto tags to all mods. */
  void reapplyAutoTags();
  /*!
   * \brief Reapplies auto tags to the specified mods.
   * \param mod_ids Mods to which auto tags are to be reapplied.
   */
  void updateAutoTags(const std::vector<int> mod_ids);
  /*! \brief Deletes all data for this app. */
  void deleteAllData();
  /*!
   * \brief Sets the app version of the currently active profile to the given version.
   * \param app_version The new app version.
   */
  void setAppVersion(const std::string& app_version);
  /*!
   * \brief Sets the given mods local and remote sources to the given paths.
   * \param mod_id Target mod id.
   * \param local_source Path to a local archive or directory used for mod installation.
   * \param remote_source Remote URL from which the mod was downloaded.
   */
  void setModSources(int mod_id, const std::string& local_source, const std::string& remote_source);
  /*!
   * \brief Fetches data from NexusMods for the given mod.
   * \param mod_id Target mod id.
   * \return A Mod object containing all data from NexusMods regarding that mod.
   */
  nexus::Page getNexusPage(int mod_id);
  /*! \brief Checks for updates for all mods. */
  void checkForModUpdates();
  /*!
   * \brief Checks for updates for mods with the given ids.
   * \param mod_ids Ids of the mods for which to check for updates.
   */
  void checkModsForUpdates(const std::vector<int>& mod_ids);
  /*!
   * \brief Temporarily disables update notifications for the given mods. This is done
   * by setting the mods remote_update_time to the installation_time.
   * \param mod_ids Ids of the mods for which update notifications are to be disabled.
   */
  void suppressUpdateNotification(const std::vector<int>& mod_ids);
  /*!
   * \brief Permanently enables or disables update checks for the given mod. Unlike
   * suppressUpdateNotification, this persists across runs and prevents the mod from ever
   * being reported as out of date until explicitly re-enabled.
   * \param mod_id Id of the target mod.
   * \param ignored If true, updates for the mod are permanently ignored.
   */
  void setUpdateIgnored(int mod_id, bool ignored);
  /*!
   * \brief Checks whether updates for the given mod are permanently ignored.
   * \param mod_id Id of the target mod.
   * \return True if the mod is on the update ignore list.
   */
  bool isUpdateIgnored(int mod_id) const;
  /*!
   * \brief Checks if files deployed by the given deployer have been externally overwritten.
   * \param deployer Deployer to check.
   * \return Contains data about overwritten files.
   */
  ExternalChangesInfo getExternalChanges(int deployer);
  /*!
   * \brief Currently only supports hard link deployment.
   * For every given file: Moves the modified file into the source mods directory and links
   * it back in, if the changes are to be kept. Else: Deletes that file and restores
   * the original link.
   * \param deployer Target deployer.
   * \param changes_to_keep Contains paths to modified files, the id of the mod currently
   * responsible for that file and a bool which indicates whether or not changes to
   * that file should be kept.
   */
  void keepOrRevertFileModifications(int deployer, const FileChangeChoices& changes_to_keep) const;
  /*! \brief For all deployers: If using hard links that can't be created, switch to sym links. */
  void fixInvalidHardLinkDeployers();
  /*!
   * \brief Exports configurations for the given deployers and the given auto tags to a json file.
   * Does not include mods.
   * \param deployers Deployers to export.
   * \param auto_tags Auto tags to export.
   */
  void exportConfiguration(const std::vector<int>& deployers,
                           const std::vector<std::string>& auto_tags);
  /*!
   * \brief Updates the file ignore list for ReverseDeployers.
   * \param deployer Target deployer.
   */
  void updateIgnoredFiles(int deployer);
  /*!
   * \brief Adds the given mod to the ignore list of the given ReverseDeployer.
   * \param deployer Target deployer.
   * \param mod_id Mod to be ignored.
   */
  void addModToIgnoreList(int deployer, int mod_id);
  /*!
   * \brief Applies the given mod action to the given mod.
   * \param deployer Target deployer.
   * \param action Action to be applied.
   * \param mod_id Target mod.
   */
  void applyModAction(int deployer, int action, int mod_id);
  /*!
   * \brief Returns the path used to store downloaded mods.
   * \return The download path.
   */
  std::filesystem::path getDownloadDir() const;
  /*!
   * \brief Sets the user note for the given mod. An empty string clears the note.
   * Notes are not profile-scoped and are persisted in lmm_mods.json.
   * \param mod_id Target mod id.
   * \param note The new note text.
   */
  void setModNote(int mod_id, const std::string& note);
  /*!
   * \brief Pins the given mod to its current installed version, suppressing update
   * notifications unless the remote version is strictly newer than the pinned version.
   * Has no effect if the mod id is unknown.
   * \param mod_id Target mod id.
   */
  void pinModVersion(int mod_id);
  /*!
   * \brief Unpins the given mod, re-enabling normal update notification behavior.
   * Has no effect if the mod id is unknown or unpinned.
   * \param mod_id Target mod id.
   */
  void unpinModVersion(int mod_id);

  /*!
   * \brief Returns all mod rules for this application.
   * \return A const reference to the rules vector.
   */
  const std::vector<ModRule>& getModRules() const;
  /*!
   * \brief Returns all mod rules that have the given mod as their source.
   * \param source_mod_id The source mod.
   * \return A vector of matching rules.
   */
  std::vector<ModRule> getModRulesFor(int source_mod_id) const;
  /*!
   * \brief Adds a new rule. Does nothing if an identical rule already exists.
   * \param rule The rule to add.
   */
  void addModRule(const ModRule& rule);
  /*!
   * \brief Removes a rule. Does nothing if the rule does not exist.
   * \param rule The rule to remove.
   */
  void removeModRule(const ModRule& rule);
  /*!
   * \brief Replaces all rules for the given source mod with the provided list.
   * \param source_mod_id The source mod.
   * \param rules New rules for this mod. All must have source_mod_id as their source.
   */
  void setModRulesFor(int source_mod_id, const std::vector<ModRule>& rules);
  /*!
   * \brief Checks all rules against the currently enabled mods in all deployers.
   * Returns a human-readable warning string listing violations.
   * An empty string means no violations.
   * \return The warning message, or an empty string.
   */
  std::string checkModRules() const;
  /*!
   * \brief Merges conflicting WitcherScript (.ws) files across the enabled mods of the given
   * Witcher 3 deployer into a dedicated merged-scripts folder in the staging directory.
   * \param deployer Target deployer (should be a Witcher 3 deployer).
   * \return A human-readable summary of what was merged and which scripts need manual review.
   */
  std::string mergeTw3Scripts(int deployer);
  /*!
   * \brief Merges each enabled mod's input.xml fragment into the Witcher 3 shared input.xml
   * for the given deployer, idempotently (via LIMO_MERGE sentinel markers).
   * \param deployer Target deployer (should be a Witcher 3 deployer).
   * \return A human-readable summary of the merge.
   */
  std::string mergeTw3Config(int deployer);
  /*!
   * \brief Builds a human-readable Cyberpunk 2077 Proton setup report for the given deployer:
   * the setup checklist, required launch options, the protontricks command, and a deploy-mode
   * safety warning for the deployer's target.
   * \param deployer Target deployer (should be a Cyberpunk 2077 deployer).
   * \return The report text.
   */
  std::string getCyberpunkSetupInfo(int deployer);
  /*!
   * \brief Lays out the enabled REDmods of the given deployer into the game's mods directory
   * and returns the shell command that runs redMod.exe deploy under Proton.
   * \param deployer Target deployer (should be a Cyberpunk 2077 deployer).
   * \return The deploy command, or an empty string if no REDmods were found among the enabled
   * mods.
   */
  std::string buildRedmodDeployCommand(int deployer);

  /*! \brief Name of the config file contained in an exported instance bundle. */
  inline static const std::string INSTANCE_BUNDLE_FILE_NAME = "limo_instance.json";
  /*! \brief Format version written into exported instance bundles. */
  inline static constexpr int INSTANCE_BUNDLE_VERSION = 1;
  /*! \brief Token used to mark a stored path as relative to the staging directory. */
  inline static const std::string STAGING_TOKEN = "$STAGING$";

  /*!
   * \brief Exports this instance's configuration (app, deployers, profiles, tools, groups,
   * tags and rules) to a single self-contained, portable JSON bundle.
   *
   * The bundle does NOT contain the mod blobs themselves; it only captures the metadata
   * required to recreate the instance on another machine. Stored paths are generalized
   * (Steam install / prefix / home paths are tokenized) so that the bundle is relocatable.
   * The structure mirrors the on-disk settings written by \ref updateSettings, wrapped in a
   * small header containing a format version and metadata.
   *
   * \param target Destination file for the bundle. If it names an existing directory, the
   * bundle is written inside it using \ref INSTANCE_BUNDLE_FILE_NAME.
   * \throws std::runtime_error If the target file can not be written.
   */
  void exportInstance(const std::filesystem::path& target) const;

  /*!
   * \brief Parses an exported instance bundle (as produced by \ref exportInstance) and
   * returns the contained settings object, ready to be written as a config file.
   *
   * Steam/home path tokens are left untouched (they are resolved lazily on load, exactly
   * like the regular config), so the returned object is portable. The caller
   * (e.g. ApplicationManager / UI) is responsible for choosing a staging directory and
   * constructing a \ref ModdedApplication from it; see \ref importInstanceInto for a helper
   * that writes the parsed config into a fresh staging directory.
   *
   * \param bundle Path to the bundle file, or a directory containing
   * \ref INSTANCE_BUNDLE_FILE_NAME.
   * \return The parsed settings object (the same shape as the on-disk config file).
   * \throws std::runtime_error If the bundle can not be read.
   * \throws ParseError If the bundle is not a valid instance bundle.
   */
  static Json::Value parseInstanceBundle(const std::filesystem::path& bundle);

  /*!
   * \brief Imports an exported instance bundle into the given (fresh) staging directory by
   * writing a config file that a \ref ModdedApplication can subsequently load.
   *
   * This is the import counterpart to \ref exportInstance. It does not move any mod blobs;
   * the resulting instance references mods by id/path exactly as the source did. The caller
   * is expected to construct a \ref ModdedApplication on \p staging_dir afterwards.
   *
   * \param bundle Path to the bundle file (or a directory containing it).
   * \param staging_dir Target staging directory. Must not already contain a config file.
   * \throws std::runtime_error If a config file already exists in \p staging_dir or it can
   * not be written.
   * \throws ParseError If the bundle is invalid.
   */
  static void importInstanceInto(const std::filesystem::path& bundle,
                                 const std::filesystem::path& staging_dir);

private:
  /*! \brief The subdirectory used to store downloads. */
  static inline constexpr std::string DOWNLOAD_DIR = "_download";
  /*!
   * \brief Returns (mod id, staging path) for every enabled, non-separator entry in the given
   * deployer's load order, in load order.
   * \param deployer Target deployer.
   * \return The mod id / staging path pairs.
   */
  std::vector<std::pair<int, std::filesystem::path>> getEnabledModPathsInLoadOrder(
    int deployer) const;

  /*! \brief The name of this application. */
  std::string name_;
  /*! \brief Contains the internal state of this object. */
  Json::Value json_settings_;
  /*!
   * \brief True if the on-disk settings file could not be parsed during the last load attempt.
   * While set, writeSettings() refuses to overwrite the existing settings file so a parse
   * error can not destroy a recoverable config.
   */
  bool settings_load_failed_ = false;
  /*! \brief The path to the staging directory containing all installed mods. */
  std::filesystem::path staging_dir_;
  /*! \brief Contains all currently installed mods. */
  std::vector<Mod> installed_mods_;
  /*! \brief Contains every Deployer used by this application. */
  std::vector<std::unique_ptr<Deployer>> deployers_;
  /*! \brief Contains all tools for this application. */
  std::vector<Tool> tools_;
  /*! \brief The command used to run this application. */
  std::string command_ = "";
  /*! \brief The currently active profile id. */
  int current_profile_ = 0;
  /*! \brief Contains names of all profiles. */
  std::vector<std::string> profile_names_;
  /*! \brief For every group: A vector containing every mod in that group. */
  std::vector<std::vector<int>> groups_;
  /*! \brief Maps mods to their groups. */
  std::map<int, int> group_map_;
  /*! \brief Contains the active member of every group. */
  std::vector<int> active_group_members_;
  /*! \brief User-visible name for each group, parallel to groups_. */
  std::vector<std::string> group_names_;
  /*! \brief Free-form notes for each group, parallel to groups_. */
  std::vector<std::string> group_notes_;
  /*! \brief Maps mods to the installer used during their installation. */
  std::map<int, std::string> installer_map_;
  /*! \brief Ids of mods for which update checks are permanently ignored. */
  std::set<int> update_ignore_list_;
  /*! \brief Path to this applications icon. */
  std::filesystem::path icon_path_;
  /*! \brief Callback for logging. */
  std::function<void(Log::LogLevel, const std::string&)> log_ = [](Log::LogLevel a,
                                                                   const std::string& b) {};
  /*! \brief Manages all backups for this application. */
  BackupManager bak_man_;
  /*! \brief Id of the most recently installed mod. */
  int last_mod_id_ = -1;
  /*! \brief Contains all known manually managed tags. */
  std::vector<ManualTag> manual_tags_;
  /*! \brief Maps mod ids to a vector of manual tags associated with that mod. */
  std::map<int, std::vector<std::string>> manual_tag_map_;
  /*! \brief Contains all known auto tags. */
  std::vector<AutoTag> auto_tags_;
  /*! \brief Maps mod ids to a vector of auto tags associated with that mod. */
  std::map<int, std::vector<std::string>> auto_tag_map_;
  /*!
   *  \brief For every profile: The version of the app managed by that profile.
   *
   *  This does not refer to a ModdedApplication object but rather the actually
   *  modded application.
   */
  std::vector<std::string> app_versions_;
  /*! \brief Callback used to inform about the current task's progress. */
  std::function<void(float)> progress_callback_ = [](float f) {};
  /*! \brief File name used to store exported deployers and auto tags. */
  std::string export_file_name = "exported_config";
  /*! \brief Steam app id. Or -1 if not a Steam app. */
  long steam_app_id_;
  /*! \brief App-global mod dependency / conflict rules. */
  std::vector<ModRule> mod_rules_;
  /*! \brief Shell command run before deployment. Empty: disabled. */
  std::string pre_deploy_hook_ = "";
  /*! \brief Shell command run after deployment. Empty: disabled. */
  std::string post_deploy_hook_ = "";
  /*! \brief Shell command run before undeployment. Empty: disabled. */
  std::string pre_undeploy_hook_ = "";
  /*! \brief Shell command run after undeployment. Empty: disabled. */
  std::string post_undeploy_hook_ = "";

  /*!
   * \brief Runs a user-authored hook command via the existing safe runner.
   *
   * The command is treated as a complete command line authored by the user
   * (exactly like a Tool command overwrite); it is never re-escaped or
   * shell-wrapped beyond what the user typed. Start and exit status are logged.
   * \param hook_name Human readable name of the hook, used for logging.
   * \param command The command line to run. Empty: no-op.
   * \return The command's exit code, or 0 if the command was empty.
   */
  int runHook(const std::string& hook_name, const std::string& command) const;

  /*!
   * \brief Updates json_settings_ with the current state of this object.
   * \param write If true: write json_settings_ to a file after updating.
   */
  void updateSettings(bool write = false);
  /*!
   * \brief Writes json_settings_ to a file at app_mod_dir_/CONFIG_FILE_NAME.
   */
  void writeSettings() const;
  /*!
   * \brief Reads json_settings_ from a file at app_mod_dir_/CONFIG_FILE_NAME.
   */
  void readSettings();
  /*!
   * \brief Updates the internal state of this object to the state stored in json_settings_.
   * \param read If true: Read json_settings_ from a file before updating.
   */
  void updateState(bool read = false);
  /*!
   * \brief Returns the name of a mod.
   * \param mod_id The mod.
   * \return The name.
   * \throws Json::LogicError Indicates a logic error, e.g. trying to convert "123" to a bool,
   * while parsing.
   * \throws Json::RuntimeError Indicates a syntax error in the JSON file.
   * \throws ParseError Indicates a semantic error while parsing the JSON file, e.g.
   * the active member of a group is not part of that group.
   */
  std::string getModName(int mod_id) const;
  /*!
   * \brief Updates the load order for every Deployer to reflect the current mod groups.
   * \param progress_node Used to inform about the current progress.
   */
  void updateDeployerGroups(std::optional<ProgressNode*> progress_node = {});
  /*!
   * \brief If given mod contains a sub-directory managed by a deployer that is not the given
   * deployer, creates a new mod which contains that sub-directory.
   * \param mod_id Mod to check.
   * \param deployer Deployer which currently manages the given mod.
   */
  void splitMod(int mod_id, int deployer);
  /*!
   * \brief Replaces an existing mod with the mod specified by the given argument.
   * \param info Contains all data needed to install the mod.
   */
  void replaceMod(const ImportModInfo& info);
  /*! \brief Updates manual_tag_map_ with the information contained in manual_tags_. */
  void updateManualTagMap();
  /*! \brief Updates auto_tag_map_ with the information contained in auto_tags_. */
  void updateAutoTagMap();
  /*!
   * \brief Checks for available updates for mods with the given index in installed_mods_.
   * \param target_mod_indices Target mod indices.
   */
  void performUpdateCheck(const std::vector<int>& target_mod_indices);
  /*!
   * \brief Checks if the given path belongs to a steam installation or prefix directory.
   * Replaces installation or prefix path components with tokens.
   * \param path Path to check.
   * \return If no steam paths are found: The input path, else: The modified path.
   */
  std::string generalizeSteamPath(const std::string& path) const;
  /*! \brief If the icon path is a steam path: Update it to the new format. */
  void updateSteamIconPath();
  /*! \brief If steam_app_id_ == -1: Try to determine the app id. */
  void updateSteamAppId();
  /*!
<<<<<<< HEAD
   * \brief Builds the set of mod ids that are enabled in at least one deployer for the
   * current profile.
   * \return Set of enabled mod ids.
   */
  std::unordered_set<int> getEnabledModIds() const;
  /*!
   * \brief Builds the set of all mod ids present in any deployer (enabled or disabled)
   * for the current profile.
   * \return Set of mod ids in any deployer.
   */
  std::unordered_set<int> getDeployedModIds() const;
  /*!
   * \brief Scans a freshly installed mod's staging files for well known script extender /
   * loader executables (e.g. skse64_loader.exe) and, for any not already present as a tool,
   * adds a best-effort Tool pointing at the executable. Never throws: errors are logged.
   * \param mod_id Id of the mod whose staging directory should be scanned.
   */
  void autoAddScriptExtenderTools(int mod_id);
  /*!
   * \brief Converts an absolute path into a staging-relative, relocatable form.
   *
   * If \p path lies inside the staging directory, the staging prefix is replaced with
   * \ref STAGING_TOKEN. Otherwise the path is returned unchanged. This keeps configs
   * portable while remaining a no-op for paths that live outside the instance.
   * \param path Path to relativize.
   * \return The relocatable representation.
   */
  std::string relativizeToStaging(const std::filesystem::path& path) const;
  /*!
   * \brief Resolves a (possibly tokenized) stored path back to an absolute path.
   *
   * If \p path starts with \ref STAGING_TOKEN it is resolved against the current staging
   * directory. Any other value (e.g. legacy absolute paths) is returned unchanged, which
   * preserves backward compatibility with existing configs.
   * \param path Stored path, possibly containing \ref STAGING_TOKEN.
   * \return The resolved path.
   */
  std::filesystem::path resolveFromStaging(const std::string& path) const;
};
