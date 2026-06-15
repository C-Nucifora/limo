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
