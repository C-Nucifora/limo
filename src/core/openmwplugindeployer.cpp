#include "openmwplugindeployer.h"
#include "pathutils.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <json/json.h>
#include <map>
#include <queue>
#include <ranges>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace sfs = std::filesystem;
namespace str = std::ranges;
namespace pu = path_utils;


OpenMwPluginDeployer::OpenMwPluginDeployer(const sfs::path& source_path,
                                           const sfs::path& dest_path,
                                           const std::string& name) :
  LootDeployer(source_path, dest_path, name, false, false)
{
  // make sure no hard link related checks are performed
  deploy_mode_ = copy;
  type_ = "OpenMW Plugin Deployer";
  is_autonomous_ = true;
  app_type_ = loot::GameType::openmw;
  plugin_regex_ =
    std::regex(R"(.*\.(?:es[pm]|omwscripts|omwaddon|omwgame)$)", std::regex_constants::icase);
  plugin_file_line_regex_ = std::regex(
    R"(^\s*(\*?)([^#]*\.(?:es[pm]|omwscripts|omwaddon|omwgame))(\r?))", std::regex_constants::icase);
  config_file_name_ = ".plugin_config";
  source_mods_file_name_ = ".plugin_mod_sources";
  plugin_file_name_ = ".plugins.txt";
  tags_file_name_ = ".omwplugin_tags";
  bool initialized_plugins = initPluginFile();
  if(!initialized_plugins)    loadPlugins();
  readPluginTags();
  updatePlugins();
  if(initialized_plugins)
    updatePluginTagsPrivate();
  if(sfs::exists(dest_path_ / config_file_name_))
    loadSettings();
  readSourceMods();
  writePluginsPrivate();
  writePluginTagsPrivate();
  saveSettings();
}

void OpenMwPluginDeployer::unDeploy(std::optional<ProgressNode*> progress_node)
{
  const std::string plugin_backup_path =
    dest_path_ / ("." + plugin_file_name_ + UNDEPLOY_BACKUP_EXTENSION);
  if(!pu::exists(plugin_backup_path))
    sfs::copy(dest_path_ / plugin_file_name_, plugin_backup_path);

  log_(Log::LOG_INFO, std::format("Deployer '{}': Updating plugins...", name_));
  updatePlugins();
  // fork #87: OpenMW data= entry (VFS) deploy mode
  // Remove Limo's managed data= block on undeploy so openmw.cfg no longer references the
  // staging directories. No-op (apart from a harmless rewrite) when the mode was never used.
  if(use_data_entries_)
    writeDataEntries(false);
}

std::vector<std::vector<int>> OpenMwPluginDeployer::getConflictGroups() const
{
  std::vector<std::vector<int>> groups;
  for(int i = 0; i < 3; i++)
    groups.push_back({});

  std::regex script_regex(R"(.*\.omwscripts$)", std::regex_constants::icase);
  for(const auto& [i, pair] : str::enumerate_view(plugins_))
  {
    const auto& [plugin, enabled] = pair;
    if(std::regex_match(plugin, script_regex))
      groups[0].push_back(i);
    else if(groundcover_plugins_.contains(plugin))
      groups[1].push_back(i);
    else
      groups[2].push_back(i);
  }

  return groups;
}

std::map<std::string, int> OpenMwPluginDeployer::getAutoTagMap()
{
  return { { GROUNDCOVER_TAG, num_groundcover_plugins_ },
           { OPENMW_TAG, num_openmw_plugins_ },
           { ES_PLUGIN_TAG, num_es_plugins_ } };
}

bool OpenMwPluginDeployer::sortPluginsWithLoot(std::optional<ProgressNode*> progress_node)
{
  if(progress_node)
  {
    (*progress_node)->addChildren({ 1, 2, 5, 0.2f });
    (*progress_node)->child(0).setTotalSteps(1);
    (*progress_node)->child(1).setTotalSteps(1);
    (*progress_node)->child(2).setTotalSteps(1);
    (*progress_node)->child(3).setTotalSteps(1);
  }

  // OpenMW shares Morrowind's masterlist. This list historically only targeted older
  // metadata syntax versions, so a missing or incompatible list must not abort the sort.
  // Any failure here is reported by the caller, which then keeps the existing load order.
  updateMasterList();
  if(progress_node)
    (*progress_node)->child(0).advance();

  const sfs::path master_list_path = dest_path_ / "masterlist.yaml";
  if(!sfs::exists(master_list_path))
  {
    log_(Log::LOG_WARNING,
         std::format("Deployer '{}': Could not find masterlist.yaml at '{}'.",
                     name_,
                     master_list_path.string()));
    return false;
  }

  auto loot_handle = loot::CreateGameHandle(app_type_, source_path_, dest_path_);
  const sfs::path prelude_path(dest_path_ / "prelude.yaml");
  if(sfs::exists(prelude_path))
    loot_handle->GetDatabase().LoadMasterlistWithPrelude(master_list_path, prelude_path);
  else
    loot_handle->GetDatabase().LoadMasterlist(master_list_path);
  const sfs::path user_list_path(dest_path_ / "userlist.yaml");
  if(sfs::exists(user_list_path))
    loot_handle->GetDatabase().LoadUserlist(user_list_path);
  if(progress_node)
    (*progress_node)->child(1).advance();

  std::vector<sfs::path> plugin_paths;
  std::vector<std::string> plugin_file_names;
  plugin_paths.reserve(plugins_.size());
  plugin_file_names.reserve(plugins_.size());
  for(const auto& [path, s] : plugins_)
  {
    plugin_paths.emplace_back(source_path_ / path);
    plugin_file_names.emplace_back(path);
  }
  loot_handle->LoadPlugins(plugin_paths, false);
  auto sorted_plugins = loot_handle->SortPlugins(plugin_file_names);
  if(progress_node)
    (*progress_node)->child(2).advance();

  std::vector<std::pair<std::string, bool>> new_plugins;
  new_plugins.reserve(plugins_.size());
  for(const auto& plugin : sorted_plugins)
  {
    auto iter = str::find_if(plugins_, [&plugin](const auto& p) { return p.first == plugin; });
    bool enabled = iter != plugins_.end() ? iter->second : true;
    new_plugins.emplace_back(plugin, enabled);

    const auto cur_plugin = loot_handle->GetPlugin(plugin);
    for(const auto& master : cur_plugin->GetMasters())
    {
      if(!pu::pathExists(master, source_path_) && enabled)
        log_(Log::LOG_WARNING,
             "LOOT: Plugin '" + master + "' is missing but required for '" + plugin + "'");
    }
    auto meta_data = loot_handle->GetDatabase().GetPluginMetadata(plugin);
    if(!meta_data)
      continue;
    for(const auto& req : meta_data->GetRequirements())
    {
      std::string file = static_cast<std::string>(req.GetName());
      if(!pu::pathExists(file, source_path_))
        log_(Log::LOG_WARNING, "LOOT: Requirement '" + file + "' not met for '" + plugin + "'");
    }
  }

  if(enable_unsafe_sorting_)
    plugins_ = new_plugins;
  log_(Log::LOG_INFO,
       std::format("Deployer '{}': Sorted {} OpenMW content files using LOOT.",
                   name_,
                   new_plugins.size()));
  if(progress_node)
    (*progress_node)->child(3).advance();
  return true;
}

void OpenMwPluginDeployer::sortModsByConflicts(std::optional<ProgressNode*> progress_node)
{
  // Use libloot's masterlist based sorting for OpenMW (libloot now supports
  // loot::GameType::openmw). If no usable masterlist is available, fall back to the
  // existing grouping-only order instead of aborting the sort.
  try
  {
    if(!sortPluginsWithLoot(progress_node))
      log_(Log::LOG_INFO,
           std::format("Deployer '{}': LOOT sorting unavailable, keeping current order.", name_));
  }
  catch(const std::exception& e)
  {
    log_(Log::LOG_WARNING,
         std::format("Deployer '{}': LOOT sorting failed ({}), keeping current order.",
                     name_,
                     e.what()));
  }

  // Optional PLOX/mlox rules-based ordering step. When a rules file is present this
  // reorders plugins to satisfy its [Order]/[Near] constraints; otherwise it is a
  // no-op and the LOOT-derived order above is kept.
  applyPloxRules();

  // Always enforce OpenMW's grouping: scripts, then groundcover, then regular plugins.
  auto groups = getConflictGroups();
  std::vector<std::pair<std::string, bool>> new_plugins;
  new_plugins.reserve(plugins_.size());
  for(const auto& group : groups)
  {
    for(int mod_id : group)
      new_plugins.push_back(plugins_[mod_id]);
  }
  plugins_ = new_plugins;
  updatePluginTags();
  writePlugins();
}

std::vector<std::pair<std::string, std::string>> OpenMwPluginDeployer::getModActions() const
{
  return { { "Add Groundcover Tag", "tag-new" }, { "Remove Groundcover Tag", "tag-delete" } };
}

std::vector<std::vector<int>> OpenMwPluginDeployer::getValidModActions() const
{
  std::vector<std::vector<int>> valid_actions;
  for(const auto& [plugin, enabled] : plugins_)
  {
    auto iter = tag_map_.find(plugin);
    if(iter != tag_map_.end() && iter->second.contains(SCRIPTS_PLUGIN_TAG))
      valid_actions.push_back({});
    else if(!groundcover_plugins_.contains(plugin))
      valid_actions.push_back({ 0 });
    else
      valid_actions.push_back({ 1 });
  }
  return valid_actions;
}

void OpenMwPluginDeployer::applyModAction(int action, int mod_id)
{
  if(action == ACTION_ADD_GROUNDCOVER_TAG)
  {
    groundcover_plugins_.insert(plugins_[mod_id].first);
    num_groundcover_plugins_++;
  }
  else if(action == ACTION_REMOVE_GROUNDCOVER_TAG)
  {
    groundcover_plugins_.erase(plugins_[mod_id].first);
    num_groundcover_plugins_--;
  }
  else
    log_(Log::LOG_DEBUG, std::format("Invalid mod action: {}", action));

  updateTagVector();
  writePluginTags();
  writePlugins();
}

void OpenMwPluginDeployer::addProfile(int source)
{
  // we don't want the changes made to profiles by LootDeployer
  PluginDeployer::addProfile(source);
}

void OpenMwPluginDeployer::removeProfile(int profile)
{
  // we don't want the changes made to profiles by LootDeployer
  PluginDeployer::removeProfile(profile);
}

void OpenMwPluginDeployer::setProfile(int profile)
{
  // we don't want the changes made to profiles by LootDeployer
  PluginDeployer::setProfile(profile);
}

void OpenMwPluginDeployer::writePlugins() const
{
  writePluginsPrivate();
}

void OpenMwPluginDeployer::updatePlugins()
{
  // Collect all plugin filenames present in the source directory.
  std::vector<std::string> plugin_files;
  for(const auto& dir_entry : sfs::directory_iterator(source_path_))
  {
    if(dir_entry.is_directory())
      continue;
    const std::string file_name = dir_entry.path().filename().string();
    if(std::regex_match(file_name, plugin_regex_))
      plugin_files.push_back(file_name);
  }

  // Helper: ASCII lower-case for case-insensitive comparison.
  auto to_lower = [](std::string s) -> std::string {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
  };

  // Re-build the plugin list.
  // Phase 1: keep existing entries whose on-disk file still exists (case-
  //          insensitive match so that e.g. "MyMod.ESP" matches "mymod.esp").
  //          Crucially, the stored name (and therefore enabled/disabled state)
  //          is preserved; the canonical on-disk spelling replaces it so that
  //          subsequent writes use the actual filename.
  std::vector<std::pair<std::string, bool>> new_plugins;
  for(const auto& existing : plugins_)
  {
    const std::string existing_lower = to_lower(existing.first);
    auto it = str::find_if(plugin_files,
                           [&](const std::string& disk_name) {
                             return to_lower(disk_name) == existing_lower;
                           });
    if(it != plugin_files.end())
    {
      // Use the on-disk spelling but keep the saved enabled/disabled state.
      new_plugins.emplace_back(*it, existing.second);
    }
  }

  // Phase 2: add newly discovered plugins (not already represented) as enabled.
  for(const auto& disk_name : plugin_files)
  {
    const std::string disk_lower = to_lower(disk_name);
    bool already_present =
      str::find_if(new_plugins, [&](const auto& p) {
        return to_lower(p.first) == disk_lower;
      }) != new_plugins.end();
    if(!already_present)
      new_plugins.emplace_back(disk_name, true);
  }

  plugins_ = new_plugins;
  writePlugins();
}

bool OpenMwPluginDeployer::initPluginFile()
{
  const sfs::path plugin_file_path = dest_path_ / plugin_file_name_;
  if(sfs::exists(plugin_file_path))
    return false;

  const sfs::path config_file_path = dest_path_ / OPEN_MW_CONFIG_FILE_NAME;
  std::ifstream in_file(config_file_path);
  if(!in_file.is_open())
    throw std::runtime_error(std::format("Error: Could not open '{}'.", config_file_path.string()));

  std::string line;
  std::regex plugin_regex(R"(^content=(.*\.(?:es[pm]|omwscripts|omwaddon|omwgame)))",
                          std::regex_constants::icase);
  std::regex groundcover_regex(R"(^groundcover=(.*\.(?:es[pm]|omwscripts|omwaddon|omwgame)))",
                               std::regex_constants::icase);
  num_groundcover_plugins_ = 0;
  while(getline(in_file, line))
  {
    std::smatch match;
    if(std::regex_match(line, match, plugin_regex))
      plugins_.emplace_back(match[1], true);
    else if(std::regex_match(line, match, groundcover_regex))
    {
      plugins_.emplace_back(match[1], true);
      groundcover_plugins_.insert(match[1]);
      num_groundcover_plugins_++;
    }
  }

  updateTagVector();
  PluginDeployer::writePlugins();
  return true;
}

void OpenMwPluginDeployer::readPluginTags()
{
  const sfs::path tag_file_path = dest_path_ / tags_file_name_;
  if(!sfs::exists(tag_file_path))
  {
    updatePluginTagsPrivate();
    return;
  }
  tag_map_.clear();
  groundcover_plugins_.clear();
  num_groundcover_plugins_ = 0;
  num_openmw_plugins_ = 0;
  num_es_plugins_ = 0;
  num_scripts_plugins_ = 0;
  std::ifstream file(tag_file_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not read from \"" + tag_file_path.string() + "\".");
  Json::Value json;
  file >> json;
  file.close();
  for(int i = 0; i < json.size(); i++)
  {
    const std::string plugin = json[i]["plugin"].asString();
    tag_map_[plugin] = {};
    for(int j = 0; j < json[i]["tags"].size(); j++)
    {
      const std::string tag = json[i]["tags"][j].asString();
      if(tag == GROUNDCOVER_TAG)
      {
        groundcover_plugins_.insert(plugin);
        num_groundcover_plugins_++;
      }
      else if(tag == OPENMW_TAG)
      {
        tag_map_[plugin].insert(tag);
        num_openmw_plugins_++;
      }
      else if(tag == ES_PLUGIN_TAG)
      {
        tag_map_[plugin].insert(tag);
        num_es_plugins_++;
      }
      else if(tag == SCRIPTS_PLUGIN_TAG)
      {
        tag_map_[plugin].insert(tag);
        num_scripts_plugins_++;
      }
    }
  }
  updateTagVector();
}

void OpenMwPluginDeployer::writePluginTags() const
{
  writePluginTagsPrivate();
}

void OpenMwPluginDeployer::updatePluginTags()
{
  updatePluginTagsPrivate();
}

void OpenMwPluginDeployer::updateTagVector()
{
  tags_.clear();
  for(const auto& [plugin, _] : plugins_)
  {
    tags_.push_back({});
    auto iter = tag_map_.find(plugin);
    if(iter != tag_map_.end())
    {
      for(const auto& tag : iter->second)
        tags_.back().push_back(tag);
    }
    if(groundcover_plugins_.contains(plugin))
      tags_.back().push_back(GROUNDCOVER_TAG);
  }
}

void OpenMwPluginDeployer::updatePluginTagsPrivate()
{
  std::regex omw_regex(R"(.*\.(?:omwscripts|omwaddon|omwgame))", std::regex_constants::icase);
  std::regex script_regex(R"(.*\.omwscripts)", std::regex_constants::icase);
  std::regex es_regex(R"(.*\.es[pm])", std::regex_constants::icase);
  num_openmw_plugins_ = 0;
  num_es_plugins_ = 0;
  num_scripts_plugins_ = 0;
  for(const auto& [i, pair] : str::enumerate_view(plugins_))
  {
    const auto& [plugin, _] = pair;
    if(std::regex_match(plugin, omw_regex))
    {
      tag_map_[plugin].insert(OPENMW_TAG);
      num_openmw_plugins_++;
    }
    if(std::regex_match(plugin, es_regex))
    {
      tag_map_[plugin].insert(ES_PLUGIN_TAG);
      num_es_plugins_++;
    }
    if(std::regex_match(plugin, script_regex))
    {
      tag_map_[plugin].insert(SCRIPTS_PLUGIN_TAG);
      num_scripts_plugins_++;
    }
  }
  updateTagVector();
  writePluginTagsPrivate();
}

void OpenMwPluginDeployer::writePluginTagsPrivate() const
{
  Json::Value json;
  for(const auto& [i, pair] : str::enumerate_view(tag_map_))
  {
    const auto& [plugin, tags] = pair;
    json[(int)i]["plugin"] = plugin;
    for(const auto& [j, tag] : str::enumerate_view(tags))
      json[(int)i]["tags"][(int)j] = tag;
    if(groundcover_plugins_.contains(plugin))
      json[(int)i]["tags"][json[(int)i]["tags"].size()] = GROUNDCOVER_TAG;
  }

  const sfs::path tag_file_path = dest_path_ / tags_file_name_;
  std::ofstream file(tag_file_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not write to \"" + tag_file_path.string() + "\".");
  file << json;
  file.close();
}

void OpenMwPluginDeployer::writePluginsToOpenMwConfig(const std::string& line_prefix,
                                                      const std::regex& line_regex,
                                                      std::function<bool(int)> plugin_filter) const
{
  const sfs::path plugin_file_path = dest_path_ / OPEN_MW_CONFIG_FILE_NAME;
  std::ifstream in_file(plugin_file_path);
  if(!in_file.is_open())
    throw std::runtime_error(std::format("Error: Could not open '{}'.", plugin_file_path.string()));

  std::vector<std::string> lines;
  int target_line = -1;
  bool found_target = false;
  std::string line;
  int i = 0;
  while(getline(in_file, line))
  {
    std::smatch match;
    if(std::regex_match(line, match, line_regex))
    {
      if(!found_target)
        target_line = i;
      found_target = true;
    }
    else
      lines.push_back(line);
    i++;
  }
  in_file.close();

  std::ofstream out_file(plugin_file_path);
  if(!out_file.is_open())
    throw std::runtime_error(std::format("Error: Could not open '{}'.", plugin_file_path.string()));

  for(const auto& [i, line] : str::enumerate_view(lines))
  {
    if(i == target_line)
    {
      for(const auto& [i, pair] : str::enumerate_view(plugins_))
      {
        if(plugin_filter(i))
          out_file << line_prefix + pair.first + "\n";
      }
    }
    out_file << line << "\n";
  }
  if(target_line == -1 || target_line >= lines.size())
  {
    for(const auto& [i, pair] : str::enumerate_view(plugins_))
    {
      if(plugin_filter(i))
        out_file << line_prefix + pair.first + "\n";
    }
  }
}

void OpenMwPluginDeployer::writePluginsPrivate() const
{
  PluginDeployer::writePlugins();

  writePluginsToOpenMwConfig("content=",
                             std::regex(R"(^content=.*)"),
                             [this](int i) {
                               return plugins_[i].second &&
                                      !groundcover_plugins_.contains(plugins_[i].first);
                             });
  writePluginsToOpenMwConfig("groundcover=",
                             std::regex(R"(^groundcover=.*)"),
                             [this](int i) {
                               return plugins_[i].second &&
                                      groundcover_plugins_.contains(plugins_[i].first);
                             });
  // fork #87: OpenMW data= entry (VFS) deploy mode
  // Keep Limo's managed data= block in sync with the current load order whenever the
  // opt-in VFS mode is active. The default path above is left completely unchanged.
  if(use_data_entries_)
    writeDataEntries(true);
}

// fork #87: OpenMW data= entry (VFS) deploy mode
void OpenMwPluginDeployer::setDeployMode(DeployMode deploy_mode)
{
  // Autonomous deployers always copy; we never change deploy_mode_. Instead, reuse the
  // existing sym_link selector as the opt-in trigger for the native VFS data= entry mode
  // so no always-compiled header needs a new enum value.
  setUseDataEntryMode(deploy_mode == sym_link);
}

// fork #87: OpenMW data= entry (VFS) deploy mode
void OpenMwPluginDeployer::setUseDataEntryMode(bool enabled)
{
  if(use_data_entries_ == enabled)
    return;
  const bool was_enabled = use_data_entries_;
  use_data_entries_ = enabled;
  if(enabled)
    writeDataEntries(true);
  else if(was_enabled)
    writeDataEntries(false);
}

// fork #87: OpenMW data= entry (VFS) deploy mode
bool OpenMwPluginDeployer::usesDataEntryMode() const
{
  return use_data_entries_;
}

// fork #87: OpenMW data= entry (VFS) deploy mode
std::vector<std::string> OpenMwPluginDeployer::collectDataEntryPaths() const
{
  // One data= entry per enabled mod's staging directory, in load order, deduplicated.
  // Each plugin file lives in source_path_ (the upstream deployer's target), so that is
  // the directory OpenMW must be pointed at. Using the per-plugin source directory keeps
  // this correct should a future layout place plugins in distinct directories.
  std::vector<std::string> paths;
  std::unordered_set<std::string> seen;
  for(const auto& [plugin, enabled] : plugins_)
  {
    if(!enabled)
      continue;
    const sfs::path plugin_path = source_path_ / plugin;
    std::error_code ec;
    sfs::path dir = plugin_path.parent_path();
    if(dir.empty())
      dir = source_path_;
    // Prefer the canonical path when it resolves, but never fail deployment over it.
    const sfs::path canonical = sfs::weakly_canonical(dir, ec);
    const std::string entry = (ec ? dir : canonical).string();
    if(seen.insert(entry).second)
      paths.push_back(entry);
  }
  return paths;
}

// fork #87: OpenMW data= entry (VFS) deploy mode
void OpenMwPluginDeployer::writeDataEntries(bool write_entries) const
{
  const sfs::path config_path = dest_path_ / OPEN_MW_CONFIG_FILE_NAME;

  std::ifstream in_file(config_path);
  if(!in_file.is_open())
    throw std::runtime_error(
      std::format("Error: Could not open '{}'.", config_path.string()));

  // Copy every line verbatim except the previously written Limo block, which is dropped.
  std::vector<std::string> lines;
  std::string line;
  bool in_block = false;
  while(std::getline(in_file, line))
  {
    std::string trimmed = line;
    if(!trimmed.empty() && trimmed.back() == '\r')
      trimmed.pop_back();
    if(trimmed == DATA_BLOCK_BEGIN_MARKER)
    {
      in_block = true;
      continue;
    }
    if(in_block)
    {
      if(trimmed == DATA_BLOCK_END_MARKER)
        in_block = false;
      continue;
    }
    lines.push_back(line);
  }
  in_file.close();

  // Write to a temporary file first, then atomically replace openmw.cfg so an error can
  // never leave the user with a corrupted config.
  const sfs::path temp_path = config_path.string() + ".lmm_tmp";
  {
    std::ofstream out_file(temp_path, std::ios::binary | std::ios::trunc);
    if(!out_file.is_open())
      throw std::runtime_error(
        std::format("Error: Could not open '{}'.", temp_path.string()));

    for(const auto& out_line : lines)
      out_file << out_line << "\n";

    if(write_entries)
    {
      const auto data_paths = collectDataEntryPaths();
      if(!data_paths.empty())
      {
        out_file << DATA_BLOCK_BEGIN_MARKER << "\n";
        for(const auto& path : data_paths)
          out_file << "data=\"" << path << "\"\n";
        out_file << DATA_BLOCK_END_MARKER << "\n";
      }
    }

    out_file.flush();
    if(!out_file.good())
    {
      out_file.close();
      std::error_code rm_ec;
      sfs::remove(temp_path, rm_ec);
      throw std::runtime_error(
        std::format("Error: Failed while writing '{}'.", temp_path.string()));
    }
  }

  std::error_code ec;
  sfs::rename(temp_path, config_path, ec);
  if(ec)
  {
    // Fallback for e.g. cross-device temp locations: copy then remove.
    sfs::copy_file(temp_path, config_path, sfs::copy_options::overwrite_existing, ec);
    std::error_code rm_ec;
    sfs::remove(temp_path, rm_ec);
    if(ec)
      throw std::runtime_error(
        std::format("Error: Could not replace '{}'.", config_path.string()));
  }
}

std::optional<sfs::path> OpenMwPluginDeployer::findPloxRulesFile() const
{
  for(const auto& name : PLOX_RULES_FILE_NAMES)
  {
    const sfs::path candidate = dest_path_ / std::string(name);
    if(sfs::exists(candidate) && sfs::is_regular_file(candidate))
      return candidate;
  }
  return {};
}

std::vector<std::pair<std::string, std::string>> OpenMwPluginDeployer::parsePloxOrderRules(
  const sfs::path& rules_path) const
{
  std::vector<std::pair<std::string, std::string>> constraints;

  std::ifstream in_file(rules_path);
  if(!in_file.is_open())
  {
    log_(Log::LOG_WARNING,
         std::format("Deployer '{}': Could not open PLOX rules file '{}'.",
                     name_,
                     rules_path.string()));
    return constraints;
  }

  // Build a case-insensitive lookup of currently present plugin names so that rules
  // referencing absent plugins can be dropped, while still mapping to the exact
  // casing used in plugins_.
  std::unordered_map<std::string, std::string> present_plugins;
  const auto to_lower = [](std::string s)
  {
    for(char& c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  for(const auto& [plugin, _] : plugins_)
    present_plugins.emplace(to_lower(plugin), plugin);

  const auto trim = [](const std::string& s)
  {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if(begin == std::string::npos)
      return std::string();
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
  };

  // Active block: 1 == [Order], 2 == [Near], 0 == ignored/other.
  int active_block = 0;
  // Last plugin seen within the current ordering block (resolved to plugins_ casing).
  std::string previous_plugin;

  std::string raw_line;
  while(std::getline(in_file, raw_line))
  {
    std::string line = trim(raw_line);
    if(line.empty() || line[0] == ';')
      continue;

    // Section headers, e.g. [Order], [Near], [Conflict], [Requires], ...
    if(line.front() == '[')
    {
      const auto close = line.find(']');
      std::string header =
        close == std::string::npos ? line.substr(1) : line.substr(1, close - 1);
      const std::string header_lc = to_lower(trim(header));
      if(header_lc == "order")
        active_block = 1;
      else if(header_lc == "near")
        active_block = 2;
      else
        active_block = 0;
      previous_plugin.clear();
      continue;
    }

    if(active_block == 0)
      continue;

    // Skip rule messages / annotations which start with an mlox operator character
    // rather than a plugin name.
    const char first = line.front();
    if(first == '>' || first == '<' || first == '!' || first == '|' || first == '&' ||
       first == '@' || first == '%' || first == '=')
      continue;

    // Within an [Order]/[Near] block each non-empty, non-comment line is a plugin name.
    // Strip any trailing inline comment.
    const auto comment_pos = line.find(" ;");
    if(comment_pos != std::string::npos)
      line = trim(line.substr(0, comment_pos));
    if(line.empty())
      continue;

    auto iter = present_plugins.find(to_lower(line));
    if(iter == present_plugins.end())
    {
      // Plugin not installed; it still breaks the chain so we don't create a
      // spurious constraint across a gap.
      previous_plugin.clear();
      continue;
    }
    const std::string& current_plugin = iter->second;

    if(!previous_plugin.empty() && previous_plugin != current_plugin)
      constraints.emplace_back(previous_plugin, current_plugin);
    previous_plugin = current_plugin;
  }

  return constraints;
}

bool OpenMwPluginDeployer::applyPloxRules()
{
  const auto rules_path = findPloxRulesFile();
  if(!rules_path)
  {
    log_(Log::LOG_DEBUG,
         std::format("Deployer '{}': No PLOX/mlox rules file found; skipping rules-based "
                     "sort and keeping existing order.",
                     name_));
    return false;
  }

  log_(Log::LOG_INFO,
       std::format("Deployer '{}': Applying PLOX/mlox rules from '{}'.",
                   name_,
                   rules_path->string()));

  const auto constraints = parsePloxOrderRules(*rules_path);
  if(constraints.empty())
  {
    log_(Log::LOG_INFO,
         std::format("Deployer '{}': PLOX rules file contained no applicable ordering "
                     "constraints for the installed plugins.",
                     name_));
    return false;
  }

  // Map plugin name -> current index, to preserve the existing order as a stable
  // tie-breaker during the topological sort.
  std::unordered_map<std::string, int> index_of;
  for(const auto& [i, pair] : str::enumerate_view(plugins_))
    index_of.emplace(pair.first, static_cast<int>(i));

  // Build adjacency (a -> b means a before b) and in-degrees.
  std::map<int, std::vector<int>> adjacency;
  std::vector<int> in_degree(plugins_.size(), 0);
  std::set<std::pair<int, int>> seen_edges;
  for(const auto& [a, b] : constraints)
  {
    auto ia = index_of.find(a);
    auto ib = index_of.find(b);
    if(ia == index_of.end() || ib == index_of.end())
      continue;
    const std::pair<int, int> edge{ ia->second, ib->second };
    if(!seen_edges.insert(edge).second)
      continue;
    adjacency[edge.first].push_back(edge.second);
    in_degree[edge.second]++;
  }

  // Kahn's algorithm with a tie-break on original index for stability.
  const auto cmp = [](int lhs, int rhs) { return lhs > rhs; };
  std::priority_queue<int, std::vector<int>, decltype(cmp)> ready(cmp);
  for(int i = 0; i < static_cast<int>(plugins_.size()); i++)
  {
    if(in_degree[i] == 0)
      ready.push(i);
  }

  std::vector<std::pair<std::string, bool>> sorted;
  sorted.reserve(plugins_.size());
  while(!ready.empty())
  {
    const int node = ready.top();
    ready.pop();
    sorted.push_back(plugins_[node]);
    auto adj_iter = adjacency.find(node);
    if(adj_iter == adjacency.end())
      continue;
    for(int next : adj_iter->second)
    {
      if(--in_degree[next] == 0)
        ready.push(next);
    }
  }

  if(sorted.size() != plugins_.size())
  {
    // Cycle detected among the constraints; conservatively keep the existing order.
    log_(Log::LOG_WARNING,
         std::format("Deployer '{}': PLOX rules contain a cycle; keeping existing order.",
                     name_));
    return false;
  }

  plugins_ = std::move(sorted);
  log_(Log::LOG_INFO,
       std::format("Deployer '{}': Applied {} PLOX ordering constraint(s).",
                   name_,
                   constraints.size()));
  return true;
}
