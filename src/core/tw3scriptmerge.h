/*!
 * \file tw3scriptmerge.h
 * \brief Header for the tw3_script_merge namespace.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


/*!
 * \brief Contains functions for merging conflicting WitcherScript (\c .ws) files of
 * The Witcher 3 mods.
 *
 * The Witcher 3 mods ship script files under \c <mod>/content/scripts/\**\/\*.ws, mirroring
 * the game's own \c content/content0/scripts/ tree. When two enabled mods modify the SAME
 * script path, the game only loads the last one in load order, silently discarding the
 * changes of every other mod that touches that file. This namespace re-implements the core
 * idea of the community "Script Merger" / w3scriptmerge tool: it detects every script path
 * that is present in more than one mod and produces a single merged file for each, written
 * into a dedicated, high-priority merged-scripts mod folder (conventionally something like
 * \c mod0000_MergedScripts/content/scripts/...).
 *
 * The merge itself is a line-based 3-way merge (diff3 style) when the vanilla game script is
 * available as a common base, falling back to a 2-way merge between two mod versions when it
 * is not. See \ref tw3_script_merge::mergeScripts for details and limitations.
 *
 * \note This module is intentionally self-contained: it depends only on the C++ standard
 * library (plus the project's \ref Log facility) and performs no game-specific parsing of the
 * WitcherScript language. Conflicts are resolved purely on a textual, per-line basis, exactly
 * like the reference tool's text merge.
 */
namespace tw3_script_merge
{
/*!
 * \brief Describes the outcome of a \ref mergeScripts run.
 */
struct MergeResult
{
  /*!
   * \brief Number of script paths for which a merged file was written.
   *
   * This counts every script present in more than one mod, regardless of whether the merge
   * was clean or contained conflicts.
   */
  int scripts_merged = 0;
  /*!
   * \brief Number of script paths whose conflicting changes could be merged automatically
   * (i.e. no overlapping hunks, so the resulting file contains no conflict markers).
   */
  int conflicts_auto_resolved = 0;
  /*!
   * \brief Number of script paths whose merged file still contains conflict markers and
   * therefore requires manual resolution by the user.
   */
  int conflicts_unresolved = 0;
  /*!
   * \brief Relative script paths (relative to \c content/scripts, using '/' separators) of
   * every merged file that still contains conflict markers.
   *
   * \sa conflicts_unresolved
   */
  std::vector<std::string> unresolved_paths = {};
  /*!
   * \brief Absolute paths of every merged file that was written, in the same order they were
   * processed. Useful for logging or for presenting the results in a UI.
   */
  std::vector<std::filesystem::path> written_files = {};
};

/*!
 * \brief Describes the outcome of merging the contents of a single script file.
 *
 * This is the lower-level result produced by \ref mergeTwoWay and \ref mergeThreeWay; it is
 * exposed so callers (and tests) can merge raw text without touching the filesystem.
 */
struct TextMergeResult
{
  /*! \brief The merged text, including any conflict markers. */
  std::string merged_text;
  /*! \brief True if \ref merged_text contains at least one conflict region. */
  bool has_conflicts = false;
};

/*! \brief Default name of the conflict marker introducing the "ours" / first-mod side. */
inline constexpr std::string_view DEFAULT_OURS_LABEL = "ours";
/*! \brief Default name of the conflict marker introducing the "theirs" / second-mod side. */
inline constexpr std::string_view DEFAULT_THEIRS_LABEL = "theirs";

/*!
 * \brief Relative directory, below a mod's root, in which WitcherScript files live.
 *
 * \details Reference layout: \c <mod>/content/scripts/\**\/\*.ws.
 * \note TODO(tw3-scriptmerge): Confirm against real mods. Some mods nest scripts one level
 * deeper (e.g. under a \c modName/content/... directory) or ship a flattened \c scripts/
 * folder without the leading \c content/. If such layouts must be supported, this prefix and
 * \ref findScriptFiles need to grow additional fallbacks.
 */
inline constexpr std::string_view SCRIPTS_SUBDIR = "content/scripts";

/*!
 * \brief File extension (lower case, including the dot) identifying WitcherScript files.
 */
inline constexpr std::string_view SCRIPT_EXTENSION = ".ws";

/*!
 * \brief Detects and merges conflicting WitcherScript files across the given mods.
 *
 * For every relative script path (under \ref SCRIPTS_SUBDIR) that exists in more than one of
 * the given mods, a single merged file is produced under
 * \c output_dir/content/scripts/<relative path>. Scripts present in only one mod are ignored,
 * since the game handles those correctly on its own.
 *
 * Merge strategy:
 * - If \p vanilla_scripts_root is given and contains the script in question, a 3-way merge is
 *   performed (see \ref mergeThreeWay): the vanilla file is the common base, and each pair of
 *   mods is merged against it. Non-overlapping changes from both mods are combined
 *   automatically; truly overlapping (conflicting) changes are wrapped in standard conflict
 *   markers and the path is recorded in \ref MergeResult::unresolved_paths.
 * - Otherwise a 2-way merge is performed (see \ref mergeTwoWay): differing regions between the
 *   two mod versions are wrapped in conflict markers. A 2-way merge cannot tell which side
 *   actually changed relative to vanilla, so it is more likely to report conflicts.
 *
 * When three or more mods modify the same script, they are merged pairwise in load order: the
 * result of merging the first two becomes the "ours" side for the third, and so on (a left
 * fold). The vanilla file remains the common base for every step.
 * \note TODO(tw3-scriptmerge): This pairwise fold matches the reference tool's general
 * approach but is not associative once conflict markers appear; a conflict introduced early
 * is treated as opaque text by later merges. Validate the behaviour with 3+ mods touching one
 * script and consider surfacing the participating mod ids in the result.
 *
 * The operation is idempotent: \p output_dir's \c content/scripts subtree is cleared at the
 * start of each run, so re-running with the same inputs regenerates byte-identical output.
 * \note TODO(tw3-scriptmerge): Only the \c content/scripts subtree of \p output_dir is
 * cleared. Any other files the merged-scripts mod might need (e.g. an \c xml or \c bundle
 * folder) are left untouched. Confirm this matches how the merged-scripts mod is packaged.
 *
 * \param mods_in_load_order Mods to consider, each as a pair of (mod id, absolute path to the
 * mod's root directory), ordered from lowest to highest load priority. The mod id is used
 * only for diagnostics / conflict marker labels; the path is where scripts are searched.
 * \param output_dir Absolute path to the merged-scripts mod's root directory. Merged files are
 * written below \c output_dir/content/scripts. Created if it does not exist.
 * \param vanilla_scripts_root Optional absolute path to the root of the unpacked vanilla game
 * scripts. This may point either directly at a \c scripts directory or at a directory that
 * contains \ref SCRIPTS_SUBDIR; both are probed. When omitted (or when a given script is not
 * found there), the merge for that script falls back to 2-way.
 * \note TODO(tw3-scriptmerge): The vanilla scripts normally have to be extracted from the
 * game's \c content0 bundles (e.g. via wcc_lite / QuickBMS) before they exist as loose \c .ws
 * files. This function does NOT perform that extraction; the caller must supply an
 * already-unpacked tree. Document this requirement in the UI.
 * \return A \ref MergeResult summarising what was merged and which paths need manual attention.
 * \note TODO(tw3-scriptmerge): Encoding and line endings. Files are read and written as raw
 * bytes and split on '\\n' (a trailing '\\r' is preserved as part of the line), so mixed CRLF/LF
 * content round-trips but is compared verbatim. WitcherScript files are commonly UTF-8 or
 * Windows-1252; no transcoding is attempted. Verify the game accepts the byte-for-byte output.
 */
MergeResult mergeScripts(
  const std::vector<std::pair<int, std::filesystem::path>>& mods_in_load_order,
  const std::filesystem::path& output_dir,
  const std::optional<std::filesystem::path>& vanilla_scripts_root = {});

/*!
 * \brief Performs a line-based 3-way merge of two versions of a file against a common base.
 *
 * Implements a diff3-style merge: both \p ours and \p theirs are diffed against \p base using
 * a longest-common-subsequence (Myers) line diff. Regions changed by only one side are taken
 * from that side; regions changed by both sides are compared, and if they do not agree they
 * are emitted as a conflict bracketed by \c <<<<<<<, \c =======, and \c >>>>>>> markers.
 *
 * \param base Lines of the common ancestor (typically the vanilla script).
 * \param ours Lines of the first mod's version.
 * \param theirs Lines of the second mod's version.
 * \param ours_label Label written after the \c <<<<<<< marker.
 * \param theirs_label Label written after the \c >>>>>>> marker.
 * \return The merged text and whether it contains conflicts.
 * \note Each element of the input vectors represents a single line and must NOT contain a
 * trailing newline; newlines are reinserted on output. See \ref splitLines.
 */
TextMergeResult mergeThreeWay(const std::vector<std::string>& base,
                              const std::vector<std::string>& ours,
                              const std::vector<std::string>& theirs,
                              std::string_view ours_label = DEFAULT_OURS_LABEL,
                              std::string_view theirs_label = DEFAULT_THEIRS_LABEL);

/*!
 * \brief Performs a line-based 2-way merge of two versions of a file with no common base.
 *
 * Computes a line diff between \p ours and \p theirs; identical regions are emitted once, and
 * every differing region is wrapped in standard conflict markers. This is the fallback used
 * when no vanilla base script is available, and it necessarily flags more conflicts than a
 * 3-way merge because it cannot distinguish an addition from an unchanged-on-one-side region.
 *
 * \param ours Lines of the first mod's version.
 * \param theirs Lines of the second mod's version.
 * \param ours_label Label written after the \c <<<<<<< marker.
 * \param theirs_label Label written after the \c >>>>>>> marker.
 * \return The merged text and whether it contains conflicts.
 */
TextMergeResult mergeTwoWay(const std::vector<std::string>& ours,
                            const std::vector<std::string>& theirs,
                            std::string_view ours_label = DEFAULT_OURS_LABEL,
                            std::string_view theirs_label = DEFAULT_THEIRS_LABEL);

/*!
 * \brief Splits a blob of text into individual lines.
 *
 * The text is split on '\\n'. The newline characters themselves are not included in the
 * returned strings, but a '\\r' immediately preceding a '\\n' is kept as part of the line so
 * that CRLF content survives a round trip. A trailing newline does not produce a final empty
 * line.
 *
 * \param text The text to split.
 * \return One string per line.
 */
std::vector<std::string> splitLines(const std::string& text);

/*!
 * \brief Recursively collects every WitcherScript file below a mod's script directory.
 *
 * Searches \c mod_root/content/scripts (see \ref SCRIPTS_SUBDIR) for files whose extension is
 * \ref SCRIPT_EXTENSION (compared case-insensitively).
 *
 * \param mod_root Absolute path to the mod's root directory.
 * \return Pairs of (relative script path using '/' separators, absolute path to the file). The
 * relative path is relative to the \c content/scripts directory and is used as the key that
 * identifies "the same script" across mods. Empty if the mod has no script directory.
 */
std::vector<std::pair<std::string, std::filesystem::path>> findScriptFiles(
  const std::filesystem::path& mod_root);
}
