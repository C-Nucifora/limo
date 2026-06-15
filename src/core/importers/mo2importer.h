/*!
 * \file mo2importer.h
 * \brief Header for the Mo2Importer class.
 *
 * Implements fork issue #45 / limo-app/limo#92: import an existing
 * Mod Organizer 2 instance into a Limo application.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>


/*!
 * \brief Describes one mod entry parsed from a Mod Organizer 2 instance.
 *
 * Produced by Mo2Importer::parseMods and consumed by Mo2Importer::buildImportPlan
 * to create the list of operations Limo must execute.
 */
struct Mo2ModEntry
{
  /*! \brief Display name taken from the folder name under mods/. */
  std::string name;
  /*! \brief Absolute path to the mod's directory inside mods/. */
  std::filesystem::path source_path;
  /*! \brief Whether the mod is enabled in modlist.txt (prefix '+'). */
  bool enabled = true;
  /*!
   * \brief Zero-based position in the final load order.
   * Lower values are deployed first (i.e. can be overridden by higher values).
   */
  int load_order_index = 0;
};

/*!
 * \brief Describes the result of parsing a single MO2 profile.
 */
struct Mo2ParseResult
{
  /*! \brief Name of the MO2 profile that was parsed. */
  std::string profile_name;
  /*! \brief Ordered list of mods, ready to be registered with Limo. */
  std::vector<Mo2ModEntry> mods;
  /*! \brief Non-fatal warnings encountered while parsing. */
  std::vector<std::string> warnings;
};

/*!
 * \brief Parses a Mod Organizer 2 instance directory and produces import
 * data suitable for registering mods with a Limo \ref ModdedApplication.
 *
 * Only reads from the MO2 directory; never writes to it.
 * Mod files are not moved or copied — the existing mods/ sub-directories
 * are used directly as Limo staging sources.
 *
 * Typical MO2 layout expected:
 * \code
 *   <mo2_instance>/
 *     mods/
 *       <mod_name_1>/
 *       <mod_name_2>/
 *       ...
 *     profiles/
 *       <profile_name>/
 *         modlist.txt   – load order + enabled state
 *         plugins.txt   – plugin load order (optional)
 * \endcode
 *
 * modlist.txt format:
 *   Lines starting with '+' are enabled mods.
 *   Lines starting with '-' are disabled mods.
 *   Lines starting with '*' or '#' are separators / comments — skipped.
 *   The file is listed top-to-bottom from highest to lowest priority.
 */
class Mo2Importer
{
public:
  Mo2Importer() = delete;

  /*!
   * \brief Checks whether the given path looks like a valid MO2 instance root.
   *
   * A path is considered valid when it contains a 'mods' subdirectory and at
   * least one profile directory that contains a modlist.txt file.
   *
   * \param instance_path Path to the suspected MO2 instance root.
   * \return True if the path passes basic validation, false otherwise.
   */
  static bool isValidInstance(const std::filesystem::path& instance_path);

  /*!
   * \brief Returns the names of all profiles found in the MO2 instance.
   *
   * Looks for subdirectories under <instance_path>/profiles/ that contain
   * a modlist.txt file.
   *
   * \param instance_path Path to the MO2 instance root.
   * \return Sorted vector of profile names. Empty if none found.
   */
  static std::vector<std::string> listProfiles(const std::filesystem::path& instance_path);

  /*!
   * \brief Parses the mods/ directory and the given profile's modlist.txt.
   *
   * MO2 modlist.txt lists mods in priority order (highest first).  This
   * function reverses the order so that load_order_index == 0 is the mod
   * deployed first (lowest priority / can be overridden).  Mods present in
   * mods/ but absent from modlist.txt are appended at the end in
   * undefined order with enabled == false.
   *
   * \param instance_path Path to the MO2 instance root.
   * \param profile_name  Name of the profile to parse (must exist under
   *                      profiles/).
   * \return Parsed result including mod list and any warnings.
   * \throws std::runtime_error If the instance path or profile is invalid,
   *         or if modlist.txt cannot be opened.
   */
  static Mo2ParseResult parseProfile(const std::filesystem::path& instance_path,
                                     const std::string& profile_name);

private:
  /*!
   * \brief Reads modlist.txt and returns mod names in ascending priority
   * order (index 0 = lowest priority = deployed first).
   *
   * \param modlist_path  Path to the modlist.txt file.
   * \param enabled_out   Receives whether each mod at the corresponding
   *                      position is enabled.
   * \return Mod names in load-order (lowest priority first).
   * \throws std::runtime_error If the file cannot be opened.
   */
  static std::vector<std::pair<std::string, bool>> readModlist(
    const std::filesystem::path& modlist_path);

  /*!
   * \brief Collects all subdirectory names under <instance_path>/mods/.
   * \param mods_dir Path to the mods/ directory.
   * \return Unordered set of directory names.
   */
  static std::vector<std::string> listModFolders(const std::filesystem::path& mods_dir);
};
