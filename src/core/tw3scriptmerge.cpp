#include "tw3scriptmerge.h"
#include "log.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace sfs = std::filesystem;

namespace
{
/*!
 * \brief A contiguous run of matching lines shared by two sequences, expressed as start
 * indices into each sequence plus a length.
 *
 * A diff is represented as the ordered list of such matches; everything between two
 * consecutive matches is a change. This mirrors the "matching blocks" representation used by
 * many diff libraries and keeps the merge logic below independent of how the matches were
 * found.
 */
struct Match
{
  /*! \brief Start index of the run in the first ("left") sequence. */
  std::size_t left = 0;
  /*! \brief Start index of the run in the second ("right") sequence. */
  std::size_t right = 0;
  /*! \brief Number of consecutive equal lines. */
  std::size_t length = 0;
};

/*!
 * \brief Computes the matching blocks between two line sequences via a longest-common-
 * subsequence dynamic program.
 *
 * The classic O(n*m) LCS table is built and then traced back into a sequence of maximal
 * matching runs. This is simpler (and easier to audit for an MVP) than a full Myers diff while
 * producing an identical merge result, since only the set of matched lines matters here, not
 * the particular shortest edit script.
 *
 * \note TODO(tw3-scriptmerge): The O(n*m) memory/time of the LCS table is fine for typical
 * WitcherScript files (a few thousand lines) but would be wasteful for pathologically large
 * generated scripts. If that ever matters, swap this for the linear-space Hirschberg variant
 * or a true Myers diff; the rest of the module only consumes the returned matches.
 *
 * \param left First line sequence.
 * \param right Second line sequence.
 * \return Ordered, non-overlapping matching runs. Always terminated by a sentinel zero-length
 * match at (left.size(), right.size()) so callers can treat the tail uniformly.
 */
/*!
 * \brief Upper bound on the number of LCS table cells (\c n*m) \ref computeMatches is willing
 * to allocate.
 *
 * The classic LCS dynamic program needs O(n*m) memory; for a maliciously (or accidentally)
 * huge script pair this can exhaust the address space. ~50M cells of \c std::size_t is on the
 * order of a few hundred MB, which comfortably covers any real WitcherScript file while
 * refusing to attempt a pathological diff. When the cap is exceeded the diff is abandoned and
 * an empty match set (just the sentinel) is returned, which makes the callers fall back to
 * treating the whole content as one conflict region rather than crashing.
 */
constexpr std::size_t kMaxLcsCells = 50ull * 1000ull * 1000ull;

std::vector<Match> computeMatches(const std::vector<std::string>& left,
                                  const std::vector<std::string>& right)
{
  const std::size_t n = left.size();
  const std::size_t m = right.size();

  // Guard against the O(n*m) LCS table exhausting memory on a pathologically large script
  // pair. Returning only the sentinel makes the whole span surface as a single conflict
  // region instead of attempting (and likely failing) a multi-gigabyte allocation. The check
  // is written to avoid overflow in the n*m product itself.
  if(n != 0 && m > kMaxLcsCells / n)
    return { Match{ n, m, 0 } };

  // lcs[i][j] = length of the LCS of left[i:] and right[j:].
  std::vector<std::vector<std::size_t>> lcs(n + 1, std::vector<std::size_t>(m + 1, 0));
  for(std::size_t i = n; i-- > 0;)
  {
    for(std::size_t j = m; j-- > 0;)
    {
      if(left[i] == right[j])
        lcs[i][j] = lcs[i + 1][j + 1] + 1;
      else
        lcs[i][j] = std::max(lcs[i + 1][j], lcs[i][j + 1]);
    }
  }

  std::vector<Match> matches;
  std::size_t i = 0;
  std::size_t j = 0;
  while(i < n && j < m)
  {
    if(left[i] == right[j])
    {
      const std::size_t start_i = i;
      const std::size_t start_j = j;
      std::size_t length = 0;
      while(i < n && j < m && left[i] == right[j])
      {
        i++;
        j++;
        length++;
      }
      matches.push_back({ start_i, start_j, length });
    }
    else if(lcs[i + 1][j] >= lcs[i][j + 1])
      i++;
    else
      j++;
  }
  // Sentinel so the region after the last match is easy to emit.
  matches.push_back({ n, m, 0 });
  return matches;
}

/*!
 * \brief A single aligned region of a 3-way merge.
 *
 * The base sequence is the spine: each region covers a half-open range of base lines, together
 * with the corresponding ranges in the "ours" and "theirs" sequences. \ref stable is true when
 * neither side changed the region relative to base.
 */
struct Region
{
  std::size_t base_begin = 0;
  std::size_t base_end = 0;
  std::size_t ours_begin = 0;
  std::size_t ours_end = 0;
  std::size_t theirs_begin = 0;
  std::size_t theirs_end = 0;
};

/*!
 * \brief Maps every base line index to its matching index in another sequence (or "no match").
 *
 * Produces an array \c map of size \c base_size+1 where \c map[k] is the index in the other
 * sequence aligned with base line \c k, or \ref kNoMatch if base line \c k was deleted. The
 * extra slot \c map[base_size] is pinned to the other sequence's size so ranges stay bounded.
 */
constexpr std::size_t kNoMatch = static_cast<std::size_t>(-1);

std::vector<std::size_t> alignBaseTo(const std::vector<std::string>& base,
                                     const std::vector<std::string>& other)
{
  std::vector<std::size_t> map(base.size() + 1, kNoMatch);
  map[base.size()] = other.size();
  const std::vector<Match> matches = computeMatches(base, other);
  for(const Match& match : matches)
  {
    for(std::size_t k = 0; k < match.length; k++)
      map[match.left + k] = match.right + k;
  }
  return map;
}

/*!
 * \brief Returns true if the half-open line ranges [a_begin, a_end) of \p a and
 * [b_begin, b_end) of \p b contain exactly the same lines.
 */
bool rangesEqual(const std::vector<std::string>& a,
                 std::size_t a_begin,
                 std::size_t a_end,
                 const std::vector<std::string>& b,
                 std::size_t b_begin,
                 std::size_t b_end)
{
  if(a_end - a_begin != b_end - b_begin)
    return false;
  for(std::size_t off = 0; a_begin + off < a_end; off++)
  {
    if(a[a_begin + off] != b[b_begin + off])
      return false;
  }
  return true;
}

/*! \brief Appends the lines in [begin, end) of \p src to \p out. */
void appendRange(std::string& out,
                 const std::vector<std::string>& src,
                 std::size_t begin,
                 std::size_t end)
{
  for(std::size_t k = begin; k < end; k++)
  {
    out += src[k];
    out += '\n';
  }
}
} // namespace


namespace tw3_script_merge
{
std::vector<std::string> splitLines(const std::string& text)
{
  std::vector<std::string> lines;
  std::string current;
  for(const char c : text)
  {
    if(c == '\n')
    {
      lines.push_back(current);
      current.clear();
    }
    else
      current += c;
  }
  // A non-empty remainder is a final line that lacked a trailing newline. An empty remainder
  // means the text ended exactly on a newline, which should not yield a spurious empty line.
  if(!current.empty())
    lines.push_back(current);
  return lines;
}

TextMergeResult mergeThreeWay(const std::vector<std::string>& base,
                              const std::vector<std::string>& ours,
                              const std::vector<std::string>& theirs,
                              std::string_view ours_label,
                              std::string_view theirs_label)
{
  // Align both sides against the common base, then walk the base spine producing regions.
  const std::vector<std::size_t> base_to_ours = alignBaseTo(base, ours);
  const std::vector<std::size_t> base_to_theirs = alignBaseTo(base, theirs);

  TextMergeResult result;
  std::string& out = result.merged_text;

  std::size_t base_pos = 0;
  std::size_t ours_pos = 0;
  std::size_t theirs_pos = 0;

  while(base_pos <= base.size())
  {
    // Find the next base line that is present (matched) in BOTH sides; everything up to it is
    // a single changed region we must resolve together. The sentinel slot guarantees this
    // terminates with base_pos == base.size().
    std::size_t stable = base_pos;
    while(stable < base.size() &&
          (base_to_ours[stable] == kNoMatch || base_to_theirs[stable] == kNoMatch))
      stable++;

    const std::size_t ours_sync =
      stable < base.size() ? base_to_ours[stable] : base_to_ours[base.size()];
    const std::size_t theirs_sync =
      stable < base.size() ? base_to_theirs[stable] : base_to_theirs[base.size()];

    Region region;
    region.base_begin = base_pos;
    region.base_end = stable;
    region.ours_begin = ours_pos;
    region.ours_end = ours_sync;
    region.theirs_begin = theirs_pos;
    region.theirs_end = theirs_sync;

    const bool ours_changed = !rangesEqual(
      ours, region.ours_begin, region.ours_end, base, region.base_begin, region.base_end);
    const bool theirs_changed = !rangesEqual(
      theirs, region.theirs_begin, region.theirs_end, base, region.base_begin, region.base_end);

    if(!ours_changed && !theirs_changed)
      appendRange(out, base, region.base_begin, region.base_end);
    else if(ours_changed && !theirs_changed)
      appendRange(out, ours, region.ours_begin, region.ours_end);
    else if(!ours_changed && theirs_changed)
      appendRange(out, theirs, region.theirs_begin, region.theirs_end);
    else if(rangesEqual(ours,
                        region.ours_begin,
                        region.ours_end,
                        theirs,
                        region.theirs_begin,
                        region.theirs_end))
    {
      // Both sides made the same change: take it once, no conflict.
      appendRange(out, ours, region.ours_begin, region.ours_end);
    }
    else
    {
      // True conflict: both sides changed the region differently.
      result.has_conflicts = true;
      out += std::format("<<<<<<< {}\n", ours_label);
      appendRange(out, ours, region.ours_begin, region.ours_end);
      out += "=======\n";
      appendRange(out, theirs, region.theirs_begin, region.theirs_end);
      out += std::format(">>>>>>> {}\n", theirs_label);
    }

    if(stable >= base.size())
      break;

    // Emit the shared anchor line and advance all three cursors past it.
    out += base[stable];
    out += '\n';
    base_pos = stable + 1;
    ours_pos = ours_sync + 1;
    theirs_pos = theirs_sync + 1;
  }

  return result;
}

TextMergeResult mergeTwoWay(const std::vector<std::string>& ours,
                            const std::vector<std::string>& theirs,
                            std::string_view ours_label,
                            std::string_view theirs_label)
{
  const std::vector<Match> matches = computeMatches(ours, theirs);

  TextMergeResult result;
  std::string& out = result.merged_text;

  std::size_t ours_pos = 0;
  std::size_t theirs_pos = 0;
  for(const Match& match : matches)
  {
    const bool ours_has_change = match.left > ours_pos;
    const bool theirs_has_change = match.right > theirs_pos;
    if(ours_has_change || theirs_has_change)
    {
      result.has_conflicts = true;
      out += std::format("<<<<<<< {}\n", ours_label);
      appendRange(out, ours, ours_pos, match.left);
      out += "=======\n";
      appendRange(out, theirs, theirs_pos, match.right);
      out += std::format(">>>>>>> {}\n", theirs_label);
    }
    // Emit the matching run verbatim.
    appendRange(out, ours, match.left, match.left + match.length);
    ours_pos = match.left + match.length;
    theirs_pos = match.right + match.length;
  }

  return result;
}

std::vector<std::pair<std::string, std::filesystem::path>> findScriptFiles(
  const std::filesystem::path& mod_root)
{
  std::vector<std::pair<std::string, sfs::path>> scripts;
  const sfs::path scripts_root = mod_root / sfs::path(SCRIPTS_SUBDIR);
  std::error_code ec;
  if(!sfs::is_directory(scripts_root, ec))
    return scripts;

  for(auto it = sfs::recursive_directory_iterator(
        scripts_root, sfs::directory_options::skip_permission_denied, ec);
      it != sfs::recursive_directory_iterator();
      it.increment(ec))
  {
    if(ec)
    {
      Log::warning(std::format("Tw3 script merge: error while scanning '{}': {}",
                               scripts_root.string(),
                               ec.message()));
      ec.clear();
      continue;
    }
    // Skip symbolic links entirely: a symlinked .ws file (or directory) could resolve outside
    // the mod's scripts tree, so reading it would step outside the mod root. is_regular_file()
    // follows links, so this guard must come first.
    if(it->is_symlink(ec) || ec)
    {
      ec.clear();
      continue;
    }
    if(!it->is_regular_file(ec))
      continue;

    std::string extension = it->path().extension().string();
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if(extension != SCRIPT_EXTENSION)
      continue;

    // Key by the path relative to content/scripts, normalised to '/' so the same logical
    // script matches across mods regardless of host path separators.
    const sfs::path relative = sfs::relative(it->path(), scripts_root, ec);
    if(ec)
    {
      ec.clear();
      continue;
    }
    std::string key = relative.generic_string();
    scripts.emplace_back(std::move(key), it->path());
  }
  return scripts;
}

namespace
{
/*! \brief Reads an entire file as raw bytes. Returns an empty string and logs on failure. */
std::string readFileBytes(const sfs::path& path)
{
  std::ifstream stream(path, std::ios::binary);
  if(!stream.is_open())
  {
    Log::warning(std::format("Tw3 script merge: could not open '{}' for reading.", path.string()));
    return {};
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

/*!
 * \brief Locates the vanilla copy of a script, probing the two supported root layouts.
 *
 * \param vanilla_scripts_root The configured vanilla root (may or may not include the
 * \c content/scripts prefix).
 * \param relative_script Script path relative to \c content/scripts (with '/' separators).
 * \return The existing vanilla file path, or an empty optional if none was found.
 */
std::optional<sfs::path> findVanillaScript(const sfs::path& vanilla_scripts_root,
                                           const std::string& relative_script)
{
  std::error_code ec;
  // Layout A: vanilla_scripts_root already points at the scripts directory.
  const sfs::path direct = vanilla_scripts_root / relative_script;
  if(sfs::is_regular_file(direct, ec))
    return direct;
  // Layout B: vanilla_scripts_root points at a folder that contains content/scripts.
  const sfs::path nested =
    vanilla_scripts_root / sfs::path(tw3_script_merge::SCRIPTS_SUBDIR) / relative_script;
  if(sfs::is_regular_file(nested, ec))
    return nested;
  return {};
}
} // namespace

MergeResult mergeScripts(
  const std::vector<std::pair<int, std::filesystem::path>>& mods_in_load_order,
  const std::filesystem::path& output_dir,
  const std::optional<std::filesystem::path>& vanilla_scripts_root)
{
  MergeResult result;

  // Step 1: index every script path to the (mod id, file path) list that provides it, in load
  // order. std::map keeps the output deterministic, which matters for idempotency and tests.
  std::map<std::string, std::vector<std::pair<int, sfs::path>>> script_to_sources;
  for(const auto& [mod_id, mod_root] : mods_in_load_order)
  {
    for(auto& [relative_script, absolute_path] : findScriptFiles(mod_root))
      script_to_sources[relative_script].emplace_back(mod_id, std::move(absolute_path));
  }

  const sfs::path output_scripts_root = output_dir / sfs::path(SCRIPTS_SUBDIR);

  // Step 2: idempotency. Wipe any previously generated scripts so a re-run cannot leave stale
  // merged files behind. Only our own content/scripts subtree is touched.
  std::error_code ec;
  if(sfs::exists(output_scripts_root, ec))
  {
    sfs::remove_all(output_scripts_root, ec);
    if(ec)
      Log::warning(std::format("Tw3 script merge: could not clear '{}': {}",
                               output_scripts_root.string(),
                               ec.message()));
  }

  // Step 3: merge each script that is provided by more than one mod.
  for(const auto& [relative_script, sources] : script_to_sources)
  {
    if(sources.size() < 2)
      continue;

    std::optional<sfs::path> vanilla_path;
    if(vanilla_scripts_root)
      vanilla_path = findVanillaScript(*vanilla_scripts_root, relative_script);
    std::vector<std::string> base_lines;
    const bool have_base = vanilla_path.has_value();
    if(have_base)
      base_lines = splitLines(readFileBytes(*vanilla_path));

    // Left-fold the mod versions in load order. The first mod seeds "ours"; each subsequent
    // mod is merged in as "theirs". With a base, every step is a 3-way merge against vanilla;
    // without one, steps are 2-way.
    std::vector<std::string> accumulated =
      splitLines(readFileBytes(sources.front().second));
    std::string ours_label = std::format("mod{}", sources.front().first);
    bool conflicted = false;

    for(std::size_t idx = 1; idx < sources.size(); idx++)
    {
      const std::vector<std::string> theirs = splitLines(readFileBytes(sources[idx].second));
      const std::string theirs_label = std::format("mod{}", sources[idx].first);
      TextMergeResult step = have_base
                               ? mergeThreeWay(base_lines, accumulated, theirs, ours_label,
                                               theirs_label)
                               : mergeTwoWay(accumulated, theirs, ours_label, theirs_label);
      conflicted = conflicted || step.has_conflicts;
      accumulated = splitLines(step.merged_text);
      // After the first merge the accumulator represents "the mods so far"; relabel so a later
      // conflict block names the combined left side meaningfully.
      ours_label = "merged";
    }

    // Reassemble the merged lines into a single blob. Always terminate with a newline so the
    // output is stable regardless of whether the last source file had a trailing newline.
    std::string merged_text;
    for(const std::string& line : accumulated)
    {
      merged_text += line;
      merged_text += '\n';
    }

    const sfs::path out_path = output_scripts_root / sfs::path(relative_script);
    sfs::create_directories(out_path.parent_path(), ec);
    if(ec)
    {
      Log::error(std::format("Tw3 script merge: could not create directory for '{}': {}",
                             out_path.string(),
                             ec.message()));
      ec.clear();
      continue;
    }
    std::ofstream out_stream(out_path, std::ios::binary | std::ios::trunc);
    if(!out_stream.is_open())
    {
      Log::error(
        std::format("Tw3 script merge: could not open '{}' for writing.", out_path.string()));
      continue;
    }
    out_stream << merged_text;
    out_stream.close();

    result.scripts_merged++;
    result.written_files.push_back(out_path);
    if(conflicted)
    {
      result.conflicts_unresolved++;
      result.unresolved_paths.push_back(relative_script);
    }
    else
      result.conflicts_auto_resolved++;
  }

  Log::info(std::format("Tw3 script merge: merged {} script(s), {} auto-resolved, {} need "
                        "manual resolution.",
                        result.scripts_merged,
                        result.conflicts_auto_resolved,
                        result.conflicts_unresolved));
  return result;
}
}
