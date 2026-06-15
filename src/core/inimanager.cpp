/*!
 * \file inimanager.cpp
 * \brief Implementation of the IniManager class.
 */

#include "inimanager.h"
#include "log.h"
#include <fstream>
#include <system_error>

namespace sfs = std::filesystem;


std::filesystem::path IniManager::profileStorePath(const std::filesystem::path& staging_dir,
                                                   int profile)
{
  return staging_dir / STORE_DIR_NAME / std::to_string(profile);
}

void IniManager::saveProfileIni(const std::filesystem::path& staging_dir,
                                int profile,
                                const std::vector<std::filesystem::path>& ini_files)
{
  const sfs::path store = profileStorePath(staging_dir, profile);
  std::error_code ec;
  sfs::create_directories(store, ec);
  if(ec)
  {
    Log::error("IniManager: Could not create profile INI store '" + store.string() +
               "': " + ec.message());
    return;
  }

  for(const auto& source : ini_files)
  {
    if(!sfs::exists(source, ec))
    {
      Log::warning("IniManager: INI file '" + source.string() +
                   "' does not exist; skipping save.");
      continue;
    }
    const sfs::path dest = store / source.filename();
    sfs::copy_file(source, dest, sfs::copy_options::overwrite_existing, ec);
    if(ec)
    {
      Log::error("IniManager: Failed to store INI file '" + source.string() + "' -> '" +
                 dest.string() + "': " + ec.message());
      ec.clear();
    }
    else
      Log::debug("IniManager: Stored INI file '" + source.string() + "' for profile " +
                 std::to_string(profile) + ".");
  }
}

void IniManager::applyProfileIni(const std::filesystem::path& staging_dir,
                                 int profile,
                                 const std::vector<std::filesystem::path>& ini_targets)
{
  const sfs::path store = profileStorePath(staging_dir, profile);
  std::error_code ec;
  if(!sfs::exists(store, ec))
  {
    Log::debug("IniManager: No stored INI files for profile " + std::to_string(profile) +
               "; nothing to apply.");
    return;
  }

  for(const auto& target : ini_targets)
  {
    const sfs::path source = store / target.filename();
    if(!sfs::exists(source, ec))
    {
      Log::debug("IniManager: No stored INI file for target '" + target.string() +
                 "' in profile " + std::to_string(profile) + "; skipping.");
      continue;
    }
    if(target.has_parent_path())
    {
      sfs::create_directories(target.parent_path(), ec);
      ec.clear();
    }
    sfs::copy_file(source, target, sfs::copy_options::overwrite_existing, ec);
    if(ec)
    {
      Log::error("IniManager: Failed to apply INI file '" + source.string() + "' -> '" +
                 target.string() + "': " + ec.message());
      ec.clear();
    }
    else
      Log::debug("IniManager: Applied INI file '" + target.string() + "' for profile " +
                 std::to_string(profile) + ".");
  }
}

std::string IniManager::trim(const std::string& str)
{
  const auto begin = str.find_first_not_of(" \t\r\n");
  if(begin == std::string::npos)
    return "";
  const auto end = str.find_last_not_of(" \t\r\n");
  return str.substr(begin, end - begin + 1);
}

std::optional<std::vector<std::string>> IniManager::readLines(const std::filesystem::path& path)
{
  std::ifstream file(path);
  if(!file.is_open())
    return std::nullopt;
  std::vector<std::string> lines;
  std::string line;
  while(std::getline(file, line))
  {
    if(!line.empty() && line.back() == '\r')
      line.pop_back();
    lines.push_back(line);
  }
  return lines;
}

std::optional<std::string> IniManager::getValue(const std::filesystem::path& ini_file,
                                                const std::string& section,
                                                const std::string& key)
{
  const auto lines = readLines(ini_file);
  if(!lines)
  {
    Log::debug("IniManager: Could not open INI file '" + ini_file.string() + "' for reading.");
    return std::nullopt;
  }

  std::string current_section;
  for(const auto& raw : *lines)
  {
    const std::string line = trim(raw);
    if(line.empty() || line[0] == ';' || line[0] == '#')
      continue;
    if(line.front() == '[' && line.back() == ']')
    {
      current_section = trim(line.substr(1, line.size() - 2));
      continue;
    }
    if(current_section != section)
      continue;
    const auto eq = line.find('=');
    if(eq == std::string::npos)
      continue;
    if(trim(line.substr(0, eq)) == key)
      return trim(line.substr(eq + 1));
  }
  return std::nullopt;
}

bool IniManager::setValue(const std::filesystem::path& ini_file,
                          const std::string& section,
                          const std::string& key,
                          const std::string& value)
{
  std::vector<std::string> lines;
  if(const auto existing = readLines(ini_file))
    lines = *existing;

  const std::string new_line = key + "=" + value;
  bool in_target_section = section.empty();
  bool key_written = false;
  // Index of the last line belonging to the target section (used for appending
  // a new key when the key was not found within an existing section).
  int section_end = -1;
  // Whether the target section header was found at all.
  bool section_found = section.empty();

  for(int i = 0; i < static_cast<int>(lines.size()); ++i)
  {
    const std::string line = trim(lines[i]);
    if(!line.empty() && line.front() == '[' && line.back() == ']')
    {
      // Leaving a section: if we were in the target section and never wrote the
      // key, this is where it should be appended.
      if(in_target_section && !key_written)
        section_end = i - 1;
      const std::string header = trim(line.substr(1, line.size() - 2));
      in_target_section = (header == section);
      if(in_target_section)
        section_found = true;
      continue;
    }
    if(in_target_section && !key_written && !line.empty() && line[0] != ';' && line[0] != '#')
    {
      const auto eq = line.find('=');
      if(eq != std::string::npos && trim(line.substr(0, eq)) == key)
      {
        lines[i] = new_line;
        key_written = true;
      }
    }
    if(in_target_section)
      section_end = i;
  }

  if(!key_written)
  {
    if(!section_found)
    {
      // Create the section (and key) at the end of the file.
      if(!lines.empty() && !trim(lines.back()).empty())
        lines.push_back("");
      lines.push_back("[" + section + "]");
      lines.push_back(new_line);
    }
    else if(section_end >= 0 && section_end + 1 <= static_cast<int>(lines.size()))
    {
      lines.insert(lines.begin() + section_end + 1, new_line);
    }
    else
    {
      lines.push_back(new_line);
    }
  }

  std::error_code ec;
  if(ini_file.has_parent_path())
  {
    sfs::create_directories(ini_file.parent_path(), ec);
    ec.clear();
  }

  std::ofstream out(ini_file, std::ios::trunc);
  if(!out.is_open())
  {
    Log::error("IniManager: Could not open INI file '" + ini_file.string() + "' for writing.");
    return false;
  }
  for(const auto& line : lines)
    out << line << '\n';
  return out.good();
}
