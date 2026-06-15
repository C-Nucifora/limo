/*!
 * \file savemanager.h
 * \brief Header for the SaveManager class.
 *
 * Fork feature #24: save-game manager.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>


/*!
 * \brief Game-agnostic enumeration and management of save game files in a directory.
 *
 * This class deliberately avoids any game-specific binary parsing. It only reports
 * cheap, filesystem-level metadata (file name, last write time, size) so that it works
 * for any game. Deletion is provided as a primitive; the caller is responsible for
 * confirming destructive actions with the user.
 */
class SaveManager
{
public:
  /*! \brief Cheap, game-agnostic metadata for a single save file. */
  struct SaveFile
  {
    /*! \brief Absolute path to the save file. */
    std::filesystem::path path;
    /*! \brief File name (including extension). */
    std::string name;
    /*! \brief Last modification time as a Unix timestamp (seconds since epoch). */
    std::int64_t timestamp = 0;
    /*! \brief Size of the file in bytes. */
    std::uintmax_t size = 0;
  };

  /*! \brief Empty default constructor. */
  SaveManager() = default;

  /*!
   * \brief Resolves the saves directory for the given path.
   *
   * If the path is empty or does not exist, an empty path is returned. If the path points
   * to a regular file, its parent directory is returned. Otherwise the path itself is
   * returned (canonicalized when possible).
   * \param saves_dir Candidate saves directory supplied by the UI.
   * \return The resolved directory, or an empty path if none could be resolved.
   */
  static std::filesystem::path resolveSavesDir(const std::filesystem::path& saves_dir);

  /*!
   * \brief Enumerates all save files in the given directory.
   *
   * Only regular files in the top level of the directory are returned. The directory is
   * resolved via \ref resolveSavesDir first. Results are sorted by timestamp, newest first.
   * \param saves_dir Directory containing the save files.
   * \param extensions Optional list of lower-case file extensions (including the leading dot,
   * e.g. ".ess") used to filter the results. If empty, all regular files are returned.
   * \return A vector of SaveFile metadata. Empty if the directory could not be resolved.
   */
  static std::vector<SaveFile> listSaves(const std::filesystem::path& saves_dir,
                                         const std::vector<std::string>& extensions = {});

  /*!
   * \brief Deletes the given save file.
   *
   * The caller is responsible for confirming this destructive action with the user.
   * \param save_path Path to the save file to delete.
   * \return True iff a file was deleted.
   * \throw std::filesystem::filesystem_error On deletion failure.
   */
  static bool deleteSave(const std::filesystem::path& save_path);
};
