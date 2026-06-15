/*!
 * \file inimanager.h
 * \brief Header for the IniManager class.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>


/*!
 * \brief Provides static helpers for managing per-profile copies of a game's
 * INI / configuration files.
 *
 * Each profile gets its own store of the relevant game config files. Switching
 * profiles is then a matter of copying the stored files back to their original
 * game locations. The caller is responsible for supplying the concrete file
 * paths; IniManager contains no game-specific knowledge.
 *
 * The per-profile store lives at \c <staging_dir>/_profile_ini/<profile>/ and
 * mirrors the original files by their file name.
 */
class IniManager
{
public:
  /*! \brief Name of the directory (relative to the staging dir) holding all per-profile stores. */
  static inline const std::string STORE_DIR_NAME = "_profile_ini";

  /*!
   * \brief Copies the given game INI files into this profile's store.
   *
   * Missing source files are skipped (with a warning). Any I/O errors are
   * logged and do not abort processing of the remaining files.
   * \param staging_dir Application's staging directory.
   * \param profile Index of the profile whose settings are being saved.
   * \param ini_files Absolute paths to the game INI/config files to store.
   */
  static void saveProfileIni(const std::filesystem::path& staging_dir,
                             int profile,
                             const std::vector<std::filesystem::path>& ini_files);

  /*!
   * \brief Copies this profile's stored INI files back to their game locations.
   *
   * For every target the matching file (by file name) is looked up in the
   * profile's store and, if present, copied over the target. Targets without a
   * stored counterpart are skipped. Errors are logged and do not abort the
   * remaining copies.
   * \param staging_dir Application's staging directory.
   * \param profile Index of the profile whose settings should be applied.
   * \param ini_targets Absolute destination paths of the game INI/config files.
   */
  static void applyProfileIni(const std::filesystem::path& staging_dir,
                              int profile,
                              const std::vector<std::filesystem::path>& ini_targets);

  /*!
   * \brief Returns the store directory for the given profile.
   * \param staging_dir Application's staging directory.
   * \param profile Profile index.
   * \return \c <staging_dir>/_profile_ini/<profile>
   */
  static std::filesystem::path profileStorePath(const std::filesystem::path& staging_dir,
                                                int profile);

  /*!
   * \brief Reads the value of a key inside a section of an INI file.
   * \param ini_file Path to the INI file.
   * \param section Section name (without brackets). Empty string for keys
   * appearing before any section header.
   * \param key Key to look up (case-sensitive).
   * \return The trimmed value, or \c std::nullopt if the file, section or key
   * does not exist.
   */
  static std::optional<std::string> getValue(const std::filesystem::path& ini_file,
                                             const std::string& section,
                                             const std::string& key);

  /*!
   * \brief Sets the value of a key inside a section of an INI file, preserving
   * all other content (comments, ordering, blank lines).
   *
   * If the section or key does not exist it is created. The file is created if
   * it does not exist.
   * \param ini_file Path to the INI file.
   * \param section Section name (without brackets). Empty string targets the
   * keys before the first section header.
   * \param key Key to set.
   * \param value New value for the key.
   * \return \c true on success, \c false if the file could not be written.
   */
  static bool setValue(const std::filesystem::path& ini_file,
                       const std::string& section,
                       const std::string& key,
                       const std::string& value);

private:
  /*! \brief Trims leading/trailing whitespace from a string. */
  static std::string trim(const std::string& str);

  /*!
   * \brief Reads all lines of a text file.
   * \param path File to read.
   * \return The file's lines, or \c std::nullopt if it could not be opened.
   */
  static std::optional<std::vector<std::string>> readLines(const std::filesystem::path& path);
};
