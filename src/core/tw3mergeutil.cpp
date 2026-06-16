/*!
 * \file tw3mergeutil.cpp
 * \brief Implementation of the Tw3MergeUtil namespace.
 *
 * \details See tw3mergeutil.h for the high-level description, the idempotency/reversibility
 * contract, and the sentinel-marker format. This file contains the actual parsing/merging logic
 * for \c input.xml (via pugixml) and the flat \c *.settings files (line-oriented text).
 *
 * Throughout this file, every place where behaviour depends on an *unverified* assumption about
 * the real Witcher 3 on-disk format carries a \c "// TODO(tw3-merge):" marker. None of these have
 * been validated against the game, because this module was written without access to an install.
 */

#include "tw3mergeutil.h"

#include "log.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace sfs = std::filesystem;

namespace
{
// ---------------------------------------------------------------------------------------------
// Shared marker helpers.
//
// The sentinel payload is identical for XML and settings files; only the surrounding comment
// syntax differs (<!-- ... --> for XML, "; ..." for the INI-like settings files). Keeping the
// payload identical means a single parser can recognise a region in either context.
// ---------------------------------------------------------------------------------------------

/*! \brief Prefix shared by every begin marker payload. */
constexpr const char* kBeginPrefix = "LIMO_MERGE_BEGIN modid=";
/*! \brief Prefix shared by every end marker payload. */
constexpr const char* kEndPrefix = "LIMO_MERGE_END modid=";

/*!
 * \brief Trims leading/trailing ASCII whitespace from a string view, returning a std::string.
 * \param s Input string.
 * \return Trimmed copy.
 */
std::string trim(const std::string& s)
{
  const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  auto begin = s.begin();
  while(begin != s.end() && is_space(static_cast<unsigned char>(*begin)))
    ++begin;
  auto end = s.end();
  while(end != begin && is_space(static_cast<unsigned char>(*(end - 1))))
    --end;
  return std::string(begin, end);
}

/*!
 * \brief Parses a mod id out of a marker comment payload.
 * \details Accepts payloads such as \c "LIMO_MERGE_BEGIN modid=12" (optionally surrounded by
 * whitespace). Recognises both the begin and end prefixes.
 * \param payload The (trimmed or untrimmed) comment text.
 * \param[out] mod_id Receives the parsed id on success.
 * \param[out] is_begin Set to true for a begin marker, false for an end marker.
 * \return True if \p payload is a LIMO_MERGE marker and an id was parsed.
 */
bool parseMarker(const std::string& payload, int& mod_id, bool& is_begin)
{
  const std::string text = trim(payload);
  const char* prefix = nullptr;
  if(text.starts_with(kBeginPrefix))
  {
    prefix = kBeginPrefix;
    is_begin = true;
  }
  else if(text.starts_with(kEndPrefix))
  {
    prefix = kEndPrefix;
    is_begin = false;
  }
  else
    return false;

  const std::string number = trim(text.substr(std::string(prefix).size()));
  if(number.empty())
    return false;
  try
  {
    size_t consumed = 0;
    mod_id = std::stoi(number, &consumed);
    // Reject trailing garbage after the number to avoid silently matching "12abc".
    return consumed == number.size();
  }
  catch(const std::exception&)
  {
    return false;
  }
}

// ---------------------------------------------------------------------------------------------
// input.xml helpers (pugixml).
// ---------------------------------------------------------------------------------------------

/*!
 * \brief Removes LIMO_MERGE regions (and the marker comments themselves) from an XML subtree.
 * \details Iterates the direct children of \p parent. When a \c LIMO_MERGE_BEGIN comment is seen,
 * every following sibling up to and including the matching \c LIMO_MERGE_END comment is deleted.
 * The function recurses into surviving element children so nested contexts are also cleaned. If
 * \p restrict_ids is non-empty, only regions whose id is in that set are removed.
 * \param parent Node whose descendants are cleaned (modified in place).
 * \param restrict_ids If non-empty, only remove regions for these mod ids.
 * \return Number of regions removed within this subtree.
 *
 * \todo TODO(tw3-merge): assumes begin/end markers are well-nested and appear as direct siblings
 * within a single parent (the way this module writes them). A file hand-edited so that a begin
 * and end live in different parents would not be cleaned correctly; we treat an unmatched begin
 * conservatively by deleting only the begin marker comment.
 */
int stripMergeRegions(pugi::xml_node parent, const std::set<int>& restrict_ids)
{
  int removed = 0;
  pugi::xml_node child = parent.first_child();
  while(child)
  {
    pugi::xml_node next = child.next_sibling();
    if(child.type() == pugi::node_comment)
    {
      int id = 0;
      bool is_begin = false;
      if(parseMarker(child.value(), id, is_begin) && is_begin &&
         (restrict_ids.empty() || restrict_ids.contains(id)))
      {
        // First confirm a matching end marker exists among the following siblings. If it does
        // not (an unmatched begin from a hand-edited/truncated file), we must NOT delete to the
        // end of the parent; per the documented contract we conservatively remove only the stray
        // begin marker comment and leave the surrounding content untouched.
        bool has_match = false;
        for(pugi::xml_node scan = child.next_sibling(); scan; scan = scan.next_sibling())
        {
          if(scan.type() != pugi::node_comment)
            continue;
          int sid = 0;
          bool sbegin = false;
          if(parseMarker(scan.value(), sid, sbegin) && !sbegin && sid == id)
          {
            has_match = true;
            break;
          }
        }
        if(!has_match)
        {
          parent.remove_child(child);
          child = next;
          continue;
        }
        // Delete everything from this begin marker through its matching end marker.
        pugi::xml_node cursor = child;
        bool closed = false;
        while(cursor)
        {
          pugi::xml_node to_delete = cursor;
          pugi::xml_node after = cursor.next_sibling();
          if(to_delete.type() == pugi::node_comment)
          {
            int cid = 0;
            bool cbegin = false;
            if(parseMarker(to_delete.value(), cid, cbegin) && !cbegin && cid == id)
              closed = true;
          }
          parent.remove_child(to_delete);
          cursor = after;
          if(closed)
            break;
        }
        removed++;
        next = cursor; // continue scanning right after the deleted region
        child = next;
        continue;
      }
    }
    // Recurse into element children so nested <InputContext> regions are cleaned too.
    if(child.type() == pugi::node_element)
      removed += stripMergeRegions(child, restrict_ids);
    child = next;
  }
  return removed;
}

/*!
 * \brief Produces a normalised, comparable signature for a binding element.
 * \details Used to de-duplicate: two \c <Action> (or other) elements are considered "the same"
 * if they have the same tag name and the same set of attribute name/value pairs, recursively
 * over child elements. Attribute order is ignored; child order is preserved (binding order can be
 * meaningful). Comment/whitespace nodes are skipped.
 * \param node Element to summarise.
 * \return A string that is equal for structurally-equal elements.
 *
 * \todo TODO(tw3-merge): the real game may treat two actions as duplicate based only on a key
 * attribute (e.g. the action \c IdTag or the bound \c Button), not on full structural equality.
 * If in-game testing shows duplicate keybinds slipping through, narrow this to compare only the
 * identifying attribute(s).
 */
std::string elementSignature(const pugi::xml_node& node)
{
  std::string sig = "<";
  sig += node.name();
  // Sort attributes for order-independent comparison.
  std::vector<std::pair<std::string, std::string>> attrs;
  for(pugi::xml_attribute a = node.first_attribute(); a; a = a.next_attribute())
    attrs.emplace_back(a.name(), a.value());
  std::ranges::sort(attrs);
  for(const auto& [name, value] : attrs)
  {
    sig += ' ';
    sig += name;
    sig += "=\"";
    sig += value;
    sig += '"';
  }
  sig += '>';
  for(pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
  {
    if(child.type() == pugi::node_element)
      sig += elementSignature(child);
  }
  sig += "</";
  sig += node.name();
  sig += '>';
  return sig;
}

/*!
 * \brief Reads the identifying name of an \c <InputContext> for matching fragment to target.
 * \details TW3 input contexts are identified by a name attribute. We try the most likely
 * attribute names in order and fall back to the empty string.
 * \param context An \c <InputContext> element.
 * \return The context's identifying name, or empty string if none found.
 *
 * \todo TODO(tw3-merge): the exact attribute that identifies an InputContext is unverified.
 * Common candidates are \c "context" and \c "name"; adjust once a real input.xml is inspected.
 */
std::string inputContextName(const pugi::xml_node& context)
{
  for(const char* attr : { "context", "name", "Context", "Name" })
  {
    pugi::xml_attribute a = context.attribute(attr);
    if(a && *a.value())
      return a.value();
  }
  return std::string();
}

/*!
 * \brief Collects the signatures of all binding elements currently in a context.
 * \param context An \c <InputContext> element in the target document.
 * \return Set of \ref elementSignature values for its element children.
 */
std::set<std::string> collectExistingSignatures(const pugi::xml_node& context)
{
  std::set<std::string> sigs;
  for(pugi::xml_node child = context.first_child(); child; child = child.next_sibling())
  {
    if(child.type() == pugi::node_element)
      sigs.insert(elementSignature(child));
  }
  return sigs;
}

/*!
 * \brief Finds (or creates) an \c <InputContext> with a given name under the target root.
 * \param root The element that holds \c <InputContext> children in the target document.
 * \param name Identifying name to match (see \ref inputContextName). If empty, matches the first
 * unnamed context, else appends a new one.
 * \param[out] created Set to true if a new context element was appended.
 * \return The matching or newly created \c <InputContext> node.
 */
pugi::xml_node findOrCreateContext(pugi::xml_node root, const std::string& name, bool& created)
{
  created = false;
  for(pugi::xml_node ctx = root.child("InputContext"); ctx;
      ctx = ctx.next_sibling("InputContext"))
  {
    if(inputContextName(ctx) == name)
      return ctx;
  }
  // Not found: create one and give it the same identifying attribute we matched on.
  pugi::xml_node ctx = root.append_child("InputContext");
  if(!name.empty())
    ctx.append_attribute("context") = name.c_str(); // see inputContextName TODO
  created = true;
  return ctx;
}

/*!
 * \brief Returns the element that holds \c <InputContext> children, given a loaded document.
 * \details Mods sometimes wrap contexts in a root element (often \c <UserConfig> or similar) and
 * sometimes list them at the document root. This returns the deepest sensible container: the
 * first element that has at least one \c <InputContext> child, else the document root itself.
 * \param doc Loaded document.
 * \return The container node (may be the document node).
 *
 * \todo TODO(tw3-merge): the real root element name for input.xml is unverified. We locate the
 * container structurally (whoever owns the <InputContext> children) to avoid hard-coding it, but
 * the created-from-scratch case below picks a placeholder root name that must be corrected once
 * the real format is known.
 */
pugi::xml_node inputContextContainer(pugi::xml_document& doc)
{
  // Direct children of the document that themselves contain InputContext elements.
  for(pugi::xml_node top = doc.first_child(); top; top = top.next_sibling())
  {
    if(top.type() == pugi::node_element && top.child("InputContext"))
      return top;
  }
  // Contexts listed directly at document root?
  if(doc.child("InputContext"))
    return doc;
  // Fall back to the first element child if present.
  for(pugi::xml_node top = doc.first_child(); top; top = top.next_sibling())
  {
    if(top.type() == pugi::node_element)
      return top;
  }
  return doc;
}

} // namespace


namespace Tw3MergeUtil
{
std::string beginMarker(int mod_id)
{
  return std::string(kBeginPrefix) + std::to_string(mod_id);
}

std::string endMarker(int mod_id)
{
  return std::string(kEndPrefix) + std::to_string(mod_id);
}

sfs::path findFragment(const sfs::path& source_path, const std::string& file_name)
{
  std::error_code ec;
  if(!sfs::is_directory(source_path, ec))
    return {};

  // 1. Direct child.
  const sfs::path direct = source_path / file_name;
  if(sfs::is_regular_file(direct, ec))
    return direct;

  // 2. One directory level deep (common single-folder-nesting case).
  for(sfs::directory_iterator it(source_path, ec), end; !ec && it != end; it.increment(ec))
  {
    if(!it->is_directory(ec))
      continue;
    const sfs::path candidate = it->path() / file_name;
    if(sfs::is_regular_file(candidate, ec))
      return candidate;
  }
  return {};
}

/*!
 * \brief Heuristic: does \p source_path look like it contains config-style files?
 * \details Used only to decide whether a failure of \ref findFragment is worth warning about.
 * A directory that holds \c .xml / \c .settings / \c .ini files (directly or one level deep)
 * but whose target fragment was not located by the shallow search is a likely candidate for a
 * deeper or game-mirrored layout that the MVP depth-1 search misses, so the silent no-op should
 * be surfaced. Returns false on any filesystem error (warn-on-best-effort only).
 */
bool looksLikeConfigDir(const sfs::path& source_path)
{
  static const std::set<std::string> kConfigExts{ ".xml", ".settings", ".ini" };
  std::error_code ec;
  if(!sfs::is_directory(source_path, ec))
    return false;
  for(sfs::recursive_directory_iterator it(source_path, ec), end; !ec && it != end;
      it.increment(ec))
  {
    // Bound the scan depth so a deeply nested mod tree does not stall the heuristic.
    if(it.depth() > 2)
    {
      it.disable_recursion_pending();
      continue;
    }
    if(it->is_regular_file(ec))
    {
      std::string ext = it->path().extension().string();
      std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return std::tolower(c); });
      if(kConfigExts.contains(ext))
        return true;
    }
  }
  return false;
}

MergeResult mergeInputXml(const sfs::path& game_root,
                          const std::vector<MergeSource>& sources,
                          bool dry_run)
{
  MergeResult result;
  const sfs::path target = game_root / USER_CONFIG_REL_DIR / INPUT_XML_NAME;

  // Load (or initialise) the target document. parse_comments is required so our existing
  // LIMO_MERGE marker comments survive a load/save round-trip and can be detected.
  pugi::xml_document doc;
  bool target_existed = false;
  std::error_code ec;
  if(sfs::is_regular_file(target, ec))
  {
    target_existed = true;
    const pugi::xml_parse_result pr =
      doc.load_file(target.c_str(), pugi::parse_default | pugi::parse_comments);
    if(!pr)
    {
      result.success = false;
      result.message =
        std::string("Failed to parse target input.xml: ") + pr.description();
      Log::error(result.message);
      return result;
    }
  }
  else
  {
    // TODO(tw3-merge): the correct declaration/encoding and root element of a fresh input.xml are
    // unverified. We emit a minimal UTF-8 document with a placeholder <UserConfig> root so the
    // merge can proceed on a clean install; replace the root name once the real format is known.
    pugi::xml_node decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    doc.append_child("UserConfig");
  }

  pugi::xml_node container = inputContextContainer(doc);

  // Make the operation idempotent: drop any LIMO_MERGE regions belonging to the mods we are about
  // to (re)merge. We restrict to the incoming ids so unrelated mods' regions are preserved. When
  // there are no incoming ids (empty source list) we strip nothing: stripMergeRegions treats an
  // empty id set as "remove every region", which would wipe other mods' contributions on a no-op
  // call, so we must guard against that here.
  std::set<int> incoming_ids;
  for(const auto& src : sources)
    incoming_ids.insert(src.mod_id);
  const int stripped = incoming_ids.empty() ? 0 : stripMergeRegions(doc, incoming_ids);

  int total_entries = 0;
  int mods_with_entries = 0;
  int contexts_created = 0;
  for(const auto& src : sources)
  {
    const sfs::path fragment = findFragment(src.source_path, INPUT_XML_NAME);
    if(fragment.empty())
    {
      // The shallow (depth-1) search found nothing. If the mod nonetheless ships config-like
      // files, its fragment may live in a deeper or game-mirrored subtree that findFragment does
      // not reach; warn so this silent no-op is diagnosable instead of mysteriously dropping the
      // mod's bindings.
      if(looksLikeConfigDir(src.source_path))
        Log::warning(std::string("Tw3MergeUtil: no ") + INPUT_XML_NAME + " found within depth 1 "
                     "for mod " + std::to_string(src.mod_id) + " (" + src.source_path.string() +
                     "), but the directory contains config-like files; a deeper layout may be "
                     "unsupported.");
      continue;
    }

    pugi::xml_document frag_doc;
    const pugi::xml_parse_result pr =
      frag_doc.load_file(fragment.c_str(), pugi::parse_default | pugi::parse_comments);
    if(!pr)
    {
      // A single bad fragment should not abort the whole pass.
      Log::warning(std::string("Tw3MergeUtil: skipping unparseable input.xml for mod ") +
                   std::to_string(src.mod_id) + " (" + fragment.string() + "): " + pr.description());
      continue;
    }

    pugi::xml_node frag_container = inputContextContainer(frag_doc);
    int entries_this_mod = 0;

    for(pugi::xml_node frag_ctx = frag_container.child("InputContext"); frag_ctx;
        frag_ctx = frag_ctx.next_sibling("InputContext"))
    {
      const std::string ctx_name = inputContextName(frag_ctx);
      bool created_ctx = false;
      pugi::xml_node target_ctx = findOrCreateContext(container, ctx_name, created_ctx);
      std::set<std::string> existing = collectExistingSignatures(target_ctx);

      // Gather the not-yet-present binding elements from this fragment context.
      std::vector<pugi::xml_node> to_insert;
      for(pugi::xml_node entry = frag_ctx.first_child(); entry; entry = entry.next_sibling())
      {
        if(entry.type() != pugi::node_element)
          continue;
        const std::string sig = elementSignature(entry);
        if(existing.contains(sig))
          continue;
        existing.insert(sig); // also de-dup within the same fragment
        to_insert.push_back(entry);
      }
      if(to_insert.empty())
        continue;

      // Wrap the inserted elements in begin/end marker comments for idempotency + reversibility.
      pugi::xml_node begin_comment = target_ctx.append_child(pugi::node_comment);
      begin_comment.set_value(beginMarker(src.mod_id).c_str());
      for(const pugi::xml_node& entry : to_insert)
      {
        target_ctx.append_copy(entry);
        entries_this_mod++;
      }
      pugi::xml_node end_comment = target_ctx.append_child(pugi::node_comment);
      end_comment.set_value(endMarker(src.mod_id).c_str());

      // Track contexts we synthesised that actually received entries: because the identifying
      // attribute used by findOrCreateContext is unverified, the game may not recognise a
      // from-scratch <InputContext>, silently ignoring its bindings. Surface this in the result.
      if(created_ctx)
        contexts_created++;
    }

    if(entries_this_mod > 0)
    {
      mods_with_entries++;
      total_entries += entries_this_mod;
    }
  }

  result.success = true;
  result.mods_merged = mods_with_entries;
  result.entries_merged = total_entries;
  // "changed" is true if we either removed old regions or inserted new entries, or had to create
  // the file from scratch.
  result.changed = stripped > 0 || total_entries > 0 || !target_existed;

  // Experimental-format caveat: contexts we created from scratch use an identifying attribute
  // that has not been validated against a real input.xml, so the game may ignore their bindings.
  std::string experimental_warning;
  if(contexts_created > 0)
  {
    experimental_warning =
      " WARNING (experimental/unvalidated format): " + std::to_string(contexts_created) +
      " <InputContext> section(s) were created from scratch; because their identifying attribute "
      "is unverified, the game may not recognise them and could silently ignore those bindings.";
    Log::warning("Tw3MergeUtil:" + experimental_warning);
  }

  if(!result.changed)
  {
    result.message = "input.xml already up to date; no changes written.";
    return result;
  }

  if(dry_run)
  {
    result.message = "Dry run: input.xml would be updated (" + std::to_string(total_entries) +
                     " entries from " + std::to_string(mods_with_entries) + " mods)." +
                     experimental_warning;
    return result;
  }

  // Ensure the destination directory exists before writing.
  sfs::create_directories(target.parent_path(), ec);

  // format_no_empty_element_tags keeps <Foo></Foo> rather than <Foo/>, which is closer to how
  // game-shipped XML tends to look; format_indent keeps the file human-diffable.
  // TODO(tw3-merge): confirm the game tolerates pugixml's re-serialisation (indentation, self-
  // closing tag style, attribute quoting). If the loader is picky, switch to format_raw or
  // preserve the original formatting via a text-level splice instead of a full rewrite.
  const bool ok = doc.save_file(
    target.c_str(), "\t", pugi::format_indent | pugi::format_no_empty_element_tags);
  if(!ok)
  {
    result.success = false;
    result.changed = false;
    result.message = "Failed to write target input.xml: " + target.string();
    Log::error(result.message);
    return result;
  }

  result.message = "Merged " + std::to_string(total_entries) + " input.xml entries from " +
                   std::to_string(mods_with_entries) + " mods into " + target.string() + "." +
                   experimental_warning;
  Log::info(result.message);
  return result;
}

MergeResult mergeSettingsFile(const sfs::path& settings_dir,
                              const std::string& settings_file_name,
                              const std::vector<MergeSource>& sources,
                              bool dry_run)
{
  MergeResult result;
  const sfs::path target = settings_dir / settings_file_name;

  // ---- Load existing target lines (if any) ----
  std::vector<std::string> lines;
  bool target_existed = false;
  std::error_code ec;
  if(sfs::is_regular_file(target, ec))
  {
    target_existed = true;
    std::ifstream in(target);
    if(!in)
    {
      result.success = false;
      result.message = "Failed to open settings file for reading: " + target.string();
      Log::error(result.message);
      return result;
    }
    std::string line;
    while(std::getline(in, line))
    {
      // Normalise away a trailing CR so Windows-style line endings don't confuse parsing.
      if(!line.empty() && line.back() == '\r')
        line.pop_back();
      lines.push_back(line);
    }
  }

  std::set<int> incoming_ids;
  for(const auto& src : sources)
    incoming_ids.insert(src.mod_id);

  // ---- Strip prior LIMO_MERGE regions for the incoming mods ----
  // The settings markers are line comments: ";" + payload. We drop every line from a begin marker
  // through its matching end marker (inclusive). We do NOT touch any surrounding blank lines: this
  // module never emits a blank line as part of a region (see the rebuild step, which inserts
  // regions without separator blanks), so stripping leaves the rest of the file byte-for-byte
  // intact apart from the removed regions. That property is what makes a re-merge idempotent.
  std::vector<std::string> stripped_lines;
  stripped_lines.reserve(lines.size());
  bool any_stripped = false;
  {
    bool in_region = false;
    int region_id = 0;
    for(const std::string& raw : lines)
    {
      const std::string text = trim(raw);
      if(!in_region)
      {
        if(!text.empty() && text.front() == ';')
        {
          int id = 0;
          bool is_begin = false;
          // Only strip regions for mods we are about to re-merge. An empty incoming set therefore
          // strips nothing, keeping a no-op call non-destructive (mirrors mergeInputXml).
          if(parseMarker(text.substr(1), id, is_begin) && is_begin && incoming_ids.contains(id))
          {
            in_region = true;
            region_id = id;
            any_stripped = true;
            continue; // drop the begin marker line
          }
        }
        stripped_lines.push_back(raw);
      }
      else
      {
        if(!text.empty() && text.front() == ';')
        {
          int id = 0;
          bool is_begin = false;
          if(parseMarker(text.substr(1), id, is_begin) && !is_begin && id == region_id)
            in_region = false; // drop the end marker line and resume
        }
        // else: inside a region -> drop the line
      }
    }
    // TODO(tw3-merge): if a begin marker is left unmatched (file truncated/hand-edited), the rest
    // of the file would be dropped. We guard against catastrophic loss by only entering this path
    // for our own markers, but real-world robustness needs validation.
  }
  lines = std::move(stripped_lines);

  // ---- Parse current target into section -> set<key> for de-duplication ----
  // We do NOT rewrite existing content; we only need to know which keys already exist per section
  // so we can append the missing ones. Keys are compared case-insensitively.
  // TODO(tw3-merge): TW3 .settings key comparison case-sensitivity is unverified; we assume
  // case-insensitive to be conservative about duplicates. Adjust if the game is case-sensitive.
  const auto lower = [](std::string s)
  {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
  };
  const auto parseSection = [](const std::string& text) -> std::string
  {
    if(text.size() >= 2 && text.front() == '[' && text.back() == ']')
      return text.substr(1, text.size() - 2);
    return {};
  };
  const auto parseKey = [](const std::string& text) -> std::string
  {
    const auto eq = text.find('=');
    if(eq == std::string::npos)
      return {};
    return trim(text.substr(0, eq));
  };

  std::map<std::string, std::set<std::string>> existing_keys; // lower(section) -> {lower(key)}
  std::string cur_section;
  for(const std::string& raw : lines)
  {
    const std::string text = trim(raw);
    if(text.empty() || text.front() == ';')
      continue;
    const std::string section = parseSection(text);
    if(!section.empty())
    {
      cur_section = section;
      existing_keys.try_emplace(lower(cur_section));
      continue;
    }
    const std::string key = parseKey(text);
    if(!key.empty())
      existing_keys[lower(cur_section)].insert(lower(key));
  }

  // ---- Walk fragments, collecting missing key=value lines per section ----
  // additions[section] = vector of (mod_id, "key=value") lines, in load order.
  std::map<std::string, std::vector<std::pair<int, std::string>>> additions;
  // Preserve first-seen section order for any sections we must create.
  std::vector<std::string> new_section_order;
  int total_entries = 0;
  int mods_with_entries = 0;

  for(const auto& src : sources)
  {
    const sfs::path fragment = findFragment(src.source_path, settings_file_name);
    if(fragment.empty())
      continue;
    std::ifstream in(fragment);
    if(!in)
    {
      Log::warning("Tw3MergeUtil: could not open settings fragment " + fragment.string());
      continue;
    }

    int entries_this_mod = 0;
    std::string frag_section;
    std::string line;
    while(std::getline(in, line))
    {
      if(!line.empty() && line.back() == '\r')
        line.pop_back();
      const std::string text = trim(line);
      if(text.empty() || text.front() == ';' || text.front() == '#')
        continue;
      const std::string section = parseSection(text);
      if(!section.empty())
      {
        frag_section = section;
        continue;
      }
      const std::string key = parseKey(text);
      if(key.empty())
        continue; // not a key=value line; ignore conservatively

      auto& keyset = existing_keys[lower(frag_section)];
      const std::string lkey = lower(key);
      if(keyset.contains(lkey))
        continue; // already present (in target or an earlier fragment this pass)
      keyset.insert(lkey);

      if(!additions.contains(frag_section))
        new_section_order.push_back(frag_section);
      additions[frag_section].emplace_back(src.mod_id, text);
      entries_this_mod++;
    }

    if(entries_this_mod > 0)
    {
      mods_with_entries++;
      total_entries += entries_this_mod;
    }
  }

  result.success = true;
  result.mods_merged = mods_with_entries;
  result.entries_merged = total_entries;
  result.changed = any_stripped || total_entries > 0;

  if(!result.changed)
  {
    result.message = settings_file_name + " already up to date; no changes written.";
    return result;
  }

  // ---- Rebuild the file: original (stripped) content, then per-section additions ----
  // Strategy: for sections that already exist in the file, insert the additions right after the
  // last existing line of that section (so they stay grouped). For brand-new sections, append a
  // new [Section] block at the end of the file. Each contiguous run of additions is wrapped in
  // begin/end markers per mod.
  std::vector<std::string> out = lines;

  // Helper: build a marked block of "key=value" lines for one section's additions.
  const auto buildBlock =
    [](const std::vector<std::pair<int, std::string>>& items) -> std::vector<std::string>
  {
    std::vector<std::string> block;
    int open_id = -1;
    for(const auto& [mod_id, kv] : items)
    {
      if(open_id != mod_id)
      {
        if(open_id != -1)
          block.push_back("; " + endMarker(open_id));
        block.push_back("; " + beginMarker(mod_id));
        open_id = mod_id;
      }
      block.push_back(kv);
    }
    if(open_id != -1)
      block.push_back("; " + endMarker(open_id));
    return block;
  };

  // Insert additions for sections that already exist, from bottom to top so earlier insertion
  // indices remain valid.
  // First, find the line index just past the end of each existing section.
  {
    // Map each existing section name (as written) to the index after its last *content* line
    // (the section header itself, or a key=value line). Trailing blank lines and comment lines are
    // deliberately excluded from the anchor: anchoring past them would make the insertion point
    // drift whenever blanks/comments surround a section, which breaks idempotency across re-merges.
    std::map<std::string, size_t> section_insert_at; // section (lower) -> insertion index
    std::string cur;
    for(size_t i = 0; i < out.size(); ++i)
    {
      const std::string text = trim(out[i]);
      const std::string section = parseSection(text);
      if(!section.empty())
      {
        cur = lower(section);
        section_insert_at[cur] = i + 1; // header counts as content; anchor just past it
        continue;
      }
      if(cur.empty())
        continue;
      // Only advance the anchor for genuine key=value content, not blanks or comment lines.
      if(!text.empty() && text.front() != ';' && text.front() != '#' && text.find('=') != std::string::npos)
        section_insert_at[cur] = i + 1;
    }

    // Collect insertions (index, lines) for existing sections.
    std::vector<std::pair<size_t, std::vector<std::string>>> insertions;
    std::vector<std::string> truly_new_sections;
    for(const std::string& section : new_section_order)
    {
      auto it = section_insert_at.find(lower(section));
      if(it == section_insert_at.end())
      {
        truly_new_sections.push_back(section);
        continue;
      }
      insertions.emplace_back(it->second, buildBlock(additions[section]));
    }

    // Apply existing-section insertions bottom-up.
    std::ranges::sort(insertions,
                      [](const auto& a, const auto& b) { return a.first > b.first; });
    for(const auto& [index, block] : insertions)
      out.insert(out.begin() + static_cast<std::ptrdiff_t>(index), block.begin(), block.end());

    // Append brand-new sections at the end. We intentionally do NOT emit a blank separator line
    // before the header: a separator would be present after the first merge but stripped on a
    // re-merge (the region's content is removed but a leading blank is not), so the file would
    // differ between runs. Omitting it keeps the merge strictly idempotent at the cost of slightly
    // denser output, which is acceptable for an MVP and tolerated by INI-style parsers.
    for(const std::string& section : truly_new_sections)
    {
      out.push_back("[" + section + "]");
      const std::vector<std::string> block = buildBlock(additions[section]);
      out.insert(out.end(), block.begin(), block.end());
    }
  }

  if(dry_run)
  {
    result.message = "Dry run: " + settings_file_name + " would be updated (" +
                     std::to_string(total_entries) + " entries from " +
                     std::to_string(mods_with_entries) + " mods).";
    return result;
  }

  sfs::create_directories(target.parent_path(), ec);
  std::ofstream os(target, std::ios::binary | std::ios::trunc);
  if(!os)
  {
    result.success = false;
    result.changed = false;
    result.message = "Failed to open settings file for writing: " + target.string();
    Log::error(result.message);
    return result;
  }
  // TODO(tw3-merge): line-ending style is unverified. The game's own writes may use CRLF on the
  // Windows side of Proton. We emit LF; if the game or its tooling requires CRLF, switch here.
  for(const std::string& l : out)
    os << l << '\n';
  os.close();

  result.changed = true;
  result.message = "Merged " + std::to_string(total_entries) + " " + settings_file_name +
                   " entries from " + std::to_string(mods_with_entries) + " mods into " +
                   target.string() + ".";
  // Silence unused-variable warning in builds where target_existed is only used for clarity.
  (void)target_existed;
  Log::info(result.message);
  return result;
}
} // namespace Tw3MergeUtil
