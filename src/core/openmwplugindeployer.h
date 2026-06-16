/*!
 * \file openmwplugindeployer.h
 * \brief Header for the OpenMwPluginDeployer class
 */

#pragma once

#include "lootdeployer.h"
#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>


/*!
 * \brief Autonomous deployer which handles plugin files for OpenMW using LOOT.
 */
class OpenMwPluginDeployer : public LootDeployer
{
public:
  /*!
   * \brief Loads plugins.
   * \param source_path Path to the directory containing installed plugins.
   * \param dest_path Path to the directory containing openmw.cfg.
   * \param name A custom name for this instance.
   * \param init_tags If true: Initializes plugin tags. Disable this for testing purposes
   * with invalid plugin files
   */
  OpenMwPluginDeployer(const std::filesystem::path& source_path,
                 const std::filesystem::path& dest_path,
                 const std::string& name);

  /*! \brief Action id for adding a groundcover tag. */
  static constexpr int ACTION_ADD_GROUNDCOVER_TAG = 0;
  /*! \brief Action id for removing a groundcover tag. */
  static constexpr int ACTION_REMOVE_GROUNDCOVER_TAG = 1;

  /*!
   * \brief If no backup exists: Backs up current plugin file, then reloads all plugins.
   * \param progress_node Used to inform about the current progress.
   */
  virtual void unDeploy(std::optional<ProgressNode*> progress_node = {}) override;
  /*!
   * \brief Restores the single-file plugin backup created by \ref unDeploy, if it exists.
   *
   * OpenMW manages only one plugin file (\ref plugin_file_name_) rather than the
   * loadorder + plugin file pair that \ref LootDeployer::restoreUndeployBackupIfExists
   * expects, so the inherited two-file restore would never trigger. This override uses
   * the single-file \ref PluginDeployer semantics to match what \ref unDeploy backs up.
   */
  virtual void restoreUndeployBackupIfExists() override;
  /*!
   * \brief Groups plugins by whether or not they are scrips, groundcover plugins or neither.
   * \return For every group: The plugin IDs part of that group.
   */
  virtual std::vector<std::vector<int>> getConflictGroups() const override;
  /*!
   * \brief Returns all available auto tag names.
   * \return The tag names mapped to how many plugins of that tag exist.
   */
  virtual std::map<std::string, int> getAutoTagMap() override;
  /*!
   * \brief Sort mods by into script, groundcover and normal groups.
   * \param progress_node Used to inform about the current progress.
   */
  virtual void sortModsByConflicts(std::optional<ProgressNode*> progress_node = {}) override;
  /*!
   * \brief Returns names and icon names for additional actions which can be applied to a mod.
   * \return The actions.
   */
  virtual std::vector<std::pair<std::string, std::string>> getModActions() const override;
  /*!
   * \brief Returns a vector containing valid mod actions.
   * \return For every mod: IDs of every valid mod_action which is valid for that mod.
   */
  virtual std::vector<std::vector<int>> getValidModActions() const override;
  /*!
   * \brief Applies the given mod action to the given mod.
   * \param action Action to be applied.
   * \param mod_id Target mod.
   */
  virtual void applyModAction(int action, int mod_id) override;
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
  // fork #87: OpenMW data= entry (VFS) deploy mode
  /*!
   * \brief Selects between the default behavior and the OpenMW native VFS deploy mode.
   *
   * This is an opt-in, deployer-local mode and does not change the inherited
   * \ref Deployer::DeployMode (which always stays \ref copy for autonomous deployers).
   * The base \ref setDeployMode is mapped here so existing UI/serialization can reach the
   * new mode without touching always-compiled headers: \ref sym_link enables the VFS mode,
   * any other value disables it.
   * \param deploy_mode \ref sym_link enables data= entry mode, anything else disables it.
   */
  virtual void setDeployMode(DeployMode deploy_mode) override;
  // fork #87: OpenMW data= entry (VFS) deploy mode
  /*!
   * \brief Explicitly enables or disables the OpenMW data= entry (VFS) deploy mode.
   *
   * When enabled, \ref writePluginsPrivate appends a Limo-owned block of
   * \c data="..." lines (one per enabled mod's staging directory, in load order) to
   * openmw.cfg instead of relying solely on the upstream file deployer. Default: off.
   * \param enabled The new state.
   */
  void setUseDataEntryMode(bool enabled);
  /*!
   * \brief Getter for \ref use_data_entries_.
   * \return True iff the OpenMW data= entry (VFS) deploy mode is active.
   */
  bool usesDataEntryMode() const;

private:
  /*! \brief Name of the OpenMW config file. */
  static constexpr std::string OPEN_MW_CONFIG_FILE_NAME = "openmw.cfg";
  /*! \brief Name of the groundcover tag. */
  static constexpr std::string GROUNDCOVER_TAG = "Groundcover";
  /*! \brief Name of the open mw tag. */
  static constexpr std::string OPENMW_TAG = "OpenMW";
  /*! \brief Name of the es plugin tag. */
  static constexpr std::string ES_PLUGIN_TAG = "ES-Plugin";
  /*! \brief Name of the es plugin tag. */
  static constexpr std::string SCRIPTS_PLUGIN_TAG = "Scripts";
  /*!
   * \brief Candidate file names (relative to the destination directory) for a PLOX/mlox
   * rules file. The first one that exists is used. PLOX is the maintained successor to
   * mlox and uses the same rules-file format.
   */
  static constexpr std::array<std::string_view, 4> PLOX_RULES_FILE_NAMES = {
    "plox_rules.txt", "mlox_user.txt", "mlox_base.txt", "mlox_rules.txt"
  };
  // fork #87: OpenMW data= entry (VFS) deploy mode
  /*! \brief Marker line opening Limo's managed block of data= entries in openmw.cfg. */
  static constexpr std::string_view DATA_BLOCK_BEGIN_MARKER = "# BEGIN limo data entries";
  /*! \brief Marker line closing Limo's managed block of data= entries in openmw.cfg. */
  static constexpr std::string_view DATA_BLOCK_END_MARKER = "# END limo data entries";

  /*! \brief Number of plugins with groundcover tag. */
  int num_groundcover_plugins_ = 0;
  /*! \brief Number of plugins with openmw tag. */
  int num_openmw_plugins_ = 0;
  /*! \brief Number of plugins with es plugin tag. */
  int num_es_plugins_ = 0;
  /*! \brief Number of script plugins. */
  int num_scripts_plugins_ = 0;
  /*! \brief Maps plugins to a set of tags. */
  std::map<std::string, std::set<std::string>> tag_map_;
  /*! \brief Names of groundcover plugins. */
  std::set<std::string> groundcover_plugins_;
  // fork #87: OpenMW data= entry (VFS) deploy mode
  /*! \brief If true: manage a Limo-owned block of data= entries in openmw.cfg. Default: off. */
  bool use_data_entries_ = false;

  /*! \brief Wrapper for \ref writePluginsPrivate. */
  void writePlugins() const override;
  /*!
   * \brief Case-insensitive override of updatePlugins().
   *
   * OpenMW plugin names in openmw.cfg may differ in case from the actual
   * filenames on disk (e.g. "MyMod.ESP" vs "MyMod.esp").  The base-class
   * implementation uses case-sensitive equality, so a plugin whose stored
   * name case doesn't exactly match the on-disk name is treated as a new
   * plugin and re-enabled, losing any disabled state.  This override
   * performs all name comparisons case-insensitively to fix that.
   */
  void updatePlugins() override;
  /*!
   * \brief Sorts the current load order using libloot with \ref loot::GameType::openmw and
   * the OpenMW masterlist. On success, \ref plugins_ is reordered to match libloot's result.
   * \param progress_node Used to inform about the current progress.
   * \return True if libloot sorting succeeded, false if it had to be skipped (e.g. a missing
   * or incompatible masterlist), in which case the existing load order is left untouched.
   */
  bool sortPluginsWithLoot(std::optional<ProgressNode*> progress_node);
  /*!
   *  \brief Initializes the plugin file, if it does not exist.
   *  \return A bool indicating if the plugin file was created.
   */
  bool initPluginFile();
  /*! \brief Reads the plugin tags from disk. */
  void readPluginTags();
  /*! \brief Wrapper for \ref writePluginTagsPrivate. */
  virtual void writePluginTags() const override;
  /*! \brief Wrapper for \ref updatePluginTagsPrivate. */
  virtual void updatePluginTags() override;
  /*! \brief Adds all tags from the tag map to the tags_ vector. */
  void updateTagVector();
  /*! \brief Updates the tag_map_ for every plugin. */
  void updatePluginTagsPrivate();
  /*! \brief Writes plugins to the OpenMW config file. */
  void writePluginTagsPrivate() const;
  /*!
   * \brief Writes a subset of plugins to the OpenMW config file.
   * \param line_prefix Prefix for the line containing the written plugins.
   * \param line_regex Regex matched against lines that should be excluded from existing files.
   * \param plugin_filter Used to filter indices in plugins_.
   * Plugins are written when this returns true.
   */
  void writePluginsToOpenMwConfig(const std::string& line_prefix, const std::regex& line_regex,
                                  std::function<bool(int)> plugin_filter) const;
  /*! \brief Writes the plugins to disk. */
  void writePluginsPrivate() const;
  // fork #87: OpenMW data= entry (VFS) deploy mode
  /*!
   * \brief Rewrites Limo's managed block of data= entries in openmw.cfg.
   *
   * Reads openmw.cfg, strips any previously written Limo block (delimited by
   * \ref DATA_BLOCK_BEGIN_MARKER / \ref DATA_BLOCK_END_MARKER) while preserving every
   * other line verbatim, then, if \p write_entries is true, appends a fresh block holding
   * one \c data="..." line per enabled mod's staging directory in load order
   * (deduplicated, order preserved). The file is written to a temporary file which then
   * atomically replaces openmw.cfg so a failure can never corrupt the original. User and
   * other non-Limo data= lines outside the marked block are never touched.
   * \param write_entries If true: (re)write Limo's data= block; if false: only remove it.
   */
  void writeDataEntries(bool write_entries) const;
  /*!
   * \brief Computes the ordered, deduplicated list of staging directories to expose via
   * \c data= entries for the currently enabled plugins, in load order.
   * \return The directories (as quoted-ready strings).
   */
  std::vector<std::string> collectDataEntryPaths() const;

  /*!
   * \brief Locates a PLOX/mlox rules file in the destination directory.
   * \return Path to the first existing candidate rules file, or an empty optional
   * if none is present.
   */
  std::optional<std::filesystem::path> findPloxRulesFile() const;
  /*!
   * \brief Parses an "A must load before B" ordering constraint out of a PLOX/mlox
   * rules file.
   *
   * Best-effort PLOX/mlox parser. The handled directives are:
   *  - [Order] blocks: every consecutive pair of plugin lines becomes an
   *    "earlier line loads before later line" constraint.
   *  - [Near] blocks: treated like [Order] (the listed plugins are ordered as written).
   * All other directives ([Conflict], [Requires], [Note], [Patch], expression
   * operators, etc.) are ignored. Comments (lines starting with ';') and rule
   * messages are skipped. Constraints that reference plugins not currently present
   * in \ref plugins_ are dropped.
   *
   * \param rules_path Path to the rules file.
   * \return Ordered pairs (a, b) meaning plugin a must load before plugin b.
   */
  std::vector<std::pair<std::string, std::string>> parsePloxOrderRules(
    const std::filesystem::path& rules_path) const;
  /*!
   * \brief Applies PLOX/mlox ordering rules to \ref plugins_ if a rules file exists.
   *
   * Performs a stable topological sort of the current plugin order subject to the
   * parsed "before" constraints. The existing order is used as the tie-breaker so the
   * result stays as close as possible to the input (and to LOOT's sort) while still
   * satisfying the rules. If no rules file is present this is a no-op and the existing
   * behavior is preserved. Cyclic constraints are skipped conservatively.
   * \return True if a rules file was found and applied, false otherwise.
   */
  bool applyPloxRules();
};
