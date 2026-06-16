#include "lootdeployer.h"
#include "pathutils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cpr/cpr.h>
#include <fstream>
#include <iostream>
#include <ranges>
#include <regex>
#include <set>

namespace sfs = std::filesystem;
namespace str = std::ranges;
namespace pu = path_utils;


LootDeployer::LootDeployer(const sfs::path& source_path,
                           const sfs::path& dest_path,
                           const std::string& name,
                           bool init_tags,
                           bool perform_init) : PluginDeployer(source_path, dest_path, name)
{
  LIST_URLS = DEFAULT_LIST_URLS;
  // make sure no hard link related checks are performed
  deploy_mode_ = copy;
  enable_unsafe_sorting_ = true;
  if(!perform_init)
    return;
  type_ = "Loot Deployer";
  is_autonomous_ = true;
  plugin_regex_ = R"(.*\.[eE][sS][pPlLmM]$)";
  plugin_file_line_regex_ = R"(^\s*(\*?)([^#]*\.[eE][sS][pPlLmM])(\r?))";
  config_file_name_ = ".lmmconfig";
  tags_file_name_ = ".loot_tags";
  source_mods_file_name_ = ".lmm_mod_sources";
  updateAppType();
  setupPluginFiles();
  loadPlugins();
  updatePlugins();
  if(sfs::exists(dest_path_ / config_file_name_))
    loadSettingsPrivate();
  if(init_tags)
    readPluginTags();
  readSourceMods();
}

void LootDeployer::unDeploy(std::optional<ProgressNode*> progress_node)
{
  const std::string loadorder_backup_path =
    dest_path_ / ("." + app_plugin_file_name_ + UNDEPLOY_BACKUP_EXTENSION);
  const std::string plugin_backup_path =
    dest_path_ / ("." + plugin_file_name_ + UNDEPLOY_BACKUP_EXTENSION);
  if(pu::exists(loadorder_backup_path) && !pu::exists(plugin_backup_path))
    sfs::remove(loadorder_backup_path);
  else if(!pu::exists(loadorder_backup_path) && pu::exists(plugin_backup_path))
    sfs::remove(plugin_backup_path);
  else if(!pu::exists(loadorder_backup_path) && !pu::exists(plugin_backup_path))
  {
    sfs::copy(dest_path_ / app_plugin_file_name_, loadorder_backup_path);
    sfs::copy(dest_path_ / plugin_file_name_, plugin_backup_path);
  }

  log_(Log::LOG_INFO, std::format("Deployer '{}': Updating plugins...", name_));
  updatePlugins();
  updatePluginTags();
}

void LootDeployer::addProfile(int source)
{
  if(num_profiles_ == 0)
  {
    num_profiles_++;
    saveSettings();
    return;
  }
  // Overwrite any stale backup files left from a previously removed profile so creating a profile
  // doesn't fail with a "file already exists" error (limo-app/limo#131).
  const auto copy_opts = sfs::copy_options::overwrite_existing;
  if(source >= 0 && source <= num_profiles_ && num_profiles_ > 1)
  {
    sfs::copy(dest_path_ / ("." + plugin_file_name_ + EXTENSION + std::to_string(source)),
              dest_path_ / ("." + plugin_file_name_ + EXTENSION + std::to_string(num_profiles_)),
              copy_opts);
    sfs::copy(dest_path_ / ("." + app_plugin_file_name_ + EXTENSION + std::to_string(source)),
              dest_path_ /
                ("." + app_plugin_file_name_ + EXTENSION + std::to_string(num_profiles_)),
              copy_opts);
  }
  else
  {
    sfs::copy(dest_path_ / plugin_file_name_,
              dest_path_ / ("." + plugin_file_name_ + EXTENSION + std::to_string(num_profiles_)),
              copy_opts);
    sfs::copy(dest_path_ / app_plugin_file_name_,
              dest_path_ /
                ("." + app_plugin_file_name_ + EXTENSION + std::to_string(num_profiles_)),
              copy_opts);
  }
  num_profiles_++;
  saveSettings();
}

void LootDeployer::removeProfile(int profile)
{
  if(profile >= num_profiles_ || profile < 0)
    return;
  std::string plugin_file = "." + plugin_file_name_ + EXTENSION + std::to_string(profile);
  std::string loadorder_file = "." + app_plugin_file_name_ + EXTENSION + std::to_string(profile);
  if(profile == current_profile_)
    setProfile(0);
  else if(profile < current_profile_)
    setProfile(current_profile_ - 1);
  sfs::remove(dest_path_ / plugin_file);
  sfs::remove(dest_path_ / loadorder_file);
  num_profiles_--;
  saveSettings();
}

void LootDeployer::setProfile(int profile)
{
  if(profile >= num_profiles_ || profile < 0 || profile == current_profile_)
    return;
  if(!sfs::exists(dest_path_ / plugin_file_name_) ||
     !sfs::exists(dest_path_ / app_plugin_file_name_) ||
     !sfs::exists(dest_path_ / ("." + plugin_file_name_ + EXTENSION + std::to_string(profile))) ||
     !sfs::exists(dest_path_ / ("." + app_plugin_file_name_ + EXTENSION + std::to_string(profile))))
  {
    resetSettings();
    return;
  }
  sfs::rename(dest_path_ / plugin_file_name_,
              dest_path_ /
                ("." + plugin_file_name_ + EXTENSION + std::to_string(current_profile_)));
  sfs::rename(dest_path_ / app_plugin_file_name_,
              dest_path_ /
                ("." + app_plugin_file_name_ + EXTENSION + std::to_string(current_profile_)));
  sfs::rename(dest_path_ / ("." + plugin_file_name_ + EXTENSION + std::to_string(profile)),
              dest_path_ / plugin_file_name_);
  sfs::rename(dest_path_ / ("." + app_plugin_file_name_ + EXTENSION + std::to_string(profile)),
              dest_path_ / app_plugin_file_name_);
  current_profile_ = profile;
  saveSettings();
  loadPlugins();
  updatePlugins();
}

std::unordered_set<int> LootDeployer::getModConflicts(int mod_id,
                                                      std::optional<ProgressNode*> progress_node)
{
  std::unordered_set<int> conflicts{ mod_id };
  if(mod_id < 0 || mod_id >= static_cast<int>(plugins_.size()))
    return conflicts;
  auto loot_handle = loot::CreateGameHandle(app_type_, source_path_, dest_path_);
  std::vector<sfs::path> plugin_paths;
  plugin_paths.reserve(plugins_.size());
  for(const auto& [path, s] : plugins_)
    plugin_paths.emplace_back(source_path_ / path);
  loot_handle->LoadPlugins(plugin_paths, false);
  // Guard against null (plugin file missing on disk) (limo-app/limo#185, limo-app/limo#31).
  auto plugin = loot_handle->GetPlugin(plugins_[mod_id].first);
  if(!plugin)
    return conflicts;
  for(int i = 0; i < plugins_.size(); i++)
  {
    if(i == mod_id)
      continue;
    auto other_plugin = loot_handle->GetPlugin(plugins_[i].first);
    if(other_plugin && other_plugin->DoRecordsOverlap(*plugin))
      conflicts.insert(i);
  }
  return conflicts;
}

void LootDeployer::sortModsByConflicts(std::optional<ProgressNode*> progress_node)
{
  if(progress_node)
  {
    (*progress_node)->addChildren({ 1, 2, 5, 0.2f });
    (*progress_node)->child(0).setTotalSteps(1);
    (*progress_node)->child(1).setTotalSteps(1);
    (*progress_node)->child(2).setTotalSteps(1);
    (*progress_node)->child(3).setTotalSteps(1);
  }
  updateMasterList();
  if(progress_node)
    (*progress_node)->child(0).advance();

  sfs::path master_list_path = dest_path_ / "masterlist.yaml";
  if(!sfs::exists(master_list_path))
    throw std::runtime_error("Could not find masterlist.yaml at '" + master_list_path.string() +
                             "'\n.Try to update the URL in the " +
                             "settings. Alternatively, you can manually download the " +
                             "file and place it in '" + dest_path_.string() + "'.\nYou can " +
                             "disable auto updates in '" +
                             (dest_path_ / config_file_name_).string() + "'.");
  auto loot_handle = loot::CreateGameHandle(app_type_, source_path_, dest_path_);
  sfs::path user_list_path(dest_path_ / "userlist.yaml");
  sfs::path prelude_path(dest_path_ / "prelude.yaml");
  if(sfs::exists(prelude_path))
    loot_handle->GetDatabase().LoadMasterlistWithPrelude(master_list_path, prelude_path);
  else if(sfs::exists(master_list_path))
    loot_handle->GetDatabase().LoadMasterlist(master_list_path);
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
  std::set<std::string> conflicting;
  int num_light_plugins = 0;
  int num_master_plugins = 0;
  int num_standard_plugins = 0;
  tags_.clear();
  for(const auto& plugin : sorted_plugins)
  {
    auto iter = str::find_if(plugins_, [plugin](const auto& p) { return p.first == plugin; });
    bool enabled = true;
    if(iter != plugins_.end())
      enabled = iter->second;
    const auto cur_plugin = loot_handle->GetPlugin(plugin);
    // GetPlugin returns null when the plugin file was not actually loaded
    // (e.g. the file does not exist on disk). Treat it as a Standard plugin to
    // avoid a null-dereference crash (limo-app/limo#185, limo-app/limo#31).
    if(!cur_plugin)
    {
      num_standard_plugins++;
      tags_.push_back({ STANDARD_PLUGIN });
      new_plugins.emplace_back(plugin, enabled);
      continue;
    }
    if(cur_plugin->IsLightPlugin())
    {
      num_light_plugins++;
      tags_.push_back({ LIGHT_PLUGIN });
    }
    else if(cur_plugin->IsMaster())
    {
      num_master_plugins++;
      tags_.push_back({ MASTER_PLUGIN });
    }
    else
    {
      num_standard_plugins++;
      tags_.push_back({ STANDARD_PLUGIN });
    }
    new_plugins.emplace_back(plugin, enabled);
    auto masters = cur_plugin->GetMasters();
    for(const auto& master : masters)
    {
      if(!pu::pathExists(master, source_path_) && enabled)
        log_(Log::LOG_WARNING,
             "LOOT: Plugin '" + master + "' is missing but required" + " for '" + plugin + "'");
    }
    auto meta_data = loot_handle->GetDatabase().GetPluginMetadata(plugin);
    if(!meta_data)
      continue;
    auto requirements = meta_data->GetRequirements();
    for(const auto& req : requirements)
    {
      std::string file = static_cast<std::string>(req.GetName());
      if(!pu::pathExists(file, source_path_))
        log_(Log::LOG_WARNING, "LOOT: Requirement '" + file + "' not met for '" + plugin + "'");
    }
  }
  log_(Log::LOG_DEBUG, std::format("LOOT: App type {}", static_cast<int>(app_type_)));
  log_(Log::LOG_INFO,
       std::format("LOOT: Total Plugins: {}, Master: {}, Standard: {}, Light: {}",
                   new_plugins.size(),
                   num_master_plugins,
                   num_standard_plugins,
                   num_light_plugins));
  // Surface LOOT's per-plugin messages so users see issues even before the
  // dedicated UI surface lands (see UI hookup note in lootdeployer.h).
  const auto plugin_messages = getPluginMessages();
  if(!plugin_messages.empty())
  {
    int num_warnings = 0;
    int num_errors = 0;
    for(const auto& message : plugin_messages)
    {
      if(message.severity == MessageSeverity::warn)
        num_warnings++;
      else if(message.severity == MessageSeverity::error)
        num_errors++;
    }
    log_(Log::LOG_WARNING,
         std::format("LOOT: {} plugin message(s) ({} warning(s), {} error(s)). "
                     "Use getPluginMessages() to inspect them.",
                     plugin_messages.size(),
                     num_warnings,
                     num_errors));
    for(const auto& message : plugin_messages)
    {
      if(message.severity == MessageSeverity::say)
        continue;
      const Log::LogLevel level =
        message.severity == MessageSeverity::error ? Log::LOG_ERROR : Log::LOG_WARNING;
      log_(level, std::format("LOOT: '{}': {}", message.plugin_name, message.text));
    }
  }

  if(enable_unsafe_sorting_)
    plugins_ = new_plugins;
  writePluginTags();
  writePlugins();
  if(progress_node)
    (*progress_node)->child(3).advance();
}

void LootDeployer::cleanup()
{
  for(int i = 0; i < num_profiles_; i++)
  {
    sfs::path plugin_path = dest_path_ / ("." + plugin_file_name_ + EXTENSION + std::to_string(i));
    sfs::path load_order_path =
      dest_path_ / ("." + app_plugin_file_name_ + EXTENSION + std::to_string(i));
    sfs::remove(plugin_path);
    sfs::remove(load_order_path);
  }
  current_profile_ = 0;
  num_profiles_ = 1;
  sfs::remove(dest_path_ / config_file_name_);
}

std::map<std::string, int> LootDeployer::getAutoTagMap()
{
  return { { LIGHT_PLUGIN, num_light_plugins_ },
           { MASTER_PLUGIN, num_master_plugins_ },
           { STANDARD_PLUGIN, num_standard_plugins_ } };
}

// fork #31: read currently loaded user metadata for every managed plugin.
std::vector<LootDeployer::PluginUserMetadata> LootDeployer::getPluginUserMetadata()
{
  auto loot_handle = loot::CreateGameHandle(app_type_, source_path_, dest_path_);
  sfs::path user_list_path(dest_path_ / "userlist.yaml");
  if(sfs::exists(user_list_path))
    loot_handle->GetDatabase().LoadUserlist(user_list_path);

  std::vector<PluginUserMetadata> result;
  result.reserve(plugins_.size());
  for(const auto& [plugin, enabled] : plugins_)
  {
    PluginUserMetadata entry;
    entry.plugin = plugin;
    // GetPluginUserMetadata only returns user-added metadata, not masterlist data.
    auto meta_data = loot_handle->GetDatabase().GetPluginUserMetadata(plugin);
    if(meta_data)
    {
      if(auto group = meta_data->GetGroup())
        entry.group = *group;
      for(const auto& file : meta_data->GetLoadAfterFiles())
        entry.load_after.push_back(static_cast<std::string>(file.GetName()));
    }
    result.push_back(std::move(entry));
  }
  return result;
}

// fork #31: write per-plugin user metadata back to userlist.yaml, preserving any
// user metadata Limo does not model by round-tripping through libloot.
void LootDeployer::writePluginUserMetadata(const std::vector<PluginUserMetadata>& metadata)
{
  auto loot_handle = loot::CreateGameHandle(app_type_, source_path_, dest_path_);
  sfs::path user_list_path(dest_path_ / "userlist.yaml");
  // Load the existing userlist so unmodelled fields survive the round-trip.
  if(sfs::exists(user_list_path))
    loot_handle->GetDatabase().LoadUserlist(user_list_path);

  for(const auto& entry : metadata)
  {
    // Start from the plugin's existing user metadata so that fields Limo does not
    // model (messages, tags, dirty/clean info, locations, requirements, ...) are kept.
    auto existing = loot_handle->GetDatabase().GetPluginUserMetadata(entry.plugin);
    loot::PluginMetadata plugin_meta = existing ? *existing : loot::PluginMetadata(entry.plugin);

    if(entry.group.empty())
      plugin_meta.UnsetGroup();
    else
      plugin_meta.SetGroup(entry.group);

    std::vector<loot::File> load_after;
    load_after.reserve(entry.load_after.size());
    for(const auto& file : entry.load_after)
    {
      if(!file.empty())
        load_after.emplace_back(file);
    }
    plugin_meta.SetLoadAfterFiles(load_after);

    // For non-regex plugin names this replaces the existing user metadata entry.
    loot_handle->GetDatabase().SetPluginUserMetadata(plugin_meta);
  }

  loot::MetadataWriteOptions options;
  options.SetTruncate(true);
  loot_handle->GetDatabase().WriteUserMetadata(user_list_path, options);
  log_(Log::LOG_INFO,
       std::format("LOOT: Wrote user metadata for {} plugins to '{}'",
                   metadata.size(),
                   user_list_path.string()));
}

namespace
{
/*!
 * \brief Idempotently sets a key=value pair inside the given section of an INI file.
 *
 * Existing sections/keys are reused (value updated in place) rather than duplicated and
 * all other content (other sections, keys, comments, blank lines) is preserved. Missing
 * sections or keys are created. The section/key match is case insensitive, mirroring how
 * the Bethesda engines parse these files.
 *
 * \param ini_path Path to the INI file. Created if it does not exist.
 * \param section Section name without the surrounding brackets, e.g. "Archive".
 * \param key Key name, e.g. "bInvalidateOlderFiles".
 * \param value Value to store, e.g. "1".
 */
void setIniValue(const sfs::path& ini_path,
                 const std::string& section,
                 const std::string& key,
                 const std::string& value)
{
  const auto to_lower = [](std::string s)
  {
    std::transform(s.begin(),
                   s.end(),
                   s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
  };
  const auto trim = [](const std::string& s)
  {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if(begin == std::string::npos)
      return std::string();
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
  };

  // Read the existing file line by line, if it exists.
  std::vector<std::string> lines;
  if(sfs::exists(ini_path))
  {
    std::ifstream in(ini_path, std::ios::binary);
    std::string line;
    while(std::getline(in, line))
    {
      if(!line.empty() && line.back() == '\r')
        line.pop_back();
      lines.push_back(line);
    }
  }

  const std::string section_lc = to_lower(section);
  const std::string key_lc = to_lower(key);
  const std::string new_line = key + "=" + value;

  int section_start = -1; // index of the "[section]" header line
  int section_end = -1;   // index one past the last line belonging to the section
  bool in_target_section = false;
  for(int i = 0; i < static_cast<int>(lines.size()); i++)
  {
    const std::string trimmed = trim(lines[i]);
    if(trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']')
    {
      const std::string name = to_lower(trim(trimmed.substr(1, trimmed.size() - 2)));
      if(in_target_section)
      {
        section_end = i;
        break;
      }
      if(name == section_lc)
      {
        in_target_section = true;
        section_start = i;
      }
    }
  }
  if(in_target_section && section_end == -1)
    section_end = static_cast<int>(lines.size());

  if(section_start == -1)
  {
    // Section does not exist: append it (with a separating blank line if needed).
    if(!lines.empty() && !trim(lines.back()).empty())
      lines.emplace_back();
    lines.push_back("[" + section + "]");
    lines.push_back(new_line);
  }
  else
  {
    // Section exists: look for the key within it and update in place.
    bool key_found = false;
    for(int i = section_start + 1; i < section_end; i++)
    {
      const std::string trimmed = trim(lines[i]);
      if(trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#')
        continue;
      const auto eq = trimmed.find('=');
      if(eq == std::string::npos)
        continue;
      if(to_lower(trim(trimmed.substr(0, eq))) == key_lc)
      {
        lines[i] = new_line;
        key_found = true;
        break;
      }
    }
    if(!key_found)
    {
      // Insert the key at the end of the section, after the last non-blank line.
      int insert_at = section_end;
      while(insert_at - 1 > section_start && trim(lines[insert_at - 1]).empty())
        insert_at--;
      lines.insert(lines.begin() + insert_at, new_line);
    }
  }

  sfs::create_directories(ini_path.parent_path());
  std::ofstream out(ini_path, std::ios::binary | std::ios::trunc);
  if(!out.is_open())
    throw std::runtime_error("Could not write to INI file '" + ini_path.string() + "'.");
  for(const auto& line : lines)
    out << line << "\n";
}
}

void LootDeployer::applyArchiveInvalidation(bool enable) const
{
  // Maps the applicable game types to the preferences INI file names (relative to
  // dest_path_, i.e. the game's "My Games" directory) that hold the [Archive] section.
  static const std::map<loot::GameType, std::vector<std::string>> INI_FILES = {
    { loot::GameType::tes4, { "Oblivion.ini" } },
    { loot::GameType::fo3, { "Fallout.ini", "FalloutPrefs.ini" } },
    { loot::GameType::fonv, { "Fallout.ini", "FalloutPrefs.ini" } }
  };

  const auto iter = INI_FILES.find(app_type_);
  if(iter == INI_FILES.end())
  {
    // Archive Invalidation does not apply to this game type: no-op.
    log_(Log::LOG_DEBUG,
         std::format("Deployer '{}': Archive Invalidation not applicable for this game.", name_));
    return;
  }

  const std::string value = enable ? "1" : "0";
  for(const auto& ini_name : iter->second)
  {
    const sfs::path ini_path = dest_path_ / ini_name;
    setIniValue(ini_path, "Archive", "bInvalidateOlderFiles", value);
    if(app_type_ == loot::GameType::tes4)
      setIniValue(ini_path, "Archive", "bLoadFaceGenHeadEGTFiles", value);
  }
  log_(Log::LOG_INFO,
       std::format("Deployer '{}': {} Archive Invalidation.",
                   name_,
                   enable ? "Enabled" : "Disabled"));
}

std::vector<LootDeployer::PluginMessage> LootDeployer::getPluginMessages() const
{
  std::vector<PluginMessage> messages;
  try
  {
    auto loot_handle = loot::CreateGameHandle(app_type_, source_path_, dest_path_);
    auto& database = loot_handle->GetDatabase();

    const sfs::path master_list_path = dest_path_ / "masterlist.yaml";
    sfs::path user_list_path(dest_path_ / "userlist.yaml");
    if(!sfs::exists(user_list_path))
      user_list_path = "";
    sfs::path prelude_path(dest_path_ / "prelude.yaml");
    if(!sfs::exists(prelude_path))
      prelude_path = "";
    loadLists(database, sfs::exists(master_list_path) ? master_list_path : sfs::path(),
              user_list_path, prelude_path);

    // Load the plugins so condition evaluation has the data it needs.
    std::vector<sfs::path> plugin_paths;
    plugin_paths.reserve(plugins_.size());
    for(const auto& [path, s] : plugins_)
      plugin_paths.emplace_back(source_path_ / path);
    loot_handle->LoadPlugins(plugin_paths, false);

    const auto select_text = [](const std::vector<loot::MessageContent>& content) -> std::string
    {
      const auto selected =
        loot::SelectMessageContent(content, std::string(loot::MessageContent::DEFAULT_LANGUAGE));
      if(selected)
        return selected->GetText();
      if(!content.empty())
        return content.front().GetText();
      return {};
    };
    const auto to_severity = [](loot::MessageType type)
    {
      switch(type)
      {
        case loot::MessageType::warn:
          return MessageSeverity::warn;
        case loot::MessageType::error:
          return MessageSeverity::error;
        default:
          return MessageSeverity::say;
      }
    };

    for(const auto& [plugin, enabled] : plugins_)
    {
      // Evaluate conditions so only messages relevant to the current setup are returned.
      const auto meta_data = database.GetPluginMetadata(plugin, true, true);
      if(!meta_data)
        continue;

      for(const auto& message : meta_data->GetMessages())
      {
        std::string text = select_text(message.GetContent());
        if(text.empty())
          continue;
        messages.push_back({ plugin, to_severity(message.GetType()), std::move(text) });
      }

      for(const auto& req : meta_data->GetRequirements())
      {
        const std::string file = static_cast<std::string>(req.GetName());
        if(!pu::pathExists(file, source_path_))
          messages.push_back(
            { plugin, MessageSeverity::error, "Missing requirement: '" + file + "'" });
      }

      for(const auto& inc : meta_data->GetIncompatibilities())
      {
        const std::string file = static_cast<std::string>(inc.GetName());
        if(pu::pathExists(file, source_path_))
          messages.push_back(
            { plugin, MessageSeverity::warn, "Incompatible with installed '" + file + "'" });
      }

      for(const auto& dirty : meta_data->GetDirtyInfo())
      {
        std::string text = "Dirty plugin: " + std::to_string(dirty.GetITMCount()) +
                           " ITM, " + std::to_string(dirty.GetDeletedReferenceCount()) +
                           " deleted references, " + std::to_string(dirty.GetDeletedNavmeshCount()) +
                           " deleted navmeshes (clean with " + dirty.GetCleaningUtility() + ")";
        const std::string detail = select_text(dirty.GetDetail());
        if(!detail.empty())
          text += ": " + detail;
        messages.push_back({ plugin, MessageSeverity::warn, std::move(text) });
      }
    }
  }
  catch(const std::exception& e)
  {
    log_(Log::LOG_WARNING,
         std::format("LOOT: Could not collect plugin messages: {}", e.what()));
    return {};
  }
  return messages;
}

// fork #212: surface LOOT-masterlist dirty/clean plugin info per managed plugin.
std::vector<LootDeployer::PluginCleanInfo> LootDeployer::getPluginCleanInfo() const
{
  std::vector<PluginCleanInfo> clean_info;
  try
  {
    auto loot_handle = loot::CreateGameHandle(app_type_, source_path_, dest_path_);
    auto& database = loot_handle->GetDatabase();

    const sfs::path master_list_path = dest_path_ / "masterlist.yaml";
    sfs::path user_list_path(dest_path_ / "userlist.yaml");
    if(!sfs::exists(user_list_path))
      user_list_path = "";
    sfs::path prelude_path(dest_path_ / "prelude.yaml");
    if(!sfs::exists(prelude_path))
      prelude_path = "";
    loadLists(database, sfs::exists(master_list_path) ? master_list_path : sfs::path(),
              user_list_path, prelude_path);

    // Load the plugins so condition evaluation has the data it needs.
    std::vector<sfs::path> plugin_paths;
    plugin_paths.reserve(plugins_.size());
    for(const auto& [path, s] : plugins_)
      plugin_paths.emplace_back(source_path_ / path);
    loot_handle->LoadPlugins(plugin_paths, false);

    clean_info.reserve(plugins_.size());
    for(const auto& [plugin, enabled] : plugins_)
    {
      PluginCleanInfo info;
      info.plugin = plugin;

      // Evaluate conditions so only dirty info relevant to the current setup is returned.
      const auto meta_data = database.GetPluginMetadata(plugin, true, true);
      if(meta_data)
      {
        // A plugin may have several dirty-info entries (one per known CRC).
        // Aggregate them so the consumer sees the worst case; the first entry's
        // cleaning utility is used as the recommended one.
        for(const auto& dirty : meta_data->GetDirtyInfo())
        {
          info.is_dirty = true;
          info.itm_count += dirty.GetITMCount();
          info.deleted_reference_count += dirty.GetDeletedReferenceCount();
          info.deleted_navmesh_count += dirty.GetDeletedNavmeshCount();
          if(info.cleaning_utility.empty())
            info.cleaning_utility = dirty.GetCleaningUtility();
        }
      }

      clean_info.push_back(std::move(info));
    }
  }
  catch(const std::exception& e)
  {
    log_(Log::LOG_WARNING,
         std::format("LOOT: Could not collect plugin clean info: {}", e.what()));
    return {};
  }
  return clean_info;
}

void LootDeployer::writePlugins() const
{
  PluginDeployer::writePlugins();

  std::ofstream plugins_file;
  plugins_file.open(dest_path_ / app_plugin_file_name_);
  if(!plugins_file.is_open())
    throw std::runtime_error("Could not open " + app_plugin_file_name_ + "!");
  for(const auto& [name, enabled] : plugins_)
  {
    // Skip base-game/DLC masters and Creation Club plugins for games that load these implicitly
    // (e.g. Skyrim SE, Fallout 4). Listing them in Plugins.txt can break the load order, while the
    // game manages them itself regardless. See https://github.com/limo-app/limo/issues/64.
    if(enabled && !isImplicitlyManagedPlugin(name))
      plugins_file << name << "\n";
  }
  plugins_file.close();

  if(APP_TYPE_WITH_FILE_MOD_ORDER.contains(app_type_))
  {
    for(const auto& [i, pair] : str::enumerate_view(plugins_))
    {
      const auto& [name, enabled] = pair;
      std::tm tm = { 0, static_cast<int>(i), 0, 1, 0, 100 };
      tm.tm_isdst = -1;
      std::filesystem::file_time_type time_point =
        std::chrono::file_clock::from_sys(std::chrono::system_clock::from_time_t(std::mktime(&tm)));
      const sfs::path plugin_path = source_path_ / name;
      if(sfs::exists(plugin_path))
      {
        sfs::last_write_time(plugin_path, time_point);
        if(sfs::is_symlink(plugin_path))
        {
          // read_symlink may return a relative target, which would otherwise be
          // resolved against the current working directory; resolve it relative
          // to the link's own directory instead. Guard against broken links so a
          // single bad symlink does not abort the whole write.
          try
          {
            sfs::path actual_path = sfs::read_symlink(plugin_path);
            if(actual_path.is_relative())
              actual_path = plugin_path.parent_path() / actual_path;
            sfs::last_write_time(actual_path, time_point);
          }
          catch(const sfs::filesystem_error&)
          {
          }
        }
      }
    }
  }
}

bool LootDeployer::isImplicitlyManagedPlugin(const std::string& plugin_name) const
{
  // Only filter for games which load their base-game/DLC masters and Creation Club content
  // implicitly. Any other game keeps writing every enabled plugin to its load order file.
  const auto iter = IMPLICIT_BASE_PLUGINS.find(app_type_);
  if(iter == IMPLICIT_BASE_PLUGINS.end())
    return false;

  // Strip any directory components and lower case the name for case insensitive comparison, since
  // plugin file names may be stored with arbitrary casing on case insensitive game file systems.
  std::string name = sfs::path(plugin_name).filename().string();
  for(char& c : name)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  // Creation Club plugins use a "cc" prefix (e.g. ccBGSSSE001-Fish.esl). Be conservative and only
  // treat names following the known Creation Club naming scheme as managed: the "cc" prefix
  // followed by a publisher/index tag of at least two alphanumerics and a plugin extension.
  std::regex cc_regex(R"(^cc[a-z0-9]{2,}.*\.es[lmp]$)");
  if(std::regex_match(name, cc_regex))
    return true;

  // Exclude the hardcoded set of well known vanilla master plugin names for this game.
  return iter->second.contains(name);
}

void LootDeployer::saveSettings() const
{
  Json::Value settings;
  settings["num_profiles"] = num_profiles_;
  settings["current_profile"] = current_profile_;
  settings["list_download_time"] = list_download_time_;
  settings["auto_update_master_list"] = auto_update_lists_;
  sfs::path settings_file_path = dest_path_ / config_file_name_;
  std::ofstream file(settings_file_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not write to \"" + settings_file_path.string() + "\".");
  file << settings;
  file.close();
}

void LootDeployer::loadSettings()
{
  loadSettingsPrivate();
}

void LootDeployer::updateAppType()
{
  for(const auto& [type, file] : TYPE_IDENTIFIERS)
  {
    if(pu::pathExists(file, source_path_))
    {
      app_type_ = type;
      if(APP_TYPE_WITH_FILE_MOD_ORDER.contains(type))
      {
        app_plugin_file_name_ = PLUGIN_FILE_NAMES.at(type);
        plugin_file_name_ = LOADORDER_FILE_NAME;
      }
      else
      {
        plugin_file_name_ = PLUGIN_FILE_NAMES.at(type);
        app_plugin_file_name_ = LOADORDER_FILE_NAME;
      }
      // Resolve the actual on-disk filename for both the internal load-order file
      // and the game-facing plugin file. On case-sensitive Linux filesystems, the
      // game may have created e.g. "Plugins.txt" (Oblivion) but our constant holds
      // "plugins.txt", so writing to the wrong case creates a second file the game
      // never reads. Both names need the same case-resolution treatment.
      // Fixes limo-app/limo#38 / limo-app/limo#184.
      auto file_name = pu::pathExists(plugin_file_name_, dest_path_);
      if(file_name)
        plugin_file_name_ = *file_name;
      auto app_file_name = pu::pathExists(app_plugin_file_name_, dest_path_);
      if(app_file_name)
        app_plugin_file_name_ = *app_file_name;
      return;
    }
  }
  throw std::runtime_error("Could not identify game type in '" + source_path_.string() + "'");
}

void LootDeployer::updateMasterList()
{
  if(!auto_update_lists_)
    return;
  const auto cur_time = std::chrono::system_clock::now();
  const std::chrono::time_point<std::chrono::system_clock> update_time{
    std::chrono::seconds(list_download_time_)};
  const auto one_hour_ago = cur_time - std::chrono::hours(1);
  if(update_time >= one_hour_ago && sfs::exists(dest_path_ / "masterlist.yaml"))
    return;

  downloadList(LIST_URLS.at(app_type_), "masterlist.yaml");
  downloadList(PRELUDE_URL, "prelude.yaml");

  list_download_time_ =
    std::chrono::duration_cast<std::chrono::seconds>(cur_time.time_since_epoch()).count();
  saveSettings();
}

void LootDeployer::resetSettings()
{
  resetSettingsPrivate();
}

void LootDeployer::setupPluginFiles()
{
  if(sfs::exists(dest_path_ / plugin_file_name_) && sfs::exists(dest_path_ / app_plugin_file_name_))
    return;
  updatePlugins();
}

void LootDeployer::updatePluginTags()
{
  updatePluginTagsPrivate();
}

void LootDeployer::readPluginTags()
{
  const sfs::path tag_file_path = dest_path_ / tags_file_name_;
  if(!sfs::exists(tag_file_path))
  {
    updatePluginTagsPrivate();
    return;
  }
  tags_.clear();
  num_light_plugins_ = 0;
  num_master_plugins_ = 0;
  num_standard_plugins_ = 0;
  std::ifstream file(tag_file_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not read from \"" + tag_file_path.string() + "\".");
  Json::Value json;
  try
  {
    file >> json;
  }
  catch(const std::exception&)
  {
    // A malformed .loot_tags file should not abort loading: rebuild the tags
    // from the plugins instead, mirroring the size-mismatch fallback below.
    file.close();
    updatePluginTagsPrivate();
    return;
  }
  file.close();
  for(Json::ArrayIndex i = 0; i < json.size(); i++)
  {
    tags_.push_back({});
    for(Json::ArrayIndex j = 0; j < json[i].size(); j++)
    {
      const std::string tag = json[i][j].asString();
      tags_[i].push_back(tag);
      if(tag == LIGHT_PLUGIN)
        num_light_plugins_++;
      else if(tag == MASTER_PLUGIN)
        num_master_plugins_++;
      else if(tag == STANDARD_PLUGIN)
        num_standard_plugins_++;
    }
  }
  if(tags_.size() != plugins_.size())
    updatePluginTagsPrivate();
}

namespace
{
/*!
 * \brief Percent-encodes characters in a URL that are unsafe to transmit while
 * preserving characters that carry URL structure (scheme, host, path and query
 * delimiters). Generalizes the previous space-only substitution so that spaces,
 * control characters, non-ASCII bytes and other unsafe characters are encoded.
 * \param url The URL to encode.
 * \return The encoded URL.
 */
std::string encodeUrl(const std::string& url)
{
  // Unreserved characters (RFC 3986) plus the reserved characters that may
  // legitimately appear unencoded in a full URL are left untouched; everything
  // else is percent-encoded.
  static const std::string safe_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    "-._~"          // unreserved
    ":/?#[]@"       // gen-delims
    "!$&'()*+,;=";  // sub-delims
  const auto is_hex = [](char ch)
  { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; };
  std::string encoded;
  encoded.reserve(url.size());
  for(std::size_t i = 0; i < url.size(); i++)
  {
    const unsigned char c = static_cast<unsigned char>(url[i]);
    // Preserve already valid percent-encoded triplets so that pre-encoded URLs
    // are not double-encoded.
    if(c == '%' && i + 2 < url.size() && is_hex(url[i + 1]) && is_hex(url[i + 2]))
    {
      encoded.push_back('%');
      encoded.push_back(url[i + 1]);
      encoded.push_back(url[i + 2]);
      i += 2;
    }
    else if(c == '%' || safe_chars.find(static_cast<char>(c)) == std::string::npos)
    {
      static const char hex_digits[] = "0123456789ABCDEF";
      encoded.push_back('%');
      encoded.push_back(hex_digits[(c >> 4) & 0x0F]);
      encoded.push_back(hex_digits[c & 0x0F]);
    }
    else
      encoded.push_back(static_cast<char>(c));
  }
  return encoded;
}
}

void LootDeployer::downloadList(std::string url, const std::string& file_name)
{
  const std::string tmp_file_name = file_name + ".tmp";
  std::ofstream fstream(dest_path_ / tmp_file_name, std::ios::binary);
  if(!fstream.is_open())
    throw std::runtime_error("Failed to update " + file_name + ": Could not write to: \"" +
                             dest_path_.string() + "\".");

  url = encodeUrl(url);
  cpr::Response response = cpr::Download(fstream, cpr::Url{ url });
  if(response.status_code != 200)
  {
    sfs::remove(dest_path_ / tmp_file_name);
    throw std::runtime_error("Could not download " + file_name + " from '" +
                             url + "'.\nTry to update the URL in the " +
                             "settings. Alternatively, you can manually download the " +
                             "file and place it in '" + dest_path_.string() +
                             "'. You can disable auto updates in '" +
                             (dest_path_ / config_file_name_).string() + "'.");
  }
  sfs::remove(dest_path_ / file_name);
  sfs::rename(dest_path_ / tmp_file_name, dest_path_ / file_name);
}

void LootDeployer::restoreUndeployBackupIfExists()
{
  const std::string loadorder_backup_path =
    dest_path_ / ("." + app_plugin_file_name_ + UNDEPLOY_BACKUP_EXTENSION);
  const std::string plugin_backup_path =
    dest_path_ / ("." + plugin_file_name_ + UNDEPLOY_BACKUP_EXTENSION);
  if(pu::exists(loadorder_backup_path) && !pu::exists(plugin_backup_path))
    sfs::remove(loadorder_backup_path);
  else if(!pu::exists(loadorder_backup_path) && pu::exists(plugin_backup_path))
    sfs::remove(plugin_backup_path);
  else if(sfs::exists(loadorder_backup_path) && sfs::exists(plugin_backup_path))
  {
    log_(Log::LOG_DEBUG, std::format("Deployer '{}': Restoring undeploy backup.", name_));
    sfs::remove(dest_path_ / app_plugin_file_name_);
    sfs::rename(loadorder_backup_path, dest_path_ / app_plugin_file_name_);
    sfs::remove(dest_path_ / plugin_file_name_);
    sfs::rename(plugin_backup_path, dest_path_ / plugin_file_name_);
    loadPlugins();
  }
}

void LootDeployer::loadSettingsPrivate()
{
  Json::Value settings;
  sfs::path settings_file_path = dest_path_ / config_file_name_;
  if(!sfs::exists(settings_file_path))
  {
    resetSettingsPrivate();
    return;
  }
  std::ifstream file(settings_file_path, std::fstream::binary);
  if(!file.is_open())
  {
    resetSettingsPrivate();
    return;
  }
  try
  {
    file >> settings;
  }
  catch(const std::exception&)
  {
    // A malformed config file should not abort loading: fall back to defaults,
    // consistent with the other error branches above.
    file.close();
    resetSettingsPrivate();
    return;
  }
  file.close();
  if(!settings.isMember("num_profiles") || !settings.isMember("current_profile") ||
     !settings.isMember("list_download_time") || !settings.isMember("auto_update_master_list"))
  {
    resetSettingsPrivate();
    return;
  }
  num_profiles_ = settings["num_profiles"].asInt();
  current_profile_ = settings["current_profile"].asInt();
  list_download_time_ = settings["list_download_time"].asInt64();
  auto_update_lists_ = settings["auto_update_master_list"].asBool();
}

void LootDeployer::resetSettingsPrivate()
{
  num_profiles_ = 1;
  current_profile_ = 0;
  auto_update_lists_ = true;
  list_download_time_ = 0;
}

void LootDeployer::updatePluginTagsPrivate()
{
  tags_.clear();
  auto loot_handle = loot::CreateGameHandle(app_type_, source_path_, dest_path_);
  std::vector<sfs::path> plugin_paths;
  plugin_paths.reserve(plugins_.size());
  for(const auto& [path, s] : plugins_)
    plugin_paths.emplace_back(source_path_ / path);
  loot_handle->LoadPlugins(plugin_paths, false);
  num_light_plugins_ = 0;
  num_master_plugins_ = 0;
  num_standard_plugins_ = 0;
  for(int i = 0; i < plugins_.size(); i++)
  {
    auto plugin = loot_handle->GetPlugin(plugins_[i].first);
    // GetPlugin returns null when the plugin file was not actually loaded
    // (e.g. the file does not exist on disk). Guard against this to avoid a
    // null-dereference crash (limo-app/limo#185, limo-app/limo#31).
    if(!plugin)
    {
      log_(Log::LOG_WARNING,
           std::format("LOOT: Plugin '{}' could not be loaded (file missing?), "
                       "treating as Standard plugin.",
                       plugins_[i].first));
      num_standard_plugins_++;
      tags_.push_back({ STANDARD_PLUGIN });
      continue;
    }
    if(plugin->IsLightPlugin())
    {
      num_light_plugins_++;
      tags_.push_back({ LIGHT_PLUGIN });
    }
    else if(plugin->IsMaster())
    {
      num_master_plugins_++;
      tags_.push_back({ MASTER_PLUGIN });
    }
    else
    {
      num_standard_plugins_++;
      tags_.push_back({ STANDARD_PLUGIN });
    }
  }
  writePluginTags();
}

void LootDeployer::loadLists(loot::DatabaseInterface& database,
                            const sfs::path& master_list_path,
                            const sfs::path& user_list_path,
                            const sfs::path& prelude_path)
{
  if(!master_list_path.empty())
  {
    if(!prelude_path.empty())
      database.LoadMasterlistWithPrelude(master_list_path, prelude_path);
    else
      database.LoadMasterlist(master_list_path);
  }
  if(!user_list_path.empty())
    database.LoadUserlist(user_list_path);
}
