#include "plugindeployer.h"
#include "pathutils.h"
#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <cctype>
#include <cstdint>
#include <numeric>
#include <ranges>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace sfs = std::filesystem;
namespace str = std::ranges;
namespace pu = path_utils;


PluginDeployer::PluginDeployer(const sfs::path& source_path,
                               const sfs::path& dest_path,
                               const std::string& name) : Deployer(source_path, dest_path, name)
{
  // make sure no hard link related checks are performed
  deploy_mode_ = copy;
  type_ = "Plugin Deployer";
  is_autonomous_ = true;
}

std::map<int, unsigned long> PluginDeployer::deploy(std::optional<ProgressNode*> progress_node)
{
  log_(Log::LOG_INFO, std::format("Deployer '{}': Updating plugins...", name_));
  restoreUndeployBackupIfExists();
  updatePlugins();
  updatePluginTags();
  updateSourceMods();
  return {};
}

std::map<int, unsigned long> PluginDeployer::deploy(const std::vector<int>& loadorder,
                                                    std::optional<ProgressNode*> progress_node)
{
  log_(Log::LOG_INFO, std::format("Deployer '{}': Updating plugins...", name_));
  restoreUndeployBackupIfExists();
  updatePlugins();
  updatePluginTags();
  updateSourceMods();
  return {};
}

void PluginDeployer::swapChild(int from_index, int to_index)
{
  if(to_index == from_index || to_index < 0 || to_index >= plugins_.size())
    return;
  iter_swap(plugins_.begin() + to_index, plugins_.begin() + from_index);
  if(tags_.size() == plugins_.size())
  {
    iter_swap(tags_.begin() + to_index, tags_.begin() + from_index);
  }
  writePluginTags();
  writePlugins();
}

void PluginDeployer::setModStatus(int mod_id, bool status)
{
  if(mod_id >= plugins_.size() || mod_id < 0)
    return;
  plugins_[mod_id].second = status;
  writePlugins();
}

std::vector<std::vector<int>> PluginDeployer::getConflictGroups() const
{
  std::vector<int> group(plugins_.size());
  std::iota(group.begin(), group.end(), 0);
  return { group };
}

std::vector<std::string> PluginDeployer::getModNames() const
{
  std::vector<std::string> names{};
  names.reserve(plugins_.size());
  for(int i = 0; i < plugins_.size(); i++)
    names.push_back(plugins_[i].first);
  return names;
}

void PluginDeployer::addProfile(int source)
{
  if(num_profiles_ == 0)
  {
    num_profiles_++;
    saveSettings();
    return;
  }
  if(source >= 0 && source <= num_profiles_ && num_profiles_ > 1 && source != current_profile_)
  {
    sfs::copy(dest_path_ / (hideFile(plugin_file_name_) + EXTENSION + std::to_string(source)),
              dest_path_ /
                (hideFile(plugin_file_name_) + EXTENSION + std::to_string(num_profiles_)));
  }
  else
  {
    sfs::copy(dest_path_ / plugin_file_name_,
              dest_path_ /
                (hideFile(plugin_file_name_) + EXTENSION + std::to_string(num_profiles_)));
  }
  num_profiles_++;
  saveSettings();
}

void PluginDeployer::removeProfile(int profile)
{
  if(profile >= num_profiles_ || profile < 0)
    return;
  std::string plugin_file = hideFile(plugin_file_name_) + EXTENSION + std::to_string(profile);
  if(profile == current_profile_)
    setProfile(0);
  else if(profile < current_profile_)
    setProfile(current_profile_ - 1);
  sfs::remove(dest_path_ / plugin_file);
  num_profiles_--;
  saveSettings();
}

void PluginDeployer::setProfile(int profile)
{
  if(profile >= num_profiles_ || profile < 0 || profile == current_profile_)
    return;
  if(!sfs::exists(dest_path_ / plugin_file_name_) ||
     !sfs::exists(dest_path_ / (hideFile(plugin_file_name_) + EXTENSION + std::to_string(profile))))
  {
    resetSettings();
    return;
  }
  sfs::rename(dest_path_ / plugin_file_name_,
              dest_path_ /
                (hideFile(plugin_file_name_) + EXTENSION + std::to_string(current_profile_)));
  sfs::rename(dest_path_ / (hideFile(plugin_file_name_) + EXTENSION + std::to_string(profile)),
              dest_path_ / plugin_file_name_);
  current_profile_ = profile;
  saveSettings();
  loadPlugins();
  updatePlugins();
}

void PluginDeployer::setConflictGroups(const std::vector<std::vector<int>>& newConflict_groups)
{
  log_(Log::LOG_DEBUG,
       std::string("WARNING: You are trying to set a load order for an autonomous deployer. ") +
         "This will have no effect.");
}

int PluginDeployer::getNumMods()
{
  return plugins_.size();
}

std::shared_ptr<TreeItem<DeployerEntry>> PluginDeployer::getLoadorder()
{
  auto loadorder = std::make_shared<TreeItem<DeployerEntry>>(std::make_shared<DeployerEntry>(true, "Root"), nullptr);
  for(const auto& [plugin, enabled] : plugins_)
  {
    auto iter = source_mods_.find(plugin);
    int id = -1;
    if(iter != source_mods_.end())
      id = iter->second;
    loadorder->emplace_back(make_shared<DeployerModInfo>(false, plugin, "", id, enabled));
  }
  return loadorder;
}

bool PluginDeployer::addMod(int mod_id, bool enabled, bool update_conflicts)
{
  log_(Log::LOG_DEBUG,
       std::string("WARNING: You are trying to add a mod to an autonomous deployer. ") +
         "This will have no effect.");
  return false;
}

bool PluginDeployer::removeMod(int mod_id)
{
  log_(Log::LOG_DEBUG,
       std::string("WARNING: You are trying to remove a mod from an autonomous deployer. ") +
         "This will have no effect.");
  return false;
}

bool PluginDeployer::hasMod(int mod_id)
{
  return false;
}

bool PluginDeployer::swapMod(int old_id, int new_id)
{
  log_(Log::LOG_DEBUG,
       std::string("WARNING: You are trying to swap a mod in an autonomous deployer. ") +
         "This will have no effect");
  return false;
}

std::vector<ConflictInfo> PluginDeployer::getFileConflicts(
  int mod_id,
  bool show_disabled,
  std::optional<ProgressNode*> progress_node)
{
  if(progress_node)
  {
    (*progress_node)->setTotalSteps(1);
    (*progress_node)->advance();
  }
  return {};
}

std::unordered_set<int> PluginDeployer::getModConflicts(int mod_id,
                                                        std::optional<ProgressNode*> progress_node)
{
  if(progress_node)
  {
    (*progress_node)->setTotalSteps(1);
    (*progress_node)->advance();
  }
  return {};
}

void PluginDeployer::sortModsByConflicts(std::optional<ProgressNode*> progress_node)
{
  if(progress_node)
  {
    (*progress_node)->setTotalSteps(1);
    (*progress_node)->advance();
  }
}

void PluginDeployer::cleanup()
{
  for(int i = 0; i < num_profiles_; i++)
  {
    sfs::path plugin_path =
      dest_path_ / (hideFile(plugin_file_name_) + EXTENSION + std::to_string(i));
    sfs::remove(plugin_path);
  }
  current_profile_ = 0;
  num_profiles_ = 1;
  sfs::remove(dest_path_ / config_file_name_);
}

std::vector<std::vector<std::string>> PluginDeployer::getAutoTags()
{
  return tags_;
}

std::map<std::string, int> PluginDeployer::getAutoTagMap()
{
  return {};
}

std::vector<std::pair<std::filesystem::path, int>> PluginDeployer::getExternallyModifiedFiles(
  std::optional<ProgressNode*> progress_node) const
{
  if(progress_node)
  {
    (*progress_node)->setTotalSteps(1);
    (*progress_node)->advance();
  }
  return {};
}

void PluginDeployer::keepOrRevertFileModifications(const FileChangeChoices& changes_to_keep) {}

void PluginDeployer::updateDeployedFilesForMod(int mod_id,
                                               std::optional<ProgressNode*> progress_node) const
{
  if(progress_node)
  {
    (*progress_node)->setTotalSteps(1);
    (*progress_node)->advance();
  }
}

void PluginDeployer::fixInvalidLinkDeployMode() {}

void PluginDeployer::setDeployMode(DeployMode deploy_mode)
{
  deploy_mode_ = copy;
}

int PluginDeployer::getDeployPriority() const
{
  return 1;
}

bool PluginDeployer::supportsFileConflicts() const
{
  return false;
}

bool PluginDeployer::supportsFileBrowsing() const
{
  return false;
}

bool PluginDeployer::idsAreSourceReferences() const
{
  return true;
}

std::vector<std::vector<int>> PluginDeployer::getValidModActions() const
{
  std::vector<std::vector<int>> valid_actions;
  for(int _ = 0; _ < plugins_.size(); _++)
    valid_actions.push_back({});
  return valid_actions;
}

void PluginDeployer::updatePlugins()
{
  std::vector<std::string> plugin_files;
  std::vector<std::pair<std::string, bool>> new_plugins;
  for(const auto& dir_entry : sfs::directory_iterator(source_path_))
  {
    if(dir_entry.is_directory())
      continue;
    const std::string file_name = dir_entry.path().filename().string();
    if(std::regex_match(file_name, plugin_regex_))
      plugin_files.push_back(file_name);
  }

  // The on-disk plugin state file may have been edited externally (by the game, a launcher,
  // xEdit, ...) since Limo last wrote it. Treat that file, when present, as the authoritative
  // source for enabled/disabled flags so external changes are not clobbered by Limo's stale
  // in-memory state. See limo-app/limo#134.
  const std::optional<std::map<std::string, bool>> external_state = readExternalPluginState();

  for(auto it = plugins_.begin(); it != plugins_.end(); it++)
  {
    if(str::find_if(plugin_files, [&it](const auto& s) { return it->first == s; }) !=
       plugin_files.end())
    {
      auto plugin = *it;
      // For plugins present in both Limo's state and the external file, prefer the externally
      // observed enabled flag over Limo's potentially stale stored value (limo-app/limo#134).
      if(external_state)
      {
        const auto ext_it = external_state->find(pu::toLowerCase(plugin.first));
        if(ext_it != external_state->end())
          plugin.second = ext_it->second;
      }
      new_plugins.emplace_back(std::move(plugin));
    }
    else
      // Plugin is listed in the load-order file but has no corresponding file on
      // disk.  Drop it from the active list and log a warning so users know why
      // it disappears.  This prevents LOOT's GetPlugin() from returning a null
      // pointer and crashing when the missing entry is later looked up
      // (limo-app/limo#185, limo-app/limo#31).
      log_(Log::LOG_WARNING,
           std::format("Deployer '{}': Plugin '{}' is listed in '{}' but not found "
                       "in '{}' — removing from load order.",
                       name_,
                       it->first,
                       plugin_file_name_,
                       source_path_.string()));
  }
  for(auto it = plugin_files.begin(); it != plugin_files.end(); it++)
  {
    if(str::find_if(new_plugins, [&it](auto& p) { return p.first == *it; }) != new_plugins.end())
      continue;
    // Newly discovered plugin file. If the external state file lists it, honor that flag instead
    // of unconditionally force-enabling it. Only default to enabled when there is no authoritative
    // external information for this plugin.
    bool enabled = true;
    if(external_state)
    {
      const auto ext_it = external_state->find(pu::toLowerCase(*it));
      if(ext_it != external_state->end())
        enabled = ext_it->second;
    }
    new_plugins.emplace_back(*it, enabled);
  }
  plugins_ = new_plugins;
  writePlugins();
}

std::optional<std::map<std::string, bool>> PluginDeployer::readExternalPluginState() const
{
  const sfs::path state_path = dest_path_ / plugin_file_name_;
  if(!sfs::exists(state_path))
    return {};

  std::ifstream plugin_file(state_path);
  if(!plugin_file.is_open())
    return {};

  std::map<std::string, bool> state;
  std::string line;
  while(getline(plugin_file, line))
  {
    std::smatch match;
    if(std::regex_match(line, match, plugin_file_line_regex_))
      // Key is lower cased to keep the case-insensitive plugin matching used elsewhere.
      state[pu::toLowerCase(std::string(match[2]))] = match[1] == "*";
  }
  plugin_file.close();
  return state;
}

void PluginDeployer::loadPlugins()
{
  plugins_.clear();
  std::string line;
  std::ifstream plugin_file;
  plugin_file.open(dest_path_ / plugin_file_name_);
  if(!plugin_file.is_open())
    throw std::runtime_error("Could not open " + plugin_file_name_ +
                             "!\nMake sure you have launched the game at least once.");

  while(getline(plugin_file, line))
  {
    std::smatch match;
    if(std::regex_match(line, match, plugin_file_line_regex_))
      plugins_.emplace_back(match[2], match[1] == "*");
  }
  plugin_file.close();
}

void PluginDeployer::writePlugins() const
{
  std::ofstream plugin_file(dest_path_ / plugin_file_name_);
  if(!plugin_file.is_open())
    throw std::runtime_error("Could not open " + plugin_file_name_ + "!");
  for(const auto& [name, status] : plugins_)
    plugin_file << (status ? "*" : "") << name << "\n";
  plugin_file.close();
}

void PluginDeployer::saveSettings() const
{
  Json::Value settings;
  settings["num_profiles"] = num_profiles_;
  settings["current_profile"] = current_profile_;
  sfs::path settings_file_path = dest_path_ / config_file_name_;
  std::ofstream file(settings_file_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not write to \"" + settings_file_path.string() + "\".");
  file << settings;
  file.close();
}

void PluginDeployer::loadSettings()
{
  Json::Value settings;
  sfs::path settings_file_path = dest_path_ / config_file_name_;
  if(!sfs::exists(settings_file_path))
  {
    resetSettings();
    return;
  }
  std::ifstream file(settings_file_path, std::fstream::binary);
  if(!file.is_open())
  {
    resetSettings();
    return;
  }
  file >> settings;
  file.close();
  if(!settings.isMember("num_profiles") || !settings.isMember("current_profile"))
  {
    resetSettings();
    return;
  }
  num_profiles_ = settings["num_profiles"].asInt();
  current_profile_ = settings["current_profile"].asInt();
}

void PluginDeployer::resetSettings()
{
  num_profiles_ = 1;
  current_profile_ = 0;
}

void PluginDeployer::writePluginTags() const
{
  Json::Value json;
  for(int i = 0; i < tags_.size(); i++)
  {
    for(int j = 0; j < tags_[i].size(); j++)
      json[i][j] = tags_.at(i).at(j);
  }

  const sfs::path tag_file_path = dest_path_ / tags_file_name_;
  std::ofstream file(tag_file_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not write to \"" + tag_file_path.string() + "\".");
  file << json;
  file.close();
}

void PluginDeployer::restoreUndeployBackupIfExists()
{
  const std::string plugin_backup_path =
    dest_path_ / (hideFile(plugin_file_name_) + UNDEPLOY_BACKUP_EXTENSION);
  if(sfs::exists(plugin_backup_path))
  {
    log_(Log::LOG_DEBUG, std::format("Deployer '{}': Restoring undeploy backup.", name_));
    sfs::remove(dest_path_ / plugin_file_name_);
    sfs::rename(plugin_backup_path, dest_path_ / plugin_file_name_);
    loadPlugins();
  }
}

void PluginDeployer::updateSourceMods()
{
  log_(Log::LOG_INFO, std::format("Deployer '{}': Finding source mods...", name_));
  source_mods_.clear();

  auto deployed_source_path = getRootOfTargetDirectory(source_path_);
  if(deployed_source_path)
    log_(Log::LOG_DEBUG, std::format("Source path: '{}'", deployed_source_path->string()));
  else
  {
    log_(Log::LOG_ERROR,
         std::format("Deployer '{}': Could not find deployed files at '{}'",
                     name_,
                     source_path_.string()));
    return;
  }
  auto deployed_files = loadDeployedFiles({}, *deployed_source_path);
  const sfs::path relative_path(pu::getRelativePath(source_path_, *deployed_source_path));
  for(const auto& [name, _] : plugins_)
  {
    auto iter = deployed_files.find((relative_path / name).string());
    if(iter != deployed_files.end())
      source_mods_[name] = iter->second;
  }
  writeSourceMods();
}

void PluginDeployer::writeSourceMods() const
{
  Json::Value json_object;
  for(const auto& [i, pair] : str::enumerate_view(source_mods_))
  {
    const auto& [plugin, source] = pair;
    json_object["source_mods"][(int)i]["plugin"] = plugin;
    json_object["source_mods"][(int)i]["source"] = source;
  }

  std::ofstream file(dest_path_ / source_mods_file_name_, std::ios::binary);
  if(!file.is_open())
  {
    log_(Log::LOG_ERROR,
         std::format(
           "Deployed '{}': Failed to write mod sources to '{}'", name_, dest_path_.string()));
    return;
  }
  file << json_object;
}

void PluginDeployer::readSourceMods()
{
  const sfs::path dest_path = dest_path_ / source_mods_file_name_;
  if(!sfs::exists(dest_path))
    return;

  std::ifstream file(dest_path, std::ios::binary);
  if(!file.is_open())
  {
    log_(Log::LOG_ERROR,
         std::format(
           "Deployed '{}': Failed to read mod sources from '{}'", name_, dest_path_.string()));
    return;
  }
  Json::Value json_object;
  file >> json_object;
  file.close();
  source_mods_.clear();
  for(int i = 0; i < json_object["source_mods"].size(); i++)
    source_mods_[json_object["source_mods"][i]["plugin"].asString()] =
      json_object["source_mods"][(int)i]["source"].asInt();
}

std::optional<sfs::path> PluginDeployer::getRootOfTargetDirectory(sfs::path target) const
{
  while(target.string() != "/" && !target.empty())
  {
    if(sfs::exists(target / deployed_files_name_))
      return { target };
    sfs::path new_target;
    const int length = pu::getPathLength(target);
    for(const auto& [i, part] : str::enumerate_view(target))
    {
      if(i == length - 1)
        break;
      new_target /= part;
    }
    target = new_target;
  }
  return {};
}

std::string PluginDeployer::hideFile(const std::string& name)
{
  return name.starts_with('.') ? name : "." + name;
}

namespace
{
/*! \brief Lower-cases an ASCII string for case-insensitive plugin/master comparison. */
std::string toLowerAscii(std::string str)
{
  for(char& c : str)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return str;
}

/*! \brief Reads a little-endian unsigned integer of the given byte width from a buffer. */
uint32_t readLE(const unsigned char* data, int num_bytes)
{
  uint32_t value = 0;
  for(int i = 0; i < num_bytes; i++)
    value |= static_cast<uint32_t>(data[i]) << (8 * i);
  return value;
}

/*!
 * \brief Extracts the NUL-terminated master name contained in a MAST field payload.
 * \param payload Pointer to the field data.
 * \param size Size of the field data in bytes.
 * \return The master file name with any trailing NUL bytes stripped.
 */
std::string parseMastPayload(const unsigned char* payload, uint32_t size)
{
  std::string name(reinterpret_cast<const char*>(payload), size);
  // MAST strings are NUL terminated; drop the terminator and anything past it.
  const auto nul_pos = name.find('\0');
  if(nul_pos != std::string::npos)
    name.resize(nul_pos);
  return name;
}
} // namespace

std::vector<std::string> PluginDeployer::readPluginMasters(const sfs::path& plugin_path) const
{
  std::vector<std::string> masters;

  std::error_code ec;
  if(!sfs::exists(plugin_path, ec) || ec)
    return masters;

  std::ifstream file(plugin_path, std::ios::binary);
  if(!file.is_open())
    return masters;

  // Read the 4 byte record signature, common to both TES3 and TES4 layouts.
  char signature[4] = { 0, 0, 0, 0 };
  if(!file.read(signature, 4))
    return masters;
  const std::string sig(signature, 4);

  if(sig == "TES3")
  {
    // TES3 (Morrowind / OpenMW) header layout:
    //   "TES3" | uint32 dataSize | uint32 unknown | uint32 flags | <subrecords...>
    // Each subrecord: 4 byte type | uint32 size | <size bytes payload>.
    // The data size counts only the subrecord block that follows the 16 byte header.
    unsigned char header[12];
    if(!file.read(reinterpret_cast<char*>(header), 12))
      return masters;
    const uint32_t data_size = readLE(header, 4);

    std::vector<unsigned char> block(data_size);
    if(data_size > 0 && !file.read(reinterpret_cast<char*>(block.data()), data_size))
      return masters;

    uint32_t pos = 0;
    while(pos + 8 <= data_size)
    {
      const std::string type(reinterpret_cast<const char*>(block.data() + pos), 4);
      const uint32_t size = readLE(block.data() + pos + 4, 4);
      pos += 8;
      if(pos + size > data_size) // truncated / inconsistent subrecord -> stop safely
        break;
      if(type == "MAST" && size > 0)
        masters.push_back(parseMastPayload(block.data() + pos, size));
      pos += size;
    }
    return masters;
  }

  if(sig == "TES4")
  {
    // TES4 (Oblivion / FO3 / FNV / Skyrim / FO4) header layout:
    //   24 byte record header: "TES4" | uint32 dataSize | uint32 flags | ... (rest unused here)
    //   followed by dataSize bytes of fields (subrecords).
    // Each field: 4 byte type | uint16 size | <size bytes payload>.
    // The signature already consumed 4 bytes; read the remaining 20 header bytes.
    unsigned char header[20];
    if(!file.read(reinterpret_cast<char*>(header), 20))
      return masters;
    const uint32_t data_size = readLE(header, 4); // bytes following the 24 byte record header

    std::vector<unsigned char> block(data_size);
    if(data_size > 0 && !file.read(reinterpret_cast<char*>(block.data()), data_size))
      return masters;

    uint32_t pos = 0;
    while(pos + 6 <= data_size)
    {
      const std::string type(reinterpret_cast<const char*>(block.data() + pos), 4);
      const uint32_t size = readLE(block.data() + pos + 4, 2);
      pos += 6;
      if(pos + size > data_size) // truncated / inconsistent field -> stop safely
        break;
      if(type == "MAST" && size > 0)
        masters.push_back(parseMastPayload(block.data() + pos, size));
      pos += size;
    }
    return masters;
  }

  // Unknown signature: not a recognized plugin header, report no masters.
  return masters;
}

std::vector<PluginDeployer::MissingMasterInfo> PluginDeployer::findMissingMasters() const
{
  // Build case-insensitive lookups of present plugins and currently enabled plugins.
  std::set<std::string> present;
  std::set<std::string> enabled;
  for(const auto& [name, is_enabled] : plugins_)
  {
    const std::string lower = toLowerAscii(name);
    present.insert(lower);
    if(is_enabled)
      enabled.insert(lower);
  }

  std::vector<MissingMasterInfo> result;
  for(const auto& [name, is_enabled] : plugins_)
  {
    if(!is_enabled) // only enabled plugins can crash the game on load
      continue;

    std::vector<std::string> masters;
    try
    {
      masters = readPluginMasters(source_path_ / name);
    }
    catch(...) // never let a malformed plugin abort the health check
    {
      continue;
    }

    MissingMasterInfo info;
    info.plugin = name;
    for(const auto& master : masters)
    {
      const std::string lower = toLowerAscii(master);
      if(!present.contains(lower))
        info.missing_masters.push_back(master);
      else if(!enabled.contains(lower))
        info.disabled_masters.push_back(master);
    }
    if(!info.missing_masters.empty() || !info.disabled_masters.empty())
      result.push_back(std::move(info));
  }

  // Log a concise summary of the detection result.
  if(result.empty())
    log_(Log::LOG_INFO,
         std::format("Deployer '{}': Master check passed, no missing or disabled masters.", name_));
  else
  {
    log_(Log::LOG_WARNING,
         std::format("Deployer '{}': {} plugin(s) have unmet master dependencies.",
                     name_,
                     result.size()));
    for(const auto& info : result)
    {
      if(!info.missing_masters.empty())
        log_(Log::LOG_WARNING,
             std::format("  '{}' is missing master(s): {}",
                         info.plugin,
                         std::accumulate(info.missing_masters.begin(),
                                         info.missing_masters.end(),
                                         std::string(),
                                         [](const std::string& a, const std::string& b)
                                         { return a.empty() ? b : a + ", " + b; })));
      if(!info.disabled_masters.empty())
        log_(Log::LOG_WARNING,
             std::format("  '{}' requires disabled master(s): {}",
                         info.plugin,
                         std::accumulate(info.disabled_masters.begin(),
                                         info.disabled_masters.end(),
                                         std::string(),
                                         [](const std::string& a, const std::string& b)
                                         { return a.empty() ? b : a + ", " + b; })));
    }
  }

  return result;
}

// fork #202: plugin ESM/ESL flag awareness.
PluginDeployer::PluginFlagInfo PluginDeployer::readPluginFlagInfo(
  const std::string& plugin_name,
  const sfs::path& plugin_path) const
{
  // Record-header flag bits in the TES4 header's 4 byte flags field.
  constexpr uint32_t FLAG_MASTER = 0x1;   // ESM / master
  constexpr uint32_t FLAG_LIGHT = 0x200;  // ESL / light

  PluginFlagInfo info;
  info.name = plugin_name;

  // Extension hint: a .esl file is always light, a .esm file always a master. This is honored
  // independently of whether the header could be read.
  std::string ext = sfs::path(plugin_name).extension().string();
  ext = toLowerAscii(ext);
  if(ext == ".esl")
    info.is_light = true;
  else if(ext == ".esm")
    info.is_master = true;

  std::error_code ec;
  if(!sfs::exists(plugin_path, ec) || ec)
    return info;

  std::ifstream file(plugin_path, std::ios::binary);
  if(!file.is_open())
    return info;

  // The leading record is a 24 byte TES4 header: "TES4" | uint32 dataSize | uint32 flags | ...
  // The flags field sits at offset 8, so reading the first 12 bytes is sufficient.
  unsigned char header[12];
  if(!file.read(reinterpret_cast<char*>(header), 12))
    return info;
  const std::string sig(reinterpret_cast<const char*>(header), 4);
  if(sig != "TES4")
    return info;

  info.exists = true;
  const uint32_t flags = readLE(header + 8, 4);
  if(flags & FLAG_MASTER)
    info.is_master = true;
  if(flags & FLAG_LIGHT)
    info.is_light = true;
  return info;
}

// fork #202: plugin ESM/ESL flag awareness.
std::vector<PluginDeployer::PluginFlagInfo> PluginDeployer::getPluginFlagInfo() const
{
  std::vector<PluginFlagInfo> result;
  result.reserve(plugins_.size());
  for(const auto& [name, _] : plugins_)
  {
    try
    {
      result.push_back(readPluginFlagInfo(name, source_path_ / name));
    }
    catch(...) // never let a malformed plugin abort the listing
    {
      PluginFlagInfo info;
      info.name = name;
      result.push_back(std::move(info));
    }
  }
  return result;
}

// fork #202: plugin ESM/ESL flag awareness.
std::pair<std::vector<PluginDeployer::PluginFlagInfo>, std::pair<int, int>>
PluginDeployer::getPluginFlagInfoWithCounts() const
{
  std::vector<PluginFlagInfo> plugins = getPluginFlagInfo();
  int full_count = 0;
  int light_count = 0;
  for(const auto& info : plugins)
  {
    if(info.is_light)
      light_count++;
    else
      full_count++;
  }
  return { std::move(plugins), { full_count, light_count } };
}
