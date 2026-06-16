/*!
 * \file plugindeployer.h
 * \brief Header for the PluginDeployer class
 */

#pragma once

#include "deployer.h"
#include <regex>

/*!
 * \brief Base class for autonomous deployers that collects all files which match a given critereon,
 * called plugins, in the source directory and adds them to a file in the target directory.
 */
class PluginDeployer : public Deployer
{
public:
  /*!
   * \brief Constructor
   * \param source_path Directory containing the plugin files.
   * \param dest_path Directory containing the file(s) into which plugin names are to be written.
   * \param name Custom name for this deployer instance.
   */
  PluginDeployer(const std::filesystem::path& source_path,
                 const std::filesystem::path& dest_path,
                 const std::string& name);

  /*!
   * \brief Reloads all deployed plugins.
   * \param progress_node Used to inform about the current progress of deployment.
   * \return Since this is an autonomous deployer, the returned map is always empty.
   */
  virtual std::map<int, unsigned long> deploy(
    std::optional<ProgressNode*> progress_node = {}) override;
  /*!
   * \brief Reloads all deployed plugins.
   * \param loadorder Ignored.
   * \param progress_node Used to inform about the current progress of deployment.
   * \return Since this is an autonomous deployer, the returned map is always empty.
   */
  virtual std::map<int, unsigned long> deploy(
    const std::vector<int>& loadorder,
    std::optional<ProgressNode*> progress_node = {}) override;
  /*!
   * \brief Moves a mod from one position in the load order to another. Saves changes to disk.
   * \param from_index Index of mod to be moved.
   * \param to_index Destination index.
   */
  virtual void swapChild(int from_index, int to_index) override;
  /*!
   * \brief Enables or disables the given mod in the load order. Saves changes to disk.
   * \param mod_id Mod to be edited.
   * \param status The new status.
   */
  virtual void setModStatus(int mod_id, bool status) override;
  /*!
   * \brief Conflict groups are not supported by this type.
   * \return All plugins in the non conflicting group.
   */
  virtual std::vector<std::vector<int>> getConflictGroups() const override;
  /*!
   * \brief Generates a vector of names for every plugin.
   * \return The name vector.
   */
  virtual std::vector<std::string> getModNames() const override;
  /*!
   * \brief Adds a new profile and optionally copies it's load order from an existing profile.
   * Profiles are stored in the target directory.
   * \param source The profile to be copied. A value of -1 indicates no copy.
   */
  virtual void addProfile(int source = -1) override;
  /*!
   * \brief Removes a profile.
   * \param profile The profile to be removed.
   */
  virtual void removeProfile(int profile) override;
  /*!
   * \brief Setter for the active profile. Changes the currently active plugin files
   * to the ones saved in the new profile.
   * \param profile The new profile.
   */
  virtual void setProfile(int profile) override;
  /*!
   * \brief Not supported by this type.
   * \param newConflict_groups Ignored.
   */
  virtual void setConflictGroups(const std::vector<std::vector<int>>& newConflict_groups) override;
  /*!
   * \brief Returns the number of plugins on the load order.
   * \return The number of plugins.
   */
  virtual int getNumMods() override;
  /*!
   * \brief Getter for the current plugin load order.
   * \return The load order.
   */
  virtual std::shared_ptr<TreeItem<DeployerEntry>> getLoadorder() override;
  /*!
   * \brief Does nothing since this deployer manages its own mods.
   * \param mod_id Ignored.
   * \param enabled Ignored.
   * \param update_conflicts Ignored.
   * \return False.
   */
  virtual bool addMod(int mod_id, bool enabled = true, bool update_conflicts = true) override;
  /*!
   * \brief Not supported by this type.
   * \param mod_id Ignored.
   * \return False.
   */
  virtual bool removeMod(int mod_id) override;
  /*!
   * \brief Since this deployer uses its own internal mod ids, this function always
   * returns false.
   * \param mod_id Ignores
   * \return False.
   */
  virtual bool hasMod(int mod_id) override;
  /*!
   * \brief Does nothing since this deployer manages its own mods.
   * \param old_id Ignored.
   * \param new_id Ignored
   * \return False.
   */
  virtual bool swapMod(int old_id, int new_id) override;
  /*!
   * \brief Not supported.
   * \param mod_id Ignored.
   * \param show_disabled Ignored.
   * \param progress_node Set to 100%.
   * \return An empty vector.
   */
  virtual std::vector<ConflictInfo> getFileConflicts(
    int mod_id,
    bool show_disabled = false,
    std::optional<ProgressNode*> progress_node = {}) override;
  /*!
   * \brief Not supported by this type.
   * \param mod_id The mod to be checked.
   * \param progress_node Used to inform about the current progress.
   * \return An empty set.
   */
  virtual std::unordered_set<int> getModConflicts(
    int mod_id,
    std::optional<ProgressNode*> progress_node = {}) override;
  /*!
   * \brief Not supported by this type.
   * \param progress_node Used to inform about the current progress.
   */
  virtual void sortModsByConflicts(std::optional<ProgressNode*> progress_node = {}) override;
  /*! \brief Deletes the config file and all profile files. */
  virtual void cleanup() override;
  /*!
   * \brief Getter for mod tags.
   * \return For every mod: A vector of auto tags added to that mod.
   */
  virtual std::vector<std::vector<std::string>> getAutoTags() override;
  /*!
   * \brief Returns all available auto tag names.
   * \return The tag names.
   */
  virtual std::map<std::string, int> getAutoTagMap() override;
  /*!
   * \brief Not supported by this Deployer type.
   * \param progress_node Ignored
   * \return An empty vector
   */
  virtual std::vector<std::pair<std::filesystem::path, int>> getExternallyModifiedFiles(
    std::optional<ProgressNode*> progress_node = {}) const override;
  /*!
   * \brief Not supported by this Deployer type.
   * \param changes_to_keep Ignored.
   */
  virtual void keepOrRevertFileModifications(const FileChangeChoices& changes_to_keep) override;
  /*!
   * \brief Updates the deployed files for one mod to match those in the mod's source directory.
   * This is not supported for this deployer type.
   * \param mod_id Ignored.
   * \param progress_node Ignored.
   */
  virtual void updateDeployedFilesForMod(
    int mod_id,
    std::optional<ProgressNode*> progress_node = {}) const override;
  /*! \brief Since this deployer type does not use normal deployment methods, this does nothing. */
  virtual void fixInvalidLinkDeployMode() override;
  /*!
   * \brief This deployer always uses copy deploy mode.
   * \param deploy_mode ignored.
   */
  virtual void setDeployMode(DeployMode deploy_mode) override;
  /*!
   * \brief Returns the order in which the deploy function of different
   *  deployers should be called.
   * \return The priority.
   */
  virtual int getDeployPriority() const override;
  /*!
   * \brief Returns whether or not this deployer type supports showing file conflicts.
   * \return True if supported.
   */
  virtual bool supportsFileConflicts() const override;
  /*!
   * \brief Returns whether or not this deployer type supports browsing mod files.
   * \return True if supported.
   */
  virtual bool supportsFileBrowsing() const override;
  /*!
   * \brief Returns whether or not this deployer type uses mod ids as references to
   * source mods. This is usually done by autonomous deployers.
   * \return True
   */
  virtual bool idsAreSourceReferences() const override;
  /*!
   * \brief Returns a vector containing valid mod actions.
   * \return For every mod: IDs of every valid mod_action which is valid for that mod.
   */
  virtual std::vector<std::vector<int>> getValidModActions() const override;

  /*!
   * \brief Describes one enabled plugin whose master dependencies are not all satisfied.
   *
   * Surfaced by \ref findMissingMasters as the detection step of the "guided fix for missing
   * plugin masters" health check (fork feature #150). A plugin (.esp/.esm/.esl) declares the
   * master files it depends on in its file header; if such a master is not present among the
   * managed plugins, or is present but disabled, the game will typically crash on load.
   */
  struct MissingMasterInfo
  {
    /*! \brief File name of the plugin that has unmet master dependencies. */
    std::string plugin;
    /*! \brief Masters required by the plugin that are not present among the managed plugins. */
    std::vector<std::string> missing_masters;
    /*! \brief Masters required by the plugin that are present but currently disabled. */
    std::vector<std::string> disabled_masters;
  };

  /*!
   * \brief Detects enabled plugins whose required master files are missing or disabled.
   *
   * For every enabled plugin the master dependencies are read directly from the plugin file
   * header (see \ref readPluginMasters) and compared, case-insensitively, against the set of
   * managed plugins. Masters that are not present at all are reported as missing; masters that
   * are present but disabled are reported separately. Plugins whose files cannot be read or are
   * too short to contain a valid header are skipped silently and never cause a crash.
   *
   * This is the detection ("surfacing") half of the guided fix; the interactive fix UI that
   * lets the user enable/install the offending masters is a documented follow-up.
   * \return One \ref MissingMasterInfo entry per plugin that has at least one missing or
   * disabled master. Plugins with all masters satisfied are omitted.
   */
  std::vector<MissingMasterInfo> findMissingMasters() const;

  // fork #202: plugin ESM/ESL flag awareness.
  /*!
   * \brief Describes the ESM/ESL flag state of a single managed plugin.
   *
   * Bethesda plugins (.esp/.esm/.esl) store record-header flags in the 4 byte flags field of
   * their leading TES4 record (file offset 8, little-endian uint32): the ESM/master bit is
   * 0x1 and the ESL/light bit is 0x200. The plugin's file extension also carries meaning: a
   * .esl file is always treated as light and a .esm file as master, regardless of the header
   * flag. \ref readPluginFlagInfo combines both signals.
   */
  struct PluginFlagInfo
  {
    /*! \brief File name of the plugin. */
    std::string name;
    /*! \brief True if the plugin file exists and could be read as a valid TES4 plugin. */
    bool exists = false;
    /*! \brief True if the plugin is a master (ESM): .esm extension or header flag 0x1 set. */
    bool is_master = false;
    /*! \brief True if the plugin is light (ESL): .esl extension or header flag 0x200 set. */
    bool is_light = false;
  };

  /*!
   * \brief Reads ESM/ESL flag information for every currently managed plugin.
   *
   * For each plugin the leading TES4 record header is read (cheaply: only the 4 byte signature
   * and the 4 byte flags field at offset 8 are inspected) and combined with the file extension.
   * Files that are missing, too short or not TES4 plugins have their flags marked unknown via
   * \ref PluginFlagInfo::exists, while still honoring any .esm/.esl extension hint. This never
   * throws.
   * \return One \ref PluginFlagInfo per managed plugin, in load order.
   */
  std::vector<PluginFlagInfo> getPluginFlagInfo() const;

  /*!
   * \brief Reads ESM/ESL flag information together with full/light counts.
   *
   * Convenience wrapper around \ref getPluginFlagInfo that also tallies how many plugins are
   * FULL (not light) versus LIGHT, for comparison against the engine caps (254 full, 4096
   * light).
   * \return A pair of the per-plugin info vector and a {full_count, light_count} pair.
   */
  std::pair<std::vector<PluginFlagInfo>, std::pair<int, int>> getPluginFlagInfoWithCounts() const;

protected:
  /*! \brief Appended to profile file names. */
  static constexpr std::string EXTENSION = ".lmmprof";
  /*! \brief File extension for plugins.txt and loadorder.txt backup files. */
  static constexpr std::string UNDEPLOY_BACKUP_EXTENSION = ".undeplbak";
  /*! \brief Name of the file containing settings. */
  std::string config_file_name_ = ".lmmconfig";
  /*! \brief Name of the file containing source mod ids for plugins. */
  std::string source_mods_file_name_ = ".lmm_mod_sources";

  /*! \brief Name of the file containing plugin activation status. */
  std::string plugin_file_name_ = "plugins.txt";
  /*! \brief Contains names of all plugins and their activation status. */
  std::vector<std::pair<std::string, bool>> plugins_;
  /*! \brief Current number of profiles. */
  int num_profiles_ = 0;
  /*! \brief For every plugin: Every tag associated with that plugin. */
  std::vector<std::vector<std::string>> tags_;
  /*! \brief Maps every plugin to a source mod, if that plugin was created by another deployer. */
  std::map<std::string, int> source_mods_;
  /*! \brief Regex used to match against files in the source directory. */
  std::regex plugin_regex_;
  /*! \brief Regex used to match against lines in the plugin file. */
  std::regex plugin_file_line_regex_;
  /*! \brief Name of the file containing loot tags. */
  std::string tags_file_name_ = ".plugin_tags";

  /*! \brief Updates current plugins to reflect plugins actually in the source directory. */
  virtual void updatePlugins();
  /*! \brief Load plugins from the plugins file. */
  virtual void loadPlugins();
  /*!
   * \brief Reads the enabled/disabled state currently stored in the on-disk plugin state file.
   *
   * The plugin state file (plugin_file_name_) may have been edited by an external tool, the game
   * itself or the user since Limo last wrote it. This returns the externally observed state so that
   * reconciliation can prefer it over Limo's potentially stale in-memory state. Keys are lower cased
   * to preserve the case-insensitive plugin matching used throughout the base class.
   * \return Maps lower cased plugin name to its enabled state, or an empty optional if the file
   * does not exist (i.e. there is no authoritative external state to respect).
   */
  virtual std::optional<std::map<std::string, bool>> readExternalPluginState() const;
  /*! \brief Writes current load order to plugins file. */
  virtual void writePlugins() const;
  /*!
   * \brief Saves number of profiles and active profile to the config file.
   */
  virtual void saveSettings() const;
  /*!
   * \brief Loads number of profiles and active profile from the config file.
   */
  virtual void loadSettings();
  /*! \brief Resets all settings to default values. */
  virtual void resetSettings();
  /*!
   *  \brief Updates the plugin tags for every currently loaded plugin.
   *  Must be implemented in derived classes.
   */
  virtual void updatePluginTags() = 0;
  /*! \brief Writes the current tags_ to disk. */
  virtual void writePluginTags() const;
  /*! \brief If plugin file backups exist, restore it and override the current file. */
  virtual void restoreUndeployBackupIfExists();
  /*! \brief Updates the source mod map with files created by another deployer. */
  virtual void updateSourceMods();
  /*! \brief Writes the source mods to disk. */
  virtual void writeSourceMods() const;
  /*! \brief Reads the source mods from disk. */
  virtual void readSourceMods();
  /*!
   * \brief Finds the directory serving as a target directory for the deployer which manages the
   * given target path.
   * \param target Target path to check.
   * \return The deployers target directory or an empty optional if no directory was found.
   */
  std::optional<std::filesystem::path> getRootOfTargetDirectory(std::filesystem::path target) const;
  /*!
   * \brief Converts the given file name to a hidden file by prepending a ".", if necessary.
   * \param name File name to hide.
   * \return The hidden file.
   */
  std::string hideFile(const std::string& name);

  /*!
   * \brief Reads the master file dependencies declared in a plugin file's header.
   *
   * Performs a minimal binary read of the plugin header only; the rest of the file is never
   * parsed. Two on-disk layouts are supported:
   *
   * - TES4 style (Oblivion, Fallout 3/NV, Skyrim (SE/AE), Fallout 4): the file begins with a
   *   24 byte record header whose 4 byte type is "TES4", followed by the record's field
   *   (subrecord) data. Each field is a 4 byte type plus a 2 byte little-endian size, then the
   *   payload. The master file names are stored in "MAST" fields, each a NUL-terminated string.
   * - TES3 style (Morrowind, OpenMW): the file begins with a "TES3" record; its subrecords use a
   *   4 byte type plus a 4 byte little-endian size. Master file names are stored in "MAST"
   *   subrecords (a NUL-terminated string), each typically followed by a "DATA" subrecord.
   *
   * The function is deliberately defensive: files that cannot be opened, are shorter than a
   * valid header, or have inconsistent sizes are treated as having no masters rather than
   * throwing, so a malformed plugin can never crash the health check.
   * \param plugin_path Absolute path to the plugin file to inspect.
   * \return The list of master file names referenced by the plugin, in file order. Empty if the
   * file could not be read or declares no masters.
   */
  std::vector<std::string> readPluginMasters(const std::filesystem::path& plugin_path) const;

  // fork #202: plugin ESM/ESL flag awareness.
  /*!
   * \brief Reads the ESM/ESL flag state of a single plugin file.
   *
   * Combines the file extension (.esm implies master, .esl implies light) with the TES4 record
   * header flags read from disk. The header read is minimal and defensive: only the 4 byte
   * signature and the flags uint32 at offset 8 are inspected. Files that cannot be opened, are
   * too short or do not start with "TES4" leave \ref PluginFlagInfo::exists false but still
   * honor the extension hint. Never throws.
   * \param plugin_name File name of the plugin (used for the extension hint and result name).
   * \param plugin_path Absolute path to the plugin file to inspect.
   * \return The combined flag info for the plugin.
   */
  PluginFlagInfo readPluginFlagInfo(const std::string& plugin_name,
                                    const std::filesystem::path& plugin_path) const;
};
