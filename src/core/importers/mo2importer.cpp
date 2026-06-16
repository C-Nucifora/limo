/*!
 * \file mo2importer.cpp
 * \brief Implementation of the Mo2Importer class.
 *
 * Implements fork issue #45 / limo-app/limo#92.
 */

#include "mo2importer.h"
#include <algorithm>
#include <format>
#include <fstream>
#include <set>
#include <stdexcept>

namespace sfs = std::filesystem;


// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

bool Mo2Importer::isValidInstance(const sfs::path& instance_path)
{
  if(!sfs::is_directory(instance_path))
    return false;

  // Must have a mods/ sub-directory
  if(!sfs::is_directory(instance_path / "mods"))
    return false;

  // Must have at least one profile with a modlist.txt
  const sfs::path profiles_dir = instance_path / "profiles";
  if(!sfs::is_directory(profiles_dir))
    return false;

  for(const auto& entry : sfs::directory_iterator(profiles_dir))
  {
    if(entry.is_directory() && sfs::is_regular_file(entry.path() / "modlist.txt"))
      return true;
  }
  return false;
}

std::vector<std::string> Mo2Importer::listProfiles(const sfs::path& instance_path)
{
  const sfs::path profiles_dir = instance_path / "profiles";
  if(!sfs::is_directory(profiles_dir))
    return {};

  std::vector<std::string> result;
  for(const auto& entry : sfs::directory_iterator(profiles_dir))
  {
    if(entry.is_directory() && sfs::is_regular_file(entry.path() / "modlist.txt"))
      result.push_back(entry.path().filename().string());
  }
  std::sort(result.begin(), result.end());
  return result;
}

Mo2ParseResult Mo2Importer::parseProfile(const sfs::path& instance_path,
                                         const std::string& profile_name)
{
  if(!sfs::is_directory(instance_path))
    throw std::runtime_error(
      std::format("MO2 instance path does not exist or is not a directory: '{}'",
                  instance_path.string()));

  const sfs::path mods_dir = instance_path / "mods";
  if(!sfs::is_directory(mods_dir))
    throw std::runtime_error(
      std::format("No 'mods' directory found inside MO2 instance: '{}'", instance_path.string()));

  const sfs::path profile_dir = instance_path / "profiles" / profile_name;
  if(!sfs::is_directory(profile_dir))
    throw std::runtime_error(std::format(
      "MO2 profile '{}' not found inside '{}'", profile_name, (instance_path / "profiles").string()));

  const sfs::path modlist_path = profile_dir / "modlist.txt";
  if(!sfs::is_regular_file(modlist_path))
    throw std::runtime_error(
      std::format("modlist.txt not found in profile '{}'", profile_dir.string()));

  Mo2ParseResult result;
  result.profile_name = profile_name;

  // Collect the physical mod folders
  const auto folder_names = listModFolders(mods_dir);
  std::set<std::string> folders_set(folder_names.begin(), folder_names.end());

  // Parse modlist.txt — entries come back lowest-priority-first (index 0 = first deployed)
  const auto modlist = readModlist(modlist_path);

  // Build entries for mods that appear in modlist.txt
  std::set<std::string> seen;
  int idx = 0;
  for(const auto& [name, enabled] : modlist)
  {
    // Reject path-traversal / non-filename entries: a mod name must be a single,
    // normal path component (no separators, no '.' or '..').  This prevents a
    // crafted modlist.txt from escaping the mods directory.
    const sfs::path name_path(name);
    if(name.empty() || name.find('/') != std::string::npos ||
       name.find('\\') != std::string::npos || !name_path.has_filename() ||
       name_path.filename() != name_path || name == "." || name == "..")
    {
      result.warnings.push_back(std::format(
        "Mod '{}' in modlist.txt has an invalid name (path separators or traversal) — skipped.",
        name));
      continue;
    }

    const sfs::path mod_path = mods_dir / name;
    if(!sfs::is_directory(mod_path))
    {
      // Present in modlist but missing on disk — warn and skip
      result.warnings.push_back(
        std::format("Mod '{}' listed in modlist.txt but folder not found on disk — skipped.", name));
      continue;
    }
    Mo2ModEntry entry;
    entry.name = name;
    entry.source_path = mod_path;
    entry.enabled = enabled;
    entry.load_order_index = idx++;
    result.mods.push_back(std::move(entry));
    seen.insert(name);
  }

  // Append any folders not mentioned in modlist.txt (disabled, order undefined)
  for(const auto& folder : folder_names)
  {
    if(seen.count(folder))
      continue;
    result.warnings.push_back(
      std::format("Mod folder '{}' not found in modlist.txt — appended as disabled.", folder));
    Mo2ModEntry entry;
    entry.name = folder;
    entry.source_path = mods_dir / folder;
    entry.enabled = false;
    entry.load_order_index = idx++;
    result.mods.push_back(std::move(entry));
  }

  return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::vector<std::pair<std::string, bool>> Mo2Importer::readModlist(
  const sfs::path& modlist_path)
{
  std::ifstream file(modlist_path);
  if(!file.is_open())
    throw std::runtime_error(
      std::format("Cannot open modlist.txt at '{}'", modlist_path.string()));

  // MO2 lists mods highest-priority first.  We collect them in that order,
  // then reverse so index 0 = lowest priority = deployed first (overridable).
  std::vector<std::pair<std::string, bool>> entries; // {name, enabled}
  std::string line;
  while(std::getline(file, line))
  {
    if(line.empty())
      continue;
    // Strip trailing carriage return (Windows line endings)
    if(!line.empty() && line.back() == '\r')
      line.pop_back();
    if(line.empty())
      continue;

    const char prefix = line.front();
    if(prefix == '+' || prefix == '-')
    {
      const bool enabled = (prefix == '+');
      entries.emplace_back(line.substr(1), enabled);
    }
    // Lines starting with '*', '#', or anything else are separators/comments — skip.
  }

  // Reverse: MO2 top-of-list = highest priority wins conflicts.
  // Limo load order: last in list wins.  So the MO2 top entry becomes the
  // last Limo entry (highest index), which is what we want for identical
  // conflict behaviour.
  std::reverse(entries.begin(), entries.end());
  return entries;
}

std::vector<std::string> Mo2Importer::listModFolders(const sfs::path& mods_dir)
{
  std::vector<std::string> result;
  if(!sfs::is_directory(mods_dir))
    return result;
  for(const auto& entry : sfs::directory_iterator(mods_dir))
  {
    if(entry.is_directory())
      result.push_back(entry.path().filename().string());
  }
  std::sort(result.begin(), result.end());
  return result;
}
