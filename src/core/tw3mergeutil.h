/*!
 * \file tw3mergeutil.h
 * \brief Header for the Tw3MergeUtil namespace.
 *
 * \details
 * This module implements an MVP "merge pass" for The Witcher 3 (next-gen). Many mods ship
 * additions to the game's *shared* user configuration files instead of providing standalone
 * files. Because these files are shared, naively deploying multiple mods causes the
 * last-deployed mod to win: every other mod's additions are lost. The classic remedy
 * (popularised by tools such as Script Merger / TW3 Mod Manager) is to *merge* every mod's
 * additions into the single shared file. This namespace performs that merge for the two most
 * common cases:
 *
 *   - \c input.xml         : keybind / action definitions, structured XML
 *                            (\c <InputContext> / \c <Action> / \c <Mapping> / \c <Button>).
 *   - \c user.settings /
 *     \c input.settings    : flat INI-like \c key=value entries grouped under \c [Section]
 *                            headers.
 *
 * The shared user config for TW3 next-gen (running through Proton) lives at:
 *
 *   \c <game>/bin/config/r4game/user_config_matrix/pc/
 *
 * with \c input.xml alongside \c dx12filelist.txt / \c dx11filelist.txt (which list the XML
 * files the game loads). This module only touches \c input.xml and the \c *.settings files;
 * it does not modify the filelist files.
 *
 * \par Idempotency and reversibility
 * Every region this module inserts is wrapped in sentinel comment markers of the form
 * \verbatim <!-- LIMO_MERGE_BEGIN modid=N --> ... <!-- LIMO_MERGE_END modid=N --> \endverbatim
 * (for the \c .settings files the markers are line comments using \c ; instead). Re-running a
 * merge first strips all previously inserted LIMO_MERGE regions for the mods being processed,
 * then re-inserts fresh ones. This makes the merge:
 *   - \b idempotent  : running it twice yields the same file as running it once;
 *   - \b reversible  : the markers delimit exactly what Limo added, so a future "unmerge"
 *                      can remove them without disturbing hand-edited or vanilla content.
 *
 * \warning This module was authored without the ability to run against an installed copy of
 * The Witcher 3. Every behavioural assumption about the real on-disk format is flagged in the
 * implementation with a \c "// TODO(tw3-merge):" comment and must be validated in-game before
 * this is relied upon. See the report accompanying this change for the full list.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>


/*!
 * \brief Functions for merging per-mod The Witcher 3 configuration fragments into the game's
 * shared config files (\c input.xml and the \c *.settings files), idempotently and reversibly.
 */
namespace Tw3MergeUtil
{
/*!
 * \brief Relative path, from the game root, to the directory holding the shared user config.
 * \details TW3 next-gen reads merged user config from this directory. Used to locate
 * \c input.xml when only the game root is supplied.
 */
inline const std::filesystem::path USER_CONFIG_REL_DIR =
  std::filesystem::path("bin") / "config" / "r4game" / "user_config_matrix" / "pc";

/*! \brief File name of the shared keybind/action config. */
inline constexpr const char* INPUT_XML_NAME = "input.xml";

/*!
 * \brief Describes one mod whose configuration fragments should be merged.
 * \details A source directory is searched (non-recursively first, then one level deep) for the
 * relevant fragment file (\c input.xml / \c user.settings / \c input.settings). The \c mod_id is
 * embedded into the LIMO_MERGE markers so each mod's contribution can be replaced independently.
 */
struct MergeSource
{
  /*! \brief Stable, unique id for this mod. Embedded into the \c modid=N sentinel markers. */
  int mod_id = 0;
  /*! \brief Directory containing the mod's installed files (e.g. a Limo Mods/<id> directory). */
  std::filesystem::path source_path;
};

/*!
 * \brief Outcome of a single merge operation, suitable for logging and reporting to the caller.
 */
struct MergeResult
{
  /*! \brief True if the merge completed without a fatal error (file may still be unchanged). */
  bool success = false;
  /*! \brief True if the target file's contents were actually changed and rewritten to disk. */
  bool changed = false;
  /*! \brief Number of mods whose fragment contributed at least one entry to the target. */
  int mods_merged = 0;
  /*! \brief Number of individual entries (actions / key=value pairs) inserted across all mods. */
  int entries_merged = 0;
  /*! \brief Human-readable summary or error description. */
  std::string message;
};

/*!
 * \brief Merges each mod's \c input.xml fragment into the game's shared \c input.xml.
 *
 * \details For every source in \p sources this function locates the mod's \c input.xml fragment
 * (see \ref findFragment), parses its \c <InputContext> sections, and inserts each contained
 * \c <Action> (or other binding child) into the matching \c <InputContext> of the target
 * \c <game>/bin/config/r4game/user_config_matrix/pc/input.xml. Behaviour:
 *   - Sections present in a fragment but absent from the target are created.
 *   - Entries already present in the target (matched structurally, see implementation) are
 *     skipped, so the merge is de-duplicating.
 *   - Inserted entries for mod \c N are wrapped between
 *     \c <!--\ LIMO_MERGE_BEGIN\ modid=N\ --> and \c <!--\ LIMO_MERGE_END\ modid=N\ -->.
 *   - Any LIMO_MERGE region for a mod that appears in \p sources is removed before re-inserting,
 *     making repeated calls idempotent.
 *
 * The target file is created (with a minimal root) if it does not yet exist.
 *
 * \param game_root Path to the game's root directory (the one containing \c bin/). The target
 * file is resolved as \p game_root / \ref USER_CONFIG_REL_DIR / \ref INPUT_XML_NAME.
 * \param sources Mods to merge, in load order (earlier entries are inserted first). The
 * \c mod_id of each is used for the sentinel markers.
 * \param dry_run If true, all parsing and merging is performed but nothing is written to disk;
 * the returned \ref MergeResult still reports what *would* change. Useful for previews/tests.
 * \return A \ref MergeResult describing the outcome. On any unrecoverable error \c success is
 * false and \c message explains why; the on-disk file is left untouched in that case.
 */
MergeResult mergeInputXml(const std::filesystem::path& game_root,
                          const std::vector<MergeSource>& sources,
                          bool dry_run = false);

/*!
 * \brief Merges each mod's \c *.settings (\c key=value) fragment into a shared settings file.
 *
 * \details The TW3 \c user.settings and \c input.settings files are flat INI-like files: lines
 * of \c key=value grouped under \c [Section] headers. For every source this function locates the
 * fragment named \p settings_file_name, parses its sections, and appends any \c key that is not
 * already present under the corresponding \c [Section] of the target file. As with
 * \ref mergeInputXml, inserted lines are wrapped in sentinel markers (line comments, e.g.
 * \c ;\ LIMO_MERGE_BEGIN\ modid=N) and prior regions for the same mods are replaced, so the
 * operation is idempotent and reversible.
 *
 * \note TW3 \c .settings files live in the user's "Documents" tree (under the Proton prefix), not
 * inside the game root. Because that location varies per install, this function takes the
 * \p settings_dir explicitly rather than deriving it from the game root.
 *
 * \param settings_dir Directory containing the target settings file.
 * \param settings_file_name Name of the settings file, e.g. \c "user.settings" or
 * \c "input.settings".
 * \param sources Mods to merge, in load order. Each mod's fragment must share the
 * \p settings_file_name.
 * \param dry_run If true, compute the result but do not write to disk.
 * \return A \ref MergeResult describing the outcome.
 */
MergeResult mergeSettingsFile(const std::filesystem::path& settings_dir,
                              const std::string& settings_file_name,
                              const std::vector<MergeSource>& sources,
                              bool dry_run = false);

/*!
 * \brief Locates a named configuration fragment inside a mod's source directory.
 *
 * \details Searches for \p file_name first directly in \p source_path, then (if not found) one
 * directory level deep. This shallow search keeps the MVP conservative and fast while still
 * coping with the common case where a mod archive nests its files in a single top-level folder.
 *
 * \param source_path Directory to search.
 * \param file_name Name of the fragment to find (e.g. \c "input.xml").
 * \return The path to the first match, or an empty path if none was found.
 *
 * \todo TODO(tw3-merge): real mods may nest config files arbitrarily deep, or under a
 * \c bin/config/... subtree mirroring the game layout. The depth-1 search is a deliberate MVP
 * simplification and should be validated against real mod packaging.
 */
std::filesystem::path findFragment(const std::filesystem::path& source_path,
                                   const std::string& file_name);

/*!
 * \brief Builds the begin-marker payload for a given mod id, e.g. \c "LIMO_MERGE_BEGIN modid=5".
 * \param mod_id Mod id to embed.
 * \return The marker payload, without comment delimiters.
 */
std::string beginMarker(int mod_id);
/*!
 * \brief Builds the end-marker payload for a given mod id, e.g. \c "LIMO_MERGE_END modid=5".
 * \param mod_id Mod id to embed.
 * \return The marker payload, without comment delimiters.
 */
std::string endMarker(int mod_id);
}
