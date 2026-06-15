/*!
 * \file installer.h
 * \brief Header for the Installer class
 */

#pragma once

#include "log.h"
#include "progressnode.h"
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>


/*!
 * \brief Holds static functions to install and uninstall mods.
 */
class Installer
{
public:
  /*! \brief Flags used for installation options. */
  enum Flag
  {
    preserve_case = 0,
    lower_case = 1 << 0,
    upper_case = 1 << 1,
    preserve_directories = 1 << 2,
    single_directory = 1 << 3,
    no_extract = 1 << 4
  };
  /*! \brief Every vector represents an exclusive group of flags. */
  inline static const std::vector<std::vector<Flag>> OPTION_GROUPS{
    { preserve_case, lower_case, upper_case },
    { preserve_directories, single_directory }
  };
  /*! \brief Maps installer flags to descriptive names. */
  inline static const std::map<Flag, std::string> OPTION_NAMES{
    { preserve_case, "Preserve file names" },
    { lower_case, "Convert to lower case" },
    { upper_case, "Convert to upper case" },
    { preserve_directories, "Preserve directories" },
    { single_directory, "Root directory only" },
    { no_extract, "Install archive without extracting" }
  };
  /*! \brief Maps installer flags to brief descriptions of what they do. */
  inline static const std::map<Flag, std::string> OPTION_DESCRIPTIONS{
    { preserve_case, "Do not alter file names" },
    { lower_case, "Convert file and directory names to lower case (FiLe -> file)" },
    { upper_case, "Convert file and directory names to upper case (FiLe -> FILE)" },
    { preserve_directories, "Do not alter directory structure" },
    { single_directory, "Move files from all sub directories to the mods root directory" },
    { no_extract,
      "Deploy the archive file itself instead of its contents (e.g. Doom .pk3/.pk4)" }
  };
  /*! \brief Simply extracts files */
  inline static const std::string SIMPLEINSTALLER{ "Simple Installer" };
  /*!
   * \brief Takes a vector of files created by fomod::FomodInstaller and
   * moves them to their target.
   */
  inline static const std::string FOMODINSTALLER{ "Fomod Installer" };
  /*!
   * \brief Contains all available installer types.
   */
  inline static const std::vector<std::string> INSTALLER_TYPES{ SIMPLEINSTALLER, FOMODINSTALLER };

  /*!
  * \brief Checks if the given path is a supported archive filetype
  * \param source_path Path to the file.
  * \return bool true if supported archive file, otherwise false
  */
  static bool sourceIsArchive(const std::filesystem::path& source_path);

  /*!
   * \brief Extracts the given archive to the given directory.
   * \param source Path to the archive.
   * \param destination Destination directory for extraction.
   * \param progress_node Used to inform about extraction progress.
   * \return Int indicating success(0), a filesystem error(-2) or an error
   * during extraction(-1).
   */
  static void extract(const std::filesystem::path& source,
                      const std::filesystem::path& destination,
                      std::optional<ProgressNode*> progress_node = {});
  /*!
   * \brief Extracts the archive, performs any actions specified by the installer type,
   * then copies all files to given destination.
   * \param path Path to the archive.
   * \param destination Destination directory for the installation.
   * \param options Sum of installation flags
   * \param installer Installer type to use.
   * \param root_level If > 0: Ignore all mod files and path components with depth <
   * root_level.
   * \param selected_files If non-empty: Only install the archive entries whose
   * relative path (as returned by \ref getArchiveFileNames) is contained in this
   * set, plus the children of any selected directory. All other extracted files
   * are discarded before the move pipeline runs. An empty set installs everything
   * (unchanged default behavior). Paths are matched against the archive layout
   * before any root_level stripping.
   * \return The total file size of the installed mod on disk.
   */
  static unsigned long install(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    int options,
    const std::string& type = SIMPLEINSTALLER,
    int root_level = 0,
    const std::vector<std::pair<std::filesystem::path, std::filesystem::path>> fomod_files = {},
    const std::set<std::filesystem::path>& selected_files = {});
  /*!
   * \brief Installs the given source as a patch/upgrade by overlaying its files onto an
   * existing mod's staging directory, instead of creating a new mod.
   *
   * The source is extracted to a temporary directory (reusing the same extraction
   * pipeline as \ref install), the requested name/structure options are applied, and the
   * resulting files are merged recursively into \p existing_mod_dir. Files present in the
   * patch overwrite files of the same relative path in the existing mod; files in the
   * existing mod that are not part of the patch are left untouched.
   * \param source Path to the archive (or directory) containing the patch.
   * \param existing_mod_dir Staging directory of the mod to patch. Must already exist.
   * \param options Sum of installation flags (case and single_directory are honored).
   * \param root_level If > 0: Ignore all patch files and path components with depth <
   * root_level before overlaying.
   * \return The total file size of the patched mod on disk after the overlay.
   */
  static unsigned long installPatch(const std::filesystem::path& source,
                                    const std::filesystem::path& existing_mod_dir,
                                    int options,
                                    int root_level = 0);
  /*!
   * \brief Uninstalls the mod at given directory using the given installer type.
   * \param path Path to the mod.
   * \param installer Installer type to use.
   */
  static void uninstall(const std::filesystem::path& mod_path,
                        const std::string& type = SIMPLEINSTALLER);
  /*!
   * \brief Recursively reads all file and directory names from given archive.
   * \param path Path to given archive.
   * \return Vector of paths within the archive and bools indicating whether that path points to
   * a directory.
   */
  static std::vector<std::pair<std::filesystem::path, bool>> getArchiveFileNames(
    const std::filesystem::path& path);
  /*!
   * \brief Identifies the appropriate installer type from given source archive or
   * directory.
   * \param source Path to mod source.
   * \return Required root level and type of the installer.
   */
  static std::tuple<int, std::string, std::string> detectInstallerSignature(
    const std::filesystem::path& source);
  /*!
   * \brief Deletes all temporary files created during a previous installation attempt.
   * \param staging_dir Directory containing temporary files.
   * \param mod_id Id of the mod whose installation failed.
   */
  static void cleanupFailedInstallation(const std::filesystem::path& staging_dir, int mod_id);
  /*!
   * \brief Sets whether this application is running as a flatpak.
   * \param is_a_flatpak If true: The application is running as a flatpak.
   */
  static void setIsAFlatpak(bool is_a_flatpak);
  /*! \brief Callback for logging. */
  static inline std::function<void(Log::LogLevel, const std::string&)> log =
    [](Log::LogLevel a, const std::string& b) {};

private:
  /*! \brief Directory name used to temporary storage of files during installation. */
  static inline std::string EXTRACT_TMP_DIR = "lmm_tmp_extract";
  /*! \brief Extension used for temporary storage during file movement. */
  static inline std::string MOVE_EXTENSION = "tmpmove";
  /*! \brief If true: The application is running as a flatpak. */
  static inline bool is_a_flatpak_ = false;
  /*!
   * \brief Name of the extraction cache directory (created under the system temp
   * directory). Holds a verbatim extraction of recently installed archives so
   * reinstalls can be populated without re-running libarchive.
   */
  static inline std::string EXTRACT_CACHE_DIR = "limo_extract_cache";
  /*! \brief File name of the marker storing the cached archive's identity. */
  static inline std::string CACHE_MARKER_FILE = ".limo_cache_marker";
  /*! \brief Directory name holding the cached extraction payload. */
  static inline std::string CACHE_PAYLOAD_DIR = "payload";

  /*!
   * \brief Computes a stable cache key for the given archive based on its
   * canonical path, file size and last write time.
   * \param source Path to the archive.
   * \return The cache key string, or an empty optional if it can not be derived
   * (e.g. source is a directory or stat fails).
   */
  static std::optional<std::string> computeCacheKey(const std::filesystem::path& source);
  /*!
   * \brief Returns the cache entry directory for the given key.
   * \param key Cache key as produced by \ref computeCacheKey.
   */
  static std::filesystem::path cacheEntryPath(const std::string& key);
  /*!
   * \brief Attempts to populate dest_path from a valid cached extraction of the
   * given archive. Files are hard-linked where possible, falling back to a copy
   * across file system boundaries.
   * \param source Path to the archive.
   * \param dest_path Directory to populate (created if missing).
   * \return True if dest_path was fully populated from the cache, false if no
   * valid cache exists or population failed (caller should extract normally).
   */
  static bool populateFromCache(const std::filesystem::path& source,
                                const std::filesystem::path& dest_path);
  /*!
   * \brief Stores a verbatim extraction of the given archive in the cache so
   * later reinstalls can be served from it. Failures are non-fatal and ignored.
   * \param source Path to the archive.
   * \param extracted_path Directory containing the verbatim extraction.
   */
  static void storeInCache(const std::filesystem::path& source,
                           const std::filesystem::path& extracted_path);
  /*!
   * \brief Recursively recreates the directory tree of src in dst, hard-linking
   * regular files and falling back to a copy when hard-linking fails (e.g.
   * across file systems). Throws on unrecoverable errors.
   * \param src Source directory.
   * \param dst Destination directory.
   */
  static void hardLinkOrCopyTree(const std::filesystem::path& src,
                                 const std::filesystem::path& dst);

  /*!
   * \brief Throws a CompressionError containing the error message of given archive.
   * \param source Archive containing the error message.
   */
  static void throwCompressionError(struct archive* source);
  /*!
   * \brief Copies data from given source archive to given destination archive.
   * Throws CompressionError when an reading or writing fails.
   * \param source Source archive.
   * \param dest Destination archive.
   */
  static void copyArchive(struct archive* source, struct archive* dest);

  /*!
   * \brief Extracts the given archive to the given directory. Informs about
   * extraction progress using the provided node.
   * \param source_path Path to the archive.
   * \param dest_path Destination directory for extraction.
   * \param progress_node Used to inform about extraction progress.
   */
  static void extractWithProgress(const std::filesystem::path& source_path,
                                  const std::filesystem::path& dest_path,
                                  std::optional<ProgressNode*> progress_node = {});
  /*!
   * \brief Libarchive sometime fails to extract certain rar archives when
   * using the method implemented in \ref extractWithProgress. This function
   * uses libunrar instead of libarchive to extract a given rar archive.
   * \param source_path Path to the archive.
   * \param dest_path Destination directory for extraction.
   */
#ifdef LIMO_WITH_UNRAR
  static void extractRarArchive(const std::filesystem::path& source_path,
                                const std::filesystem::path& dest_path);
#endif

  /*!
   * \brief Returns true if the given path has the ".omod" extension (case
   * insensitive).
   * \param source_path Path to check.
   */
  static bool sourceIsOmod(const std::filesystem::path& source_path);
  /*!
   * \brief Best-effort extraction of an Oblivion Mod Manager (.omod) archive.
   *
   * An .omod is a 7-zip container holding member streams (config, data,
   * data.crc, plugins, plugins.crc, ...). The 'data'/'plugins' members are
   * single blobs containing all mod files concatenated and compressed with
   * either zlib (deflate) or 7-zip/LZMA, as indicated by the 'config' member.
   * The accompanying '*.crc' members list the contained files and their sizes.
   *
   * This function extracts the actual mod files into dest_path. The OBMM
   * install script (the 'script' member) is intentionally ignored, as
   * executing OBMM scripts would require a full OBMM scripting interpreter
   * which is out of scope. Malformed input never crashes: it logs and throws
   * a CompressionError instead.
   * \param source_path Path to the .omod file.
   * \param dest_path Destination directory for the extracted mod files.
   */
  static void extractOmodArchive(const std::filesystem::path& source_path,
                                 const std::filesystem::path& dest_path);
  /*!
   * \brief Reads a single named member stream out of a 7-zip/zip container
   * into a byte buffer using libarchive.
   * \param archive_path Path to the container.
   * \param member_name Name of the member to read (case insensitive).
   * \param out_data Receives the raw member bytes on success.
   * \return True if the member was found and read.
   */
  static bool readOmodMember(const std::filesystem::path& archive_path,
                             const std::string& member_name,
                             std::vector<unsigned char>& out_data);
  /*!
   * \brief Inflates a zlib (deflate) compressed buffer.
   * \param input Compressed input bytes.
   * \param output Receives the decompressed bytes.
   * \return True on success.
   */
  static bool inflateZlibBuffer(const std::vector<unsigned char>& input,
                                std::vector<unsigned char>& output);
  /*!
   * \brief Decompresses an in-memory buffer using libarchive (used for the
   * 7-zip/LZMA compressed OMOD data blob).
   * \param input Compressed input bytes.
   * \param output Receives the decompressed bytes.
   * \return True on success.
   */
  static bool decompressBufferWithLibarchive(const std::vector<unsigned char>& input,
                                             std::vector<unsigned char>& output);
  /*!
   * \brief Removes everything below the given extraction directory that is not part of
   * the given selection. A path is kept if it is itself selected, if it is a descendant
   * of a selected directory, or if it is an ancestor directory of a selected path (so the
   * selected entry remains reachable). All other files and directories are deleted.
   * \param extract_dir Directory containing the freshly extracted archive.
   * \param selected_files Set of archive-relative paths to keep.
   */
  static void pruneToSelection(const std::filesystem::path& extract_dir,
                               const std::set<std::filesystem::path>& selected_files);
};
