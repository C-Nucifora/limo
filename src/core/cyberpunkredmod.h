/*!
 * \file cyberpunkredmod.h
 * \brief Header for the cyberpunk_redmod namespace.
 *
 * Pure, UI-agnostic core logic for handling Cyberpunk 2077 REDmods (CD Projekt
 * Red's official mod framework). None of these functions depend on Qt; they only
 * touch the filesystem, jsoncpp and the standard library so they can be reused by
 * any front end or by an automated test harness.
 *
 * A REDmod is a folder \c mods/<modname>/ inside the game root. It contains an
 * \c info.json describing the mod and one or more content subdirectories
 * (\c archives/, \c scripts/, \c tweaks/, \c customSounds/). Before the game can
 * use them, REDmods must be "deployed": running
 * \c <game>/tools/redmod/bin/redMod.exe \c deploy scans \c mods/, builds combined
 * archives into \c archive/pc/mod/ and writes a load order file. On Linux that
 * Windows executable has to run inside the game's Proton prefix.
 *
 * \warning Every assumption about the on-disk \c info.json schema, the
 * \c redMod.exe command line and the Proton invocation is marked with a
 * \c TODO(cp-redmod): comment and must be validated against a real Cyberpunk 2077
 * installation. See \ref cyberpunk_redmod::redmodDeployCommand for details.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>


/*!
 * \brief Contains UI-agnostic helpers for detecting, laying out and deploying
 * Cyberpunk 2077 REDmods.
 */
namespace cyberpunk_redmod
{
/*!
 * \brief Steam app id of Cyberpunk 2077.
 *
 * Used to locate the Proton prefix when building the deploy command via
 * protontricks.
 * TODO(cp-redmod): Confirm this is the correct app id for the user's installation
 * (GOG / Epic builds are not Steam apps and would need a different prefix lookup).
 */
constexpr int CYBERPUNK_STEAM_APP_ID = 1091500;

/*!
 * \brief Name of the metadata file that identifies a REDmod folder.
 * TODO(cp-redmod): Confirm the file is always named exactly "info.json"
 * (lower case) across game versions.
 */
constexpr char REDMOD_INFO_FILE_NAME[] = "info.json";

/*!
 * \brief Directory (relative to the game root) into which REDmods are placed.
 */
constexpr char REDMOD_MODS_DIR[] = "mods";

/*!
 * \brief Relative path to the REDmod deploy tool inside the game root.
 * TODO(cp-redmod): Confirm this path; it has been stable but could change with
 * future game updates.
 */
constexpr char REDMOD_TOOL_RELATIVE_PATH[] = "tools/redmod/bin/redMod.exe";

/*!
 * \brief Name of the load order file written into the \c mods directory.
 *
 * The REDmod toolchain reads this file to determine the order in which mods are
 * combined during deployment.
 * TODO(cp-redmod): Confirm the current file name and schema. Historically the
 * load order has been stored in \c mods/MODS.json with an \c "enabledMods" array.
 */
constexpr char REDMOD_LOAD_ORDER_FILE_NAME[] = "MODS.json";

/*!
 * \brief The content types a REDmod can provide.
 *
 * These map to the optional sections in \c info.json and to the corresponding
 * content subdirectories of the mod folder. They are stored as flags on
 * \ref RedMod so a front end can show, e.g., which mods contain scripts.
 */
enum RedModContentType
{
  /*! \brief Contains an \c archives/ subdirectory with \c .archive files. */
  archives = 1 << 0,
  /*! \brief Declares a \c customSounds section / contains a \c customSounds/ dir. */
  custom_sounds = 1 << 1,
  /*! \brief Declares a \c scripts section / contains a \c scripts/ dir. */
  scripts = 1 << 2,
  /*! \brief Declares a \c tweaks section / contains a \c tweaks/ dir. */
  tweaks = 1 << 3
};

/*!
 * \brief Describes a single REDmod discovered on disk.
 */
struct RedMod
{
  /*!
   * \brief The mod's name as declared by the \c name field of \c info.json.
   *
   * This is the identifier passed to \c redMod.exe via \c -mod=<name> and is
   * used as the destination folder name under \c mods/.
   */
  std::string name;
  /*! \brief The mod's version as declared by the \c version field of \c info.json. */
  std::string version;
  /*!
   * \brief Absolute path to the source folder containing the mod's \c info.json.
   *
   * This is the folder that gets copied into \c game_root/mods/<name>/.
   */
  std::filesystem::path source_path;
  /*!
   * \brief Bitwise-or of \ref RedModContentType flags describing which content
   * types this mod provides.
   */
  int content_types = 0;

  /*!
   * \brief Convenience check for a single content type.
   * \param type The content type to test for.
   * \return True if the corresponding flag is set in \ref content_types.
   */
  bool hasContent(RedModContentType type) const;
};

/*!
 * \brief Scans the given directory for REDmods.
 *
 * Every immediate subdirectory of \p source_dir that contains a readable, valid
 * \c info.json (with at least a non-empty \c name field) is reported as a
 * \ref RedMod. Subdirectories without an \c info.json, or with one that cannot be
 * parsed, are skipped silently so that a mixed directory does not abort detection.
 *
 * \param source_dir Directory to scan. Typically a staging directory containing
 * extracted mod archives, but it may also be the game's \c mods directory.
 * \return One \ref RedMod per detected mod, in unspecified order.
 *
 * TODO(cp-redmod): Only the immediate children of \p source_dir are scanned. Some
 * downloaded archives wrap the actual REDmod in an extra folder
 * (e.g. \c MyMod/mods/MyMod/info.json); decide during integration whether to
 * recurse or to rely on Limo's installer to flatten that first.
 */
std::vector<RedMod> detectRedMods(const std::filesystem::path& source_dir);

/*!
 * \brief Parses a single REDmod folder into a \ref RedMod.
 *
 * Reads \c <mod_dir>/info.json, extracts the \c name and \c version fields and
 * derives the content type flags from both the \c info.json sections and the
 * subdirectories present on disk.
 *
 * \param mod_dir Folder expected to contain an \c info.json.
 * \return The parsed \ref RedMod on success, or an empty optional if the folder
 * does not contain a readable, valid \c info.json.
 *
 * TODO(cp-redmod): The mapping from \c info.json keys (\c customSounds,
 * \c scripts, \c tweaks) to content types is based on the documented schema and
 * needs to be checked against real mods, which sometimes omit sections that are
 * nevertheless present as folders.
 */
std::optional<RedMod> parseRedMod(const std::filesystem::path& mod_dir);

/*!
 * \brief Copies the given REDmods into \c game_root/mods/<name>/ in load order.
 *
 * This performs the first half of "deployment": placing each mod's files under
 * the game's \c mods directory. It does \e not run \c redMod.exe; building the
 * combined archives under \c archive/pc/mod/ is done separately via the command
 * returned by \ref redmodDeployCommand.
 *
 * Any existing \c game_root/mods/<name>/ directory for a deployed mod is removed
 * first so that re-deploying does not leave stale files behind. The
 * \c game_root/mods directory is created if it does not yet exist. After copying,
 * a load order file is written via \ref writeLoadOrderFile.
 *
 * \param mods_in_load_order Pairs of (mod id, source path) in the desired load
 * order. The \c int is an opaque caller-side identifier (e.g. Limo's mod id); it
 * is not used by this function beyond preserving order, so callers may pass any
 * value. Each path must point at a folder containing a valid \c info.json.
 * \param game_root Absolute path to the Cyberpunk 2077 installation root (the
 * directory that contains \c bin, \c archive, \c mods and \c tools).
 *
 * \throws std::filesystem::filesystem_error on copy/remove failures.
 *
 * TODO(cp-redmod): Decide whether mods should be hard-linked instead of copied to
 * save space; this MVP always copies. Limo's existing Deployer infrastructure may
 * supersede this function entirely (see the integration notes in the header docs).
 */
void layoutRedMods(const std::vector<std::pair<int, std::filesystem::path>>& mods_in_load_order,
                   const std::filesystem::path& game_root);

/*!
 * \brief Writes the REDmod load order file into \c game_root/mods.
 *
 * Produces \c game_root/mods/MODS.json listing the enabled mods in order.
 *
 * \param mod_names_in_order Mod names (the \c name field / folder name) in load
 * order.
 * \param game_root Absolute path to the Cyberpunk 2077 installation root.
 *
 * \throws std::runtime_error if the file cannot be opened for writing.
 *
 * TODO(cp-redmod): The exact schema is assumed to be
 * \c {"mods":[{"folder":"<name>","enabled":true,"deployable":true}]}. The
 * historical format used an \c "enabledMods" string array instead. Verify which
 * the installed game expects and adjust \ref writeLoadOrderFile accordingly. When
 * passing the order explicitly to \c redMod.exe via repeated \c -mod= arguments,
 * this file may be unnecessary, but the launcher/game still reads it to decide
 * which mods are active.
 */
void writeLoadOrderFile(const std::vector<std::string>& mod_names_in_order,
                        const std::filesystem::path& game_root);

/*!
 * \brief Builds the shell command that runs \c redMod.exe \c deploy under Proton.
 *
 * The returned string is a complete shell command (suitable for the same
 * execution path Limo uses for its \ref Tool commands) that invokes
 * \c tools/redmod/bin/redMod.exe with the \c deploy verb inside the game's Proton
 * prefix. The mods are passed in the given order via repeated \c -mod=<name>
 * arguments so the deploy tool builds the load order deterministically.
 *
 * <b>Exact command assumed (all parts flagged for validation):</b>
 * \code
 *   protontricks-launch --appid 1091500 \
 *     "<game_root>/tools/redmod/bin/redMod.exe" deploy \
 *     -root="<game_root>" \
 *     -mod=<name0> -mod=<name1> ...
 * \endcode
 *
 * Assumptions:
 * - \c protontricks-launch is the chosen Proton bridge, mirroring Limo's existing
 *   \ref Tool protontricks runtime. \p proton_prefix is therefore \e not placed on
 *   the command line directly when protontricks is used (protontricks derives the
 *   prefix from the app id); it is retained in the signature so an alternative
 *   implementation that calls the Steam compat tool's \c proton binary with
 *   \c STEAM_COMPAT_DATA_PATH=<proton_prefix> can use it. When \p proton_prefix is
 *   non-empty it is exported as \c STEAM_COMPAT_DATA_PATH so either bridge sees a
 *   consistent value.
 * - The verb is the literal \c deploy.
 * - \c redMod.exe accepts \c -root=<path> to point at the game install and
 *   repeated \c -mod=<name> arguments to set the load order. CDPR's launcher uses
 *   this tool internally; the precise flag spelling is the riskiest part of this
 *   module.
 * - Paths are passed as native (Linux) paths. redMod.exe running under Proton
 *   generally resolves these via the Z: drive mapping, but if it requires Windows
 *   paths this must be converted (e.g. via \c winepath) during validation.
 *
 * \param game_root Absolute path to the Cyberpunk 2077 installation root.
 * \param mod_names_in_order Mod names in the desired load order.
 * \param proton_prefix Path to the game's Proton/compatdata prefix
 * (e.g. \c .../steamapps/compatdata/1091500/pfx or its parent). May be empty when
 * relying solely on protontricks' app-id lookup.
 * \return The shell command string.
 *
 * TODO(cp-redmod): Validate the full command line against a real install. In
 * particular confirm: the flag names (\c -root / \c -mod), whether a trailing
 * \c -force or \c -reportProgress style flag is needed, and whether native Linux
 * paths work or must be converted to Windows paths.
 */
std::string redmodDeployCommand(const std::filesystem::path& game_root,
                                const std::vector<std::string>& mod_names_in_order,
                                const std::filesystem::path& proton_prefix);
} // namespace cyberpunk_redmod
