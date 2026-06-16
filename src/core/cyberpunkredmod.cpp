#include "cyberpunkredmod.h"
#include <algorithm>
#include <fstream>
#include <json/json.h>

namespace sfs = std::filesystem;

namespace
{
/*!
 * \brief POSIX-shell-escapes an arbitrary string by single-quoting it.
 *
 * Wraps \p s in single quotes and replaces every embedded single quote with the
 * \c '\\'' sequence. Inside single quotes the shell treats every other character
 * literally, so this neutralises $, backticks, ;, spaces, backslashes, etc. Used
 * for values that originate from untrusted mod metadata (e.g. a REDmod's name from
 * its info.json) before they are placed on a shell command line.
 * \param s String to escape.
 * \return The single-quoted, escaped string.
 */
std::string shellEscape(const std::string& s)
{
  std::string out = "'";
  for(char c : s)
  {
    if(c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}

/*!
 * \brief Escapes a path for safe inclusion in a shell command.
 *
 * Uses the same POSIX single-quote escaping as \ref shellEscape so that $,
 * backticks, backslashes, spaces and embedded quotes are all neutralised, even
 * for paths that originate from untrusted input.
 * \param path Path to quote.
 * \return The single-quoted, escaped string.
 */
std::string quote(const sfs::path& path)
{
  return shellEscape(path.string());
}

/*!
 * \brief Reads and parses a JSON file from disk.
 * \param file_path File to read.
 * \param[out] out_value Receives the parsed value on success.
 * \return True on success, false if the file cannot be opened or parsed.
 */
bool readJsonFile(const sfs::path& file_path, Json::Value& out_value)
{
  std::ifstream file(file_path, std::fstream::binary);
  if(!file.is_open())
    return false;
  try
  {
    file >> out_value;
  }
  catch(const Json::Exception&)
  {
    return false;
  }
  return true;
}
} // namespace


namespace cyberpunk_redmod
{
bool RedMod::hasContent(RedModContentType type) const
{
  return (content_types & type) != 0;
}

std::optional<RedMod> parseRedMod(const sfs::path& mod_dir)
{
  std::error_code ec;
  if(!sfs::is_directory(mod_dir, ec))
    return std::nullopt;

  const sfs::path info_path = mod_dir / REDMOD_INFO_FILE_NAME;
  if(!sfs::is_regular_file(info_path, ec))
    return std::nullopt;

  Json::Value info;
  if(!readJsonFile(info_path, info))
    return std::nullopt;

  // The name is mandatory; without it the mod cannot be addressed by redMod.exe.
  // TODO(cp-redmod): Confirm "name" is always present and is the value used for
  // the -mod= argument (as opposed to the folder name on disk).
  if(!info.isMember("name") || !info["name"].isString() || info["name"].asString().empty())
    return std::nullopt;

  // The name is used verbatim as a path component under game_root/mods/, so reject
  // any value that could escape that directory (path separators, ".", "..",
  // absolute paths or embedded ".." elements) to prevent path traversal.
  const std::string raw_name = info["name"].asString();
  if(raw_name == "." || raw_name == ".." ||
     raw_name.find('/') != std::string::npos || raw_name.find('\\') != std::string::npos)
    return std::nullopt;

  RedMod mod;
  mod.name = raw_name;
  mod.source_path = mod_dir;
  // TODO(cp-redmod): Verify the version field is named "version" and is a string;
  // some tooling stores it as a number or under a different key.
  if(info.isMember("version") && (info["version"].isString() || info["version"].isNumeric()))
    mod.version = info["version"].asString();

  // Derive content type flags from the declared info.json sections...
  // TODO(cp-redmod): Confirm these key names match the real schema. The customSounds,
  // scripts and tweaks sections are documented but optional.
  if(info.isMember("customSounds"))
    mod.content_types |= custom_sounds;
  if(info.isMember("scripts"))
    mod.content_types |= scripts;
  if(info.isMember("tweaks"))
    mod.content_types |= tweaks;

  // ...and additionally from the subdirectories actually present on disk, since
  // many mods ship content folders without declaring matching info.json sections.
  // TODO(cp-redmod): Validate these directory names against real REDmods.
  if(sfs::is_directory(mod_dir / "archives", ec))
    mod.content_types |= archives;
  if(sfs::is_directory(mod_dir / "customSounds", ec))
    mod.content_types |= custom_sounds;
  if(sfs::is_directory(mod_dir / "scripts", ec))
    mod.content_types |= scripts;
  if(sfs::is_directory(mod_dir / "tweaks", ec))
    mod.content_types |= tweaks;

  return mod;
}

std::vector<RedMod> detectRedMods(const sfs::path& source_dir)
{
  std::vector<RedMod> mods;
  std::error_code ec;
  if(!sfs::is_directory(source_dir, ec))
    return mods;

  sfs::directory_iterator it(source_dir, ec);
  if(ec)
    return mods;
  for(const auto& entry : it)
  {
    if(!entry.is_directory(ec))
      continue;
    if(auto mod = parseRedMod(entry.path()))
      mods.push_back(std::move(*mod));
  }
  return mods;
}

void writeLoadOrderFile(const std::vector<std::string>& mod_names_in_order,
                        const sfs::path& game_root)
{
  const sfs::path mods_dir = game_root / REDMOD_MODS_DIR;
  std::error_code ec;
  sfs::create_directories(mods_dir, ec);

  // TODO(cp-redmod): This schema is an assumption. Verify against the installed
  // game; the historical format used {"enabledMods":["name0","name1"]} while newer
  // launchers use an array of objects. Both are produced below only as one guess.
  Json::Value root;
  root["mods"] = Json::Value(Json::arrayValue);
  for(const auto& name : mod_names_in_order)
  {
    Json::Value entry;
    entry["folder"] = name;
    entry["enabled"] = true;
    entry["deployable"] = true;
    root["mods"].append(entry);
  }

  const sfs::path file_path = mods_dir / REDMOD_LOAD_ORDER_FILE_NAME;
  std::ofstream file(file_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not write to \"" + file_path.string() + "\".");
  file << root;
}

void layoutRedMods(const std::vector<std::pair<int, sfs::path>>& mods_in_load_order,
                   const sfs::path& game_root)
{
  const sfs::path mods_dir = game_root / REDMOD_MODS_DIR;
  std::error_code ec;
  sfs::create_directories(mods_dir, ec);

  std::vector<std::string> ordered_names;
  ordered_names.reserve(mods_in_load_order.size());

  for(const auto& [id, source_path] : mods_in_load_order)
  {
    auto mod = parseRedMod(source_path);
    // Skip sources that are not valid REDmods rather than aborting the whole
    // layout; detection already filtered most of these but a caller may pass an
    // arbitrary path.
    if(!mod)
      continue;

    const sfs::path dest = mods_dir / mod->name;
    // Remove any previous deployment of this mod so stale files do not linger.
    sfs::remove_all(dest, ec);
    // TODO(cp-redmod): This copies the whole mod folder. Consider hard-linking for
    // large archive mods, mirroring Deployer::hard_link mode, once validated.
    // Copy symlinks as links rather than following them, so a malicious mod cannot
    // use a symlink to read or overwrite files outside the destination.
    sfs::copy(source_path,
              dest,
              sfs::copy_options::recursive | sfs::copy_options::overwrite_existing |
                sfs::copy_options::copy_symlinks);

    ordered_names.push_back(mod->name);
  }

  writeLoadOrderFile(ordered_names, game_root);
}

std::string redmodDeployCommand(const sfs::path& game_root,
                                const std::vector<std::string>& mod_names_in_order,
                                const sfs::path& proton_prefix)
{
  const sfs::path redmod_exe = game_root / REDMOD_TOOL_RELATIVE_PATH;

  std::string command;

  // Export the compat data path when provided so that both the protontricks bridge
  // and an alternative direct-proton bridge observe a consistent prefix.
  // TODO(cp-redmod): Confirm STEAM_COMPAT_DATA_PATH is the correct variable and
  // that protontricks-launch tolerates it being set.
  if(!proton_prefix.empty())
    command += "STEAM_COMPAT_DATA_PATH=" + quote(proton_prefix) + " ";

  // Proton bridge: reuse the same mechanism as Limo's Tool protontricks runtime.
  // TODO(cp-redmod): The flatpak variant
  // ("flatpak run --command=protontricks-launch com.github.Matoking.protontricks")
  // is not emitted here; wire that in when surfacing this in the UI, matching
  // Tool::getCommand's is_flatpak branch.
  command += "protontricks-launch --appid " + std::to_string(CYBERPUNK_STEAM_APP_ID) + " ";

  // The executable to run inside the prefix.
  command += quote(redmod_exe);

  // The deploy verb.
  // TODO(cp-redmod): Confirm the verb is the literal "deploy".
  command += " deploy";

  // Point the tool at the game install.
  // TODO(cp-redmod): Confirm the -root flag spelling and that a native Linux path
  // is accepted (it may require a Windows path under Proton).
  command += " -root=" + quote(game_root);

  // Pass the load order explicitly, one -mod= per entry, in order. The mod name
  // comes from untrusted info.json metadata, so it is shell-escaped to prevent
  // command injection (see shellEscape).
  // TODO(cp-redmod): Confirm repeated -mod=<name> arguments are the correct way to
  // specify load order and that names (not folder paths) are expected.
  for(const auto& name : mod_names_in_order)
    command += " -mod=" + shellEscape(name);

  return command;
}
} // namespace cyberpunk_redmod
