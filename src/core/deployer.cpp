#include "deployer.h"
#include "pathutils.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <ranges>
#include <set>
#include <unordered_set>

namespace str = std::ranges;
namespace stv = std::views;
namespace sfs = std::filesystem;
namespace pu = path_utils;


Deployer::Deployer(const sfs::path& source_path,
                   const sfs::path& dest_path,
                   const std::string& name,
                   DeployMode deploy_mode) :
  source_path_(source_path), dest_path_(dest_path), name_(name), deploy_mode_(deploy_mode)
{}

std::string Deployer::getDestPath() const
{
  return dest_path_;
}

std::string Deployer::getSourcePath() const
{
  return source_path_;
}

std::string Deployer::getName() const
{
  return name_;
}

void Deployer::setName(const std::string& name)
{
  name_ = name;
}

std::map<int, unsigned long> Deployer::deploy(const std::vector<int>& loadorder,
                                              std::optional<ProgressNode*> progress_node)
{
  auto [source_files, mod_sizes] = getDeploymentSourceFilesAndModSizes(loadorder);
  log_(Log::LOG_INFO,
       std::format("Deployer '{}': Deploying {} files for {} mods...",
                   name_,
                   source_files.size(),
                   loadorder.size()));
  if(progress_node)
    (*progress_node)->addChildren({ 2, 5, 1 });
  std::map<sfs::path, int> dest_files =
    loadDeployedFiles(progress_node ? &(*progress_node)->child(0) : std::optional<ProgressNode*>{});
  backupOrRestoreFiles(source_files, dest_files);
  deployFiles(source_files,
              progress_node ? &(*progress_node)->child(1) : std::optional<ProgressNode*>{});
  saveDeployedFiles(source_files,
                    progress_node ? &(*progress_node)->child(2) : std::optional<ProgressNode*>{});
  return mod_sizes;
}

std::map<int, unsigned long> Deployer::deploy(std::optional<ProgressNode*> progress_node)
{
  std::vector<int> loadorder;
  for(auto const& lo : *loadorders_[current_profile_])
  {
    auto mod_info = std::static_pointer_cast<DeployerModInfo>(lo.lock());
    if(!mod_info->isSeparator && mod_info->enabled)
      loadorder.push_back(mod_info->id);
  }
  return deploy(loadorder, progress_node);
}

Deployer::DeploymentPlan Deployer::computeDeploymentPlan(const std::vector<int>& loadorder) const
{
  // What would be deployed for this load order (relative path -> winning mod id).
  // Reuses the exact source enumeration used by deploy() so the preview cannot drift from it.
  auto [source_files, mod_sizes] = getDeploymentSourceFilesAndModSizes(loadorder);
  // What is currently recorded as deployed (relative path -> source mod id) from .lmmfiles.
  const std::map<sfs::path, int> dest_files = loadDeployedFiles();

  DeploymentPlan plan;
  plan.deployer_name = name_;

  // Walk the source map: every path is either new (create) or already deployed. If already
  // deployed but owned by a different mod, the winning mod changes -> overwrite.
  for(const auto& [path, mod_id] : source_files)
  {
    // Directories are created implicitly when their files are deployed; skip them.
    if(sfs::is_directory(source_path_ / std::to_string(mod_id) / path))
      continue;
    const auto dest_it = dest_files.find(path);
    if(dest_it == dest_files.end())
      plan.to_create.push_back({ path, mod_id, -1 });
    else if(dest_it->second != mod_id)
      plan.to_overwrite.push_back({ path, mod_id, dest_it->second });
  }

  // Walk the recorded deployed files: any path no longer present in the source map would be
  // removed and any backup of it restored.
  for(const auto& [path, mod_id] : dest_files)
  {
    if(source_files.find(path) != source_files.end())
      continue;
    // Skip directory records; they are removed implicitly once empty.
    if(sfs::is_directory(dest_path_ / path))
      continue;
    plan.to_remove.push_back({ path, -1, mod_id });
  }

  return plan;
}

Deployer::DeploymentPlan Deployer::computeDeploymentPlan() const
{
  std::vector<int> loadorder;
  for(auto const& lo : *loadorders_[current_profile_])
  {
    auto mod_info = std::static_pointer_cast<DeployerModInfo>(lo.lock());
    if(mod_info && !mod_info->isSeparator && mod_info->enabled)
      loadorder.push_back(mod_info->id);
  }
  return computeDeploymentPlan(loadorder);
}

void Deployer::unDeploy(std::optional<ProgressNode*> progress_node)
{
  log_(Log::LOG_DEBUG, "Undeploying...");
  deploy({}, progress_node);
}

void Deployer::setLoadorder(const std::shared_ptr<TreeItem<DeployerEntry>> loadorder)
{
  loadorders_[current_profile_] = loadorder;
}

void Deployer::setLoadorder(Json::Value entry, std::shared_ptr<TreeItem<DeployerEntry>> current)
{
  if(entry.isObject())
  {
    if (entry.isMember("status")) {
      if(!entry["id"].isInt() || !entry["status"].isBool())
      {
        log_(Log::LOG_WARNING,
             std::format("Deployer '{}': Skipping malformed load-order entry", name_));
        return;
      }
      current->emplace_back(
        make_shared<DeployerModInfo>(false, std::string(""), "", entry["id"].asInt(),
                            entry["status"].asBool()));
    }
    else if (entry.isMember("name")){
      if(!entry["name"].isString())
      {
        log_(Log::LOG_WARNING,
             std::format("Deployer '{}': Skipping malformed load-order group entry", name_));
        return;
      }
      auto data = make_shared<DeployerEntry>(true, entry["name"].asString());
      data->isExpanded = entry["expanded"].isBool() && entry["expanded"].asBool();
      current->emplace_back(data);
      if(entry["children"].isArray())
      {
        for (const auto& sub_entry : entry["children"])
        {
          setLoadorder(sub_entry, current->back());
        }
      }
    }
  }
}

void Deployer::setLoadorder(Json::Value loadorder)
{
  if (!loadorder["children"].isNull() && loadorder["children"].isArray())
  {
    for (const auto& entry : loadorder["children"])
    {
      setLoadorder(entry, loadorders_[current_profile_]);
    }
  }
}

std::shared_ptr<TreeItem<DeployerEntry>> Deployer::getLoadorder()
{
  if(loadorders_.empty() || current_profile_ < 0 || current_profile_ >= loadorders_.size() ||
     loadorders_[current_profile_]->empty())
    return std::make_shared<TreeItem<DeployerEntry>>(std::make_shared<DeployerEntry>(true, "Root"), nullptr);
  return loadorders_[current_profile_];
}

std::string Deployer::getType() const
{
  return type_;
}

void Deployer::swapChild(int from_index, int to_index)
{
  if(to_index == from_index || to_index < 0 || to_index >= loadorders_[current_profile_]->size())
    return;
  loadorders_[current_profile_]->swapChild(from_index, to_index);
}

void Deployer::swapNodes (std::shared_ptr<TreeItem<DeployerEntry>> node_a,
                                 std::shared_ptr<TreeItem<DeployerEntry>> node_b)
{
  if(node_a == node_b || !node_a || !node_b)
    return;
  loadorders_[current_profile_]->swapNodes(node_a, node_b);
}

bool Deployer::addMod(int mod_id, bool enabled, bool update_conflicts)
{
  if(hasMod(mod_id))
    return false;
  loadorders_[current_profile_]->emplace_back(
    std::make_shared<DeployerModInfo>(false, "", "", mod_id, enabled));
  if(update_conflicts && auto_update_conflict_groups_)
    updateConflictGroups();
  return true;
}

bool Deployer::removeNode(void *node_ptr)
{
  auto item = static_cast<TreeItem<DeployerEntry>*>(node_ptr)->shared_from_this();
  item->parent()->remove(item);
  if(auto_update_conflict_groups_)
    updateConflictGroups();
  return true;
}

bool Deployer::removeMod(int mod_id)
{
  auto iter = std::find_if(loadorders_[current_profile_]->begin(),
                           loadorders_[current_profile_]->end(),
                           [mod_id](auto entry) { return entry.lock()->id == mod_id; });
  if(iter == loadorders_[current_profile_]->end())
    return false;
  loadorders_[current_profile_]->erase(*iter);
  if(auto_update_conflict_groups_)
    updateConflictGroups();
  return true;
}

void Deployer::setModStatus(int mod_id, bool status)
{
  if (mod_id < 0) // Separator
    return;
  auto iter = std::find_if(loadorders_[current_profile_]->begin(),
                           loadorders_[current_profile_]->end(),
                           [mod_id](const auto& entry) { return entry.lock()->id == mod_id; });
  if(iter == loadorders_[current_profile_]->end())
    return;
  auto deployer_mod = static_pointer_cast<DeployerModInfo>(iter->lock());
  if (deployer_mod != nullptr && !deployer_mod->isSeparator)
    deployer_mod->enabled = status;
  return;
}

bool Deployer::hasMod(int mod_id)
{
  return std::find_if(loadorders_[current_profile_]->begin(),
                      loadorders_[current_profile_]->end(),
                      [mod_id](const auto& entry) { return entry.lock()->id == mod_id; }) !=
         loadorders_[current_profile_]->end();
}

std::vector<ConflictInfo> Deployer::getFileConflicts(
  int mod_id,
  bool show_disabled,
  std::optional<ProgressNode*> progress_node)
{
  std::vector<ConflictInfo> conflicts;
  if(!checkModPathExistsAndMaybeLogError(mod_id))
    return conflicts;
  std::vector<std::string> mod_files = getModFiles(mod_id, false);
  std::vector<int> loadorder;
  for(const auto& entry_weak : loadorders_[current_profile_]->getTraversalItems())
  {
    auto entry = static_pointer_cast<DeployerModInfo>(entry_weak.lock());
    if (!entry->isSeparator) {
      if(entry->enabled || show_disabled)
        loadorder.push_back(entry->id);
    }
  }

  if(progress_node)
    (*progress_node)->setTotalSteps(loadorder.size() * mod_files.size());
  std::vector<std::vector<int>> overwrite_orders;
  overwrite_orders.reserve(mod_files.size());
  for(const auto& [i, path] : str::enumerate_view(mod_files))
  {
    overwrite_orders.push_back({});
    for(int cur_id : loadorder)
    {
      if(!checkModPathExistsAndMaybeLogError(cur_id))
        continue;
      if(sfs::exists(source_path_ / std::to_string(cur_id) / path))
        overwrite_orders[i].push_back(cur_id);
      if(progress_node)
        (*progress_node)->advance();
    }
  }

  for(const auto& [path, order] : str::zip_view(mod_files, overwrite_orders))
  {
    if(order.size() > 1)
      conflicts.push_back({ path, order, {} });
  }

  return conflicts;
}

int Deployer::getNumMods()
{
  return loadorders_[current_profile_]->size();
}

const std::filesystem::path& Deployer::destPath() const
{
  return dest_path_;
}

void Deployer::setDestPath(const sfs::path& path)
{
  dest_path_ = path;
}

std::unordered_set<int> Deployer::getModConflicts(int mod_id,
                                                  std::optional<ProgressNode*> progress_node)
{
  std::unordered_set<int> conflicts{ mod_id };
  std::vector<std::string> mod_files = getModFiles(mod_id, false);
  if(!checkModPathExistsAndMaybeLogError(mod_id))
    return conflicts;
  auto what = loadorders_[current_profile_];
  if(progress_node)
    (*progress_node)->setTotalSteps(loadorders_[current_profile_]->size());
  for(const auto &entry_weak : *loadorders_[current_profile_])
  {
    auto entry = static_pointer_cast<DeployerModInfo>(entry_weak.lock());
    if(entry->isSeparator || !checkModPathExistsAndMaybeLogError(entry->id))
      continue;
    for(const auto& path : mod_files)
    {
      if(sfs::exists(source_path_ / std::to_string(entry->id) / path))
      {
        conflicts.insert(entry->id);
        break;
      }
    }
    if(progress_node)
      (*progress_node)->advance();
  }
  return conflicts;
}

void Deployer::addProfile(int source)
{
  if(source < 0 || source >= loadorders_.size())
  {
    auto root = std::make_shared<TreeItem<DeployerEntry>>(std::make_shared<DeployerEntry>(true, "Root"), nullptr);
    loadorders_.push_back(root);
    conflict_groups_.push_back(std::vector<std::vector<int>>{});
  }
  else
  {
    loadorders_.push_back(loadorders_[source]);
    conflict_groups_.push_back(conflict_groups_[source]);
  }
}

void Deployer::removeProfile(int profile)
{
  loadorders_.erase(loadorders_.begin() + profile);
  conflict_groups_.erase(conflict_groups_.begin() + profile);
  if(profile == current_profile_)
    setProfile(0);
  else if(profile < current_profile_)
    setProfile(current_profile_ - 1);
}

void Deployer::setProfile(int profile)
{
  current_profile_ = profile;
}

int Deployer::getProfile() const
{
  return current_profile_;
}

std::pair<int, std::string> Deployer::verifyDirectories()
{
  std::string file_name = "_lmm_write_test_file_";
  try
  {
    std::ofstream file(source_path_ / file_name);
    if(file.is_open())
      file << "test";
  }
  catch(const std::ios_base::failure& f)
  {
    return { 1,
             std::format("Deployer name: '{}', type: '{}'. The error was: '{}'. (Code: {}. Message: '{}')",
                         name_,
                         type_,
                         f.what(),
                         f.code().value(),
                         f.code().message()) };
  }
  // When the source and destination directories are identical, writability is already proven by
  // the test file written above. Removing dest/test_file would delete that very file and the
  // subsequent link/copy of a file onto itself would fail, so report success directly.
  if(source_path_ == dest_path_)
  {
    sfs::remove(source_path_ / file_name);
    return { 0, "" };
  }
  try
  {
    sfs::remove(dest_path_ / file_name);
    if(deploy_mode_ == copy)
      sfs::copy_file(source_path_ / file_name, dest_path_ / file_name);
    else if(deploy_mode_ == sym_link)
      sfs::create_symlink(source_path_ / file_name, dest_path_ / file_name);
    else
      sfs::create_hard_link(source_path_ / file_name, dest_path_ / file_name);
  }
  catch(sfs::filesystem_error& e)
  {
    if(deploy_mode_ != hard_link)
    {
      sfs::remove(source_path_ / file_name);
      return { 3,
               std::format("Deployer name: '{}', type: '{}'. The error was: '{}'..(Code: {}. Message: '{}')",
                           name_,
                           type_,
                           e.what(),
                           e.code().value(),
                           e.code().message()) };
    }
    else
    {
      try
      {
        sfs::copy_file(source_path_ / file_name, dest_path_ / file_name);
      }
      catch(sfs::filesystem_error& e)
      {
        sfs::remove(source_path_ / file_name);
        return { 3,
                 std::format("Deployer name: '{}', type: '{}'. The error was: '{}'..(Code: {}. Message: '{}')",
                             name_,
                             type_,
                             e.what(),
                             e.code().value(),
                             e.code().message()) };
      }
      sfs::remove(source_path_ / file_name);
      return { 2,
               std::format("Deployer name: '{}', type: '{}'. The error was: '{}'..(Code: {}. Message: '{}')",
                           name_,
                           type_,
                           e.what(),
                           e.code().value(),
                           e.code().message()) };
    }
  }
  sfs::remove(source_path_ / file_name);
  sfs::remove(dest_path_ / file_name);
  return { 0, "" };
}

bool Deployer::swapMod(int old_id, int new_id)
{
  if (old_id == new_id)
    return false;
  auto weak_iter = std::find_if(loadorders_[current_profile_]->begin(),
                           loadorders_[current_profile_]->end(),
                           [old_id](auto entry) { return entry.lock()->id == old_id; });
  if(weak_iter == loadorders_[current_profile_]->end())
    return false;
  auto shared_iter = weak_iter->lock();
  shared_iter->id = new_id;
  if(auto_update_conflict_groups_)
    updateConflictGroups();
  return true;
}

void Deployer::sortModsByConflicts(std::optional<ProgressNode*> progress_node)
{
  updateConflictGroups(progress_node);
  auto new_loadorder = std::make_shared<TreeItem<DeployerEntry>>(std::make_shared<DeployerEntry>(true, "Root"), nullptr);
  int i = 0;
  for(const auto& group : conflict_groups_[current_profile_])
  {
    for(int mod_id : group)
    {
      auto iter = str::find_if(*loadorders_[current_profile_],
                               [mod_id](auto entry) { return entry.lock()->id == mod_id; });
      if(iter == loadorders_[current_profile_]->end())
        continue; // conflict group references a mod no longer in the load order (desync)
      new_loadorder->emplace_back(iter->lock());
    }
    i++;
  }
  loadorders_[current_profile_] = new_loadorder;
}

void Deployer::setLoadorderByModIds(const std::vector<int>& ordered_mod_ids)
{
  auto new_loadorder = std::make_shared<TreeItem<DeployerEntry>>(
    std::make_shared<DeployerEntry>(true, "Root"), nullptr);
  std::set<int> placed;
  // First, the requested mods in the requested order (only those this deployer actually has).
  for(int mod_id : ordered_mod_ids)
  {
    if(placed.contains(mod_id))
      continue;
    auto iter = str::find_if(*loadorders_[current_profile_],
                             [mod_id](auto entry) { return entry.lock()->id == mod_id; });
    if(iter == loadorders_[current_profile_]->end())
      continue;
    new_loadorder->emplace_back(iter->lock());
    placed.insert(mod_id);
  }
  // Then every remaining mod, preserving its current relative order.
  for(auto iter = loadorders_[current_profile_]->begin();
      iter != loadorders_[current_profile_]->end();
      ++iter)
  {
    const int mod_id = iter->lock()->id;
    if(placed.contains(mod_id))
      continue;
    new_loadorder->emplace_back(iter->lock());
    placed.insert(mod_id);
  }
  loadorders_[current_profile_] = new_loadorder;
}

std::vector<std::vector<int>> Deployer::getConflictGroups() const
{
  return conflict_groups_[current_profile_];
}

void Deployer::setConflictGroups(const std::vector<std::vector<int>>& newConflict_groups)
{
  conflict_groups_[current_profile_] = newConflict_groups;
}

Deployer::DeployMode Deployer::getDeployMode() const
{
  return deploy_mode_;
}

void Deployer::setDeployMode(DeployMode deploy_mode)
{
  deploy_mode_ = deploy_mode;
}

bool Deployer::isAutonomous()
{
  return is_autonomous_;
}

std::vector<std::string> Deployer::getModNames() const
{
  return {};
}

std::filesystem::path Deployer::sourcePath() const
{
  return source_path_;
}

void Deployer::setSourcePath(const sfs::path& newSourcePath)
{
  source_path_ = newSourcePath;
}

std::pair<std::map<std::filesystem::path, int>, std::map<int, unsigned long>>
Deployer::getDeploymentSourceFilesAndModSizes(const std::vector<int>& loadorder) const
{
  std::map<sfs::path, int> source_files{};
  std::map<int, unsigned long> mod_sizes{};
  for(int i = loadorder.size() - 1; i >= 0; i--)
  {
    if(!checkModPathExistsAndMaybeLogError(loadorder[i]))
      continue;
    sfs::path mod_base_path = source_path_ / std::to_string(loadorder[i]);
    unsigned long mod_size = 0;
    for(auto const& dir_entry : sfs::recursive_directory_iterator(mod_base_path))
    {
      // Security: never deploy symlinks (a mod could link to a file outside its directory).
      if(dir_entry.is_symlink())
        continue;
      const bool is_regular_file = dir_entry.is_regular_file();
      // Skip blacklisted files (e.g. README.md, codes.txt) so they are never deployed.
      if(is_regular_file && isIgnoredFile(dir_entry.path()))
        continue;
      if(is_regular_file)
        mod_size += dir_entry.file_size();
      if(is_regular_file || dir_entry.is_directory())
        source_files.insert({ pu::getRelativePath(dir_entry.path(), mod_base_path), loadorder[i] });
    }
    mod_sizes[loadorder[i]] = mod_size;
  }
  return { source_files, mod_sizes };
}

void Deployer::backupOrRestoreFiles(const std::map<sfs::path, int>& source_files,
                                    const std::map<sfs::path, int>& dest_files) const
{
  std::map<sfs::path, int> restore_targets;
  std::map<sfs::path, int> backup_targets;
  std::set_difference(dest_files.begin(),
                      dest_files.end(),
                      source_files.begin(),
                      source_files.end(),
                      std::inserter(restore_targets, restore_targets.begin()),
                      dest_files.value_comp());
  std::set_difference(source_files.begin(),
                      source_files.end(),
                      dest_files.begin(),
                      dest_files.end(),
                      std::inserter(backup_targets, backup_targets.begin()),
                      source_files.value_comp());

  std::map<sfs::path, int> restore_directories;
  for(const auto& [path, id] : restore_targets)
  {
    sfs::path absolute_path = dest_path_ / path;
    if(!pu::exists(absolute_path))
      continue;
    if(sfs::is_directory(absolute_path))
    {
      restore_directories[path] = id;
      continue;
    }
    // Only remove the orphaned target if it is still the file Limo deployed. This prevents
    // deleting files a user has put in place of (or in addition to) a previously deployed mod
    // file. For copy mode the origin cannot be determined, so the recorded entry is trusted.
    if(!targetWasDeployedByLimo(absolute_path, id, path))
    {
      log_(Log::LOG_DEBUG,
           std::format("Deployer '{}': Skipping cleanup of '{}', it no longer matches the "
                       "deployed file",
                       name_,
                       absolute_path.string()));
      continue;
    }
    sfs::path backup_name = absolute_path.string() + backup_extension_;
    // Restore atomically: when a backup exists, rename overwrites the deployed file in one step so
    // an I/O failure can't leave the target missing. Only plainly remove the deployed file when
    // there is no backup (it was added by a mod rather than backed up from the game).
    if(pu::exists(backup_name))
      sfs::rename(backup_name, absolute_path);
    else
      sfs::remove(absolute_path);
  }
  // Remove now-empty directories Limo created. Iterate deepest first so that emptying a child
  // directory allows its parent to be removed within this single pass.
  for(const auto& [path, id] : restore_directories | stv::reverse)
  {
    sfs::path absolute_path = dest_path_ / path;
    if(pu::directoryIsEmpty(absolute_path, { managed_dir_file_name_ }))
      sfs::remove_all(absolute_path);
  }

  for(const auto& [path, id] : backup_targets)
  {
    sfs::path absolute_path = dest_path_ / path;
    sfs::path backup_name = absolute_path.string() + backup_extension_;
    if(pu::exists(absolute_path) && !sfs::is_directory(absolute_path))
      sfs::rename(absolute_path, backup_name);
  }
}

void Deployer::deployFiles(const std::map<sfs::path, int>& source_files,
                           std::optional<ProgressNode*> progress_node) const
{
  if(progress_node)
    (*progress_node)->setTotalSteps(source_files.size());

  for(const auto& [path, id] : source_files)
  {
    sfs::path dest_path = dest_path_ / path;
    if(!checkModPathExistsAndMaybeLogError(id))
      continue;
    sfs::path source_path = source_path_ / std::to_string(id) / path;
    if(sfs::is_directory(source_path) ||
       pu::exists(dest_path) && (deploy_mode_ == hard_link && !sfs::is_symlink(dest_path) &&
                                    sfs::equivalent(source_path, dest_path) ||
                                  deploy_mode_ == sym_link && sfs::is_symlink(dest_path) &&
                                    sfs::read_symlink(dest_path) == source_path))
    {
      if(progress_node)
        (*progress_node)->advance();
      continue;
    }
    const auto parent_path = dest_path.parent_path();
    sfs::create_directories(parent_path);
    removeManagedDirFile(parent_path);
    // Build the new link/copy under a temporary name in the same directory, then atomically rename
    // it over dest_path. sfs::rename replaces the existing target on a single filesystem, so an
    // interrupted deploy never leaves the target absent (limo-app/limo audit F125).
    const sfs::path temp_path =
      parent_path / (dest_path.filename().string() + ".limo_deploy_tmp");
    sfs::remove(temp_path);
    try
    {
      if(deploy_mode_ == copy)
        sfs::copy_file(source_path, temp_path);
      else if(deploy_mode_ == sym_link)
        sfs::create_symlink(source_path, temp_path);
      else
        sfs::create_hard_link(source_path, temp_path);
      sfs::rename(temp_path, dest_path);
    }
    catch(const sfs::filesystem_error& e)
    {
      // Drop the partial temporary so a failed deploy does not leave stray files behind. The
      // original dest_path is untouched because the rename never completed.
      std::error_code temp_ec;
      sfs::remove(temp_path, temp_ec);
      // Hard links can't span filesystems; under Flatpak the sandbox can also place the staging and
      // target dirs on different mounts. Give actionable guidance instead of the raw errno
      // (limo-app/limo#13, #143).
      if(deploy_mode_ == hard_link && e.code() == std::errc::cross_device_link)
        throw std::runtime_error(std::format(
          "Deployer '{}': cannot hard link onto the target because the staging directory and the "
          "game directory are on different filesystems (or separated by the Flatpak sandbox). Use "
          "the 'Sym Link' or 'Copy' deploy mode, move the staging directory onto the same filesystem "
          "as the game, or grant Limo access to both locations (e.g. via Flatseal). Affected file: "
          "'{}' -> '{}'.",
          name_,
          source_path.string(),
          dest_path.string()));
      throw;
    }

    if(progress_node)
      (*progress_node)->advance();
  }
}

std::map<sfs::path, int> Deployer::loadDeployedFiles(std::optional<ProgressNode*> progress_node,
                                                     sfs::path dest_path) const
{
  if(dest_path == "")
    dest_path = dest_path_;
  if(progress_node)
  {
    (*progress_node)->addChildren({ 1, 2 });
    (*progress_node)->child(0).setTotalSteps(1);
  }
  std::map<sfs::path, int> deployed_files;
  sfs::path deployed_files_path = dest_path / deployed_files_name_;
  if(!sfs::exists(deployed_files_path))
    return deployed_files;
  std::ifstream file(deployed_files_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Could not read \"" + deployed_files_path.string() + "\"");
  Json::Value json_object;
  try
  {
    file >> json_object;
  }
  catch(const std::exception& e)
  {
    log_(Log::LOG_ERROR,
         std::format("Deployer '{}': Failed to parse deployed files record '{}': {}",
                     name_,
                     deployed_files_path.string(),
                     e.what()));
    return deployed_files;
  }
  if(!json_object["files"].isArray())
  {
    if(progress_node)
      (*progress_node)->child(0).advance();
    return deployed_files;
  }
  if(progress_node)
  {
    (*progress_node)->child(0).advance();
    (*progress_node)->child(1).setTotalSteps(json_object["files"].size());
  }
  for(Json::ArrayIndex i = 0; i < json_object["files"].size(); i++)
  {
    const Json::Value& entry = json_object["files"][i];
    if(entry.isMember("path") && entry["path"].isString() && entry.isMember("mod_id") &&
       entry["mod_id"].isInt())
      deployed_files[entry["path"].asString()] = entry["mod_id"].asInt();
    if(progress_node)
      (*progress_node)->child(1).advance();
  }
  return deployed_files;
}

void Deployer::saveDeployedFiles(const std::map<sfs::path, int>& deployed_files,
                                 std::optional<ProgressNode*> progress_node) const
{
  if(progress_node)
  {
    (*progress_node)->addChildren({ 1, 1 });
    (*progress_node)->child(0).setTotalSteps(deployed_files.size());
    (*progress_node)->child(1).setTotalSteps(1);
  }
  sfs::path deployed_files_path = dest_path_ / deployed_files_name_;
  std::ofstream file(deployed_files_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Could not write \"" + deployed_files_path.string() + "\"");
  Json::Value json_object;
  int i = 0;
  for(auto const& [path, id] : deployed_files)
  {
    json_object["files"][i]["path"] = path.c_str();
    json_object["files"][i]["mod_id"] = id;
    i++;
    if(progress_node)
      (*progress_node)->child(0).advance();
  }
  file << json_object;
  file.close();
  if(progress_node)
    (*progress_node)->child(1).advance();
}

std::vector<std::string> Deployer::getModFiles(int mod_id, bool include_directories) const
{
  std::vector<std::string> mod_files;
  if(!checkModPathExistsAndMaybeLogError(mod_id))
    return mod_files;
  sfs::path mod_base_path = source_path_ / std::to_string(mod_id);
  for(const auto& dir_entry : sfs::recursive_directory_iterator(mod_base_path))
  {
    if(!dir_entry.is_directory() || include_directories)
      mod_files.push_back(pu::getRelativePath(dir_entry.path(), mod_base_path));
  }
  return mod_files;
}

bool Deployer::modPathExists(int mod_id) const
{
  return sfs::exists(source_path_ / std::to_string(mod_id));
}

bool Deployer::checkModPathExistsAndMaybeLogError(int mod_id) const
{
  if(modPathExists(mod_id))
    return true;

  log_(Log::LOG_ERROR, std::format("No installation directory exists for mod with id {}", mod_id));
  return false;
}

void Deployer::updateConflictGroups(std::optional<ProgressNode*> progress_node)
{
  log_(Log::LOG_INFO, std::format("Deployer '{}': Updating conflict groups...", name_));
  std::map<std::string, int> file_map;
  std::vector<std::set<int>> groups;
  std::vector<int> non_conflicting;
  // create groups
  if(progress_node)
    (*progress_node)->setTotalSteps(loadorders_[current_profile_]->getTraversalItems().size());
  for(const auto& entry_weak : loadorders_[current_profile_]->getTraversalItems())
  {
    auto entry = entry_weak.lock();
    if(entry->isSeparator || !checkModPathExistsAndMaybeLogError(entry->id))
      continue;
    std::string base_path = (source_path_ / std::to_string(entry->id)).string();
    for(const auto& dir_entry : sfs::recursive_directory_iterator(base_path))
    {
      if(dir_entry.is_directory())
        continue;
      const auto relative_path = pu::getRelativePath(dir_entry.path(), base_path);
      if(!file_map.contains(relative_path))
        file_map[relative_path] = entry->id;
      else
      {
        int other_id = file_map[relative_path];
        auto contains_id = [other_id](const auto& s) { return str::find(s, other_id) != s.end(); };
        auto group_iter = str::find_if(groups, contains_id);
        if(group_iter != groups.end())
          group_iter->insert(entry->id);
        else
          groups.push_back({ other_id, entry->id });
      }
    }
    if(progress_node)
      (*progress_node)->advance();
  }
  std::vector<std::set<int>> merged_groups;
  // merge groups
  for(int i = 0; i < groups.size(); i++)
  {
    if(groups[i].empty())
      continue;
    std::set<int> new_group = groups[i];
    bool found_intersection = true;
    while(found_intersection)
    {
      found_intersection = false;
      for(int j = i + 1; j < groups.size(); j++)
      {
        if(groups[j].empty())
          continue;
        std::vector<int> intersection;
        std::set_intersection(new_group.begin(),
                              new_group.end(),
                              groups[j].begin(),
                              groups[j].end(),
                              std::back_inserter(intersection));
        if(!intersection.empty())
        {
          found_intersection = true;
          new_group.merge(groups[j]);
          groups[j].clear();
        }
      }
    }
    merged_groups.push_back(std::move(new_group));
  }
  std::vector<std::vector<int>> sorted_groups(merged_groups.size() + 1, std::vector<int>());
  // sort mods
  for(const auto& entry_weak : *loadorders_[current_profile_])
  {
    auto entry = entry_weak.lock();
    if (entry->isSeparator)
      continue;
    bool is_in_group = false;
    for(int i = 0; i < merged_groups.size(); i++)
    {
      if(merged_groups[i].contains(entry->id))
      {
        sorted_groups[i].push_back(entry->id);
        is_in_group = true;
        break;
      }
    }
    if(!is_in_group)
      sorted_groups[sorted_groups.size() - 1].push_back(entry->id);
  }
  conflict_groups_[current_profile_] = sorted_groups;
  log_(Log::LOG_INFO, std::format("Deployer '{}': Conflict groups updated", name_));
}

void Deployer::setLog(const std::function<void(Log::LogLevel, const std::string&)>& newLog)
{
  log_ = newLog;
}

void Deployer::cleanup()
{
  deploy(std::vector<int>{});
  sfs::remove(dest_path_ / deployed_files_name_);
}

bool Deployer::autoUpdateConflictGroups() const
{
  return auto_update_conflict_groups_;
}

void Deployer::setAutoUpdateConflictGroups(bool status)
{
  auto_update_conflict_groups_ = status;
}

std::optional<bool> Deployer::getModStatus(int mod_id)
{
  auto iter = str::find_if(loadorders_[current_profile_]->begin(),
                            loadorders_[current_profile_]->end(),
                           [mod_id](auto entry) { return entry.lock()->id == mod_id; });
  if(iter == loadorders_[current_profile_]->end())
    return {};
  return { static_pointer_cast<DeployerModInfo>((*iter).lock())->enabled };
}

std::vector<std::vector<std::string>> Deployer::getAutoTags()
{
  return {};
}

std::map<std::string, int> Deployer::getAutoTagMap()
{
  return {};
}

std::vector<std::pair<sfs::path, int>> Deployer::getExternallyModifiedFiles(
  std::optional<ProgressNode*> progress_node) const
{
  if(deploy_mode_ == copy)
    return {};

  log_(Log::LOG_INFO, std::format("Deployer '{}': Checking for external changes...", name_));

  std::vector<std::pair<sfs::path, int>> modified_files;
  const auto deployed_files = loadDeployedFiles();

  if(progress_node)
    (*progress_node)->setTotalSteps(deployed_files.size());

  for(const auto& [path, mod_id] : deployed_files)
  {
    const auto target_path = dest_path_ / path;
    const auto mod_file_path = source_path_ / std::to_string(mod_id) / path;
    const bool file_exists_and_is_modified_link =
      modPathExists(mod_id) && pu::exists(target_path) && sfs::exists(mod_file_path) &&
      !sfs::is_directory(target_path) &&
      (deploy_mode_ == hard_link && !sfs::equivalent(mod_file_path, target_path) ||
       deploy_mode_ == sym_link &&
         (!sfs::is_symlink(target_path) || sfs::read_symlink(target_path) != mod_file_path));
    if(file_exists_and_is_modified_link)
      modified_files.emplace_back(path, mod_id);

    if(progress_node)
      (*progress_node)->advance();
  }

  if(modified_files.empty())
    log_(Log::LOG_INFO, "No changes found");
  else
    log_(Log::LOG_INFO, std::format("Found {} modified files", modified_files.size()));

  return modified_files;
}

// fork #53: deployment integrity verification
Deployer::VerificationResult Deployer::verifyDeployment(
  bool checksum,
  std::optional<ProgressNode*> progress_node) const
{
  log_(Log::LOG_INFO,
       std::format("Deployer '{}': Verifying deployment integrity{}...",
                   name_,
                   checksum ? " (with checksums)" : ""));

  VerificationResult result;
  result.used_checksums = checksum && deploy_mode_ == copy;

  const auto deployed_files = loadDeployedFiles();

  if(progress_node)
    (*progress_node)->setTotalSteps(deployed_files.size());

  for(const auto& [path, mod_id] : deployed_files)
  {
    if(progress_node)
      (*progress_node)->advance();

    const auto target_path = dest_path_ / path;
    const auto source_path = source_path_ / std::to_string(mod_id) / path;

    // Directories are recorded but are not deployed as links/copies; nothing to verify.
    if(sfs::exists(source_path) && sfs::is_directory(source_path))
      continue;

    result.total_checked++;

    // The deployed file must still be present in the target.
    if(!pu::exists(target_path))
    {
      result.missing.push_back(path);
      continue;
    }

    // Without a staged source we cannot confirm the link/contents are still correct.
    if(!modPathExists(mod_id) || !sfs::exists(source_path))
    {
      result.source_missing.push_back(path);
      continue;
    }

    if(deploy_mode_ == hard_link)
    {
      // A symlink where a hard link is expected is the wrong link type.
      if(sfs::is_symlink(target_path))
        result.not_a_link.push_back(path);
      // Hard links must still share an inode with the staged source.
      else if(!sfs::equivalent(source_path, target_path))
        result.modified.push_back(path);
    }
    else if(deploy_mode_ == sym_link)
    {
      // A real file where a symlink is expected is the wrong link type.
      if(!sfs::is_symlink(target_path))
        result.not_a_link.push_back(path);
      // Symlinks must still point at the staged source.
      else if(sfs::read_symlink(target_path) != source_path)
        result.modified.push_back(path);
    }
    else // copy
    {
      // For copies, existence (checked above) is enough unless checksums are requested.
      if(checksum)
      {
        bool differs = false;
        std::error_code ec;
        const auto target_size = sfs::file_size(target_path, ec);
        const auto source_size = sfs::file_size(source_path, ec);
        if(ec || target_size != source_size)
          differs = true;
        else
        {
          // Size matches: compare contents byte-by-byte (simple content hash equivalent).
          std::ifstream target_file(target_path, std::ios::binary);
          std::ifstream source_file(source_path, std::ios::binary);
          if(!target_file.is_open() || !source_file.is_open())
            differs = true;
          else
            differs = !std::equal(std::istreambuf_iterator<char>(target_file),
                                  std::istreambuf_iterator<char>(),
                                  std::istreambuf_iterator<char>(source_file));
        }
        if(differs)
          result.modified.push_back(path);
      }
    }
  }

  if(result.isClean())
    log_(Log::LOG_INFO,
         std::format("Verified {} files: deployment is intact", result.total_checked));
  else
    log_(Log::LOG_WARNING,
         std::format("Verification of {} files found drift: {} missing, {} modified, "
                     "{} wrong link type, {} with missing source",
                     result.total_checked,
                     result.missing.size(),
                     result.modified.size(),
                     result.not_a_link.size(),
                     result.source_missing.size()));

  return result;
}

// fork #11: virtual deployed-file tree with per-file mod origin
std::vector<Deployer::FileOrigin> Deployer::getDeployedFileOrigins(bool include_conflicts) const
{
  log_(Log::LOG_INFO,
       std::format("Deployer '{}': Building deployed file origin map{}...",
                   name_,
                   include_conflicts ? " (with conflicts)" : ""));

  const auto deployed_files = loadDeployedFiles();

  // When conflicts are requested, enumerate the staged mod directories once so that each
  // deployed file only needs a cheap existence check per candidate mod.
  std::vector<int> mod_dirs;
  if(include_conflicts && sfs::exists(source_path_))
  {
    for(const auto& dir_entry : sfs::directory_iterator(source_path_))
    {
      if(!dir_entry.is_directory())
        continue;
      const std::string dir_name = dir_entry.path().filename().string();
      if(!dir_name.empty() &&
         str::all_of(dir_name, [](unsigned char c) { return std::isdigit(c); }))
        mod_dirs.push_back(std::stoi(dir_name));
    }
    str::sort(mod_dirs);
  }

  std::vector<FileOrigin> origins;
  origins.reserve(deployed_files.size());
  for(const auto& [path, mod_id] : deployed_files)
  {
    // Directories are recorded but are not files provided by a single mod; skip them.
    if(sfs::is_directory(source_path_ / std::to_string(mod_id) / path))
      continue;

    FileOrigin origin;
    origin.path = path;
    origin.mod_id = mod_id;

    if(include_conflicts)
    {
      for(int candidate : mod_dirs)
      {
        if(candidate == mod_id)
          continue;
        if(sfs::exists(source_path_ / std::to_string(candidate) / path))
          origin.conflicting_mod_ids.push_back(candidate);
      }
    }

    origins.push_back(std::move(origin));
  }

  // deployed_files is a std::map, so origins is already ordered by path.
  return origins;
}

// fork #50: 'Problems' / health-check panel
Deployer::HealthCheckResult Deployer::runHealthCheck(
  bool checksum,
  std::optional<ProgressNode*> progress_node) const
{
  log_(Log::LOG_INFO, std::format("Deployer '{}': Running health check...", name_));

  HealthCheckResult result;
  result.deployer_name = name_;

  // The enabled load order, mirroring how computeDeploymentPlan() / deploy() select mods.
  std::vector<int> loadorder;
  for(auto const& lo : *loadorders_[current_profile_])
  {
    auto mod_info = std::static_pointer_cast<DeployerModInfo>(lo.lock());
    if(mod_info && !mod_info->isSeparator && mod_info->enabled)
      loadorder.push_back(mod_info->id);
  }
  const std::set<int> enabled_mods(loadorder.begin(), loadorder.end());

  // Broken/incorrect links and missing files: reuse the verification logic verbatim.
  result.verification = verifyDeployment(checksum, progress_node);

  // Orphaned files: recorded as deployed in .lmmfiles but owned by a mod which is no longer in
  // the enabled load order, so a deploy would not re-create them.
  const std::map<sfs::path, int> deployed_files = loadDeployedFiles();
  for(const auto& [path, mod_id] : deployed_files)
  {
    // Directory records are not deployed files; skip them.
    if(sfs::is_directory(dest_path_ / path))
      continue;
    if(!enabled_mods.contains(mod_id))
      result.orphaned.push_back(path);
  }

  // Conflicts: enumerate the files each enabled mod would provide (the same enumeration deploy()
  // uses) and record every path claimed by more than one mod. The winner is the mod deepest in
  // the load order, matching how getDeploymentSourceFilesAndModSizes resolves overwrites.
  std::map<sfs::path, std::vector<int>> providers;
  for(int mod_id : loadorder)
  {
    if(!checkModPathExistsAndMaybeLogError(mod_id))
      continue;
    const sfs::path mod_base_path = source_path_ / std::to_string(mod_id);
    for(auto const& dir_entry : sfs::recursive_directory_iterator(mod_base_path))
    {
      if(dir_entry.is_symlink() || !dir_entry.is_regular_file())
        continue;
      if(isIgnoredFile(dir_entry.path()))
        continue;
      providers[pu::getRelativePath(dir_entry.path(), mod_base_path)].push_back(mod_id);
    }
  }
  for(auto& [path, mod_ids] : providers)
  {
    if(mod_ids.size() < 2)
      continue;
    // The winner is the last enabled mod in the load order which provides this path.
    int winner = mod_ids.front();
    for(int mod_id : loadorder)
    {
      if(str::find(mod_ids, mod_id) != mod_ids.end())
        winner = mod_id;
    }
    result.conflicts.push_back({ path, winner, std::move(mod_ids) });
  }

  if(result.isHealthy())
    log_(Log::LOG_INFO, std::format("Deployer '{}': Health check found no problems", name_));
  else
    log_(Log::LOG_WARNING,
         std::format("Deployer '{}': Health check found {} problems: {} orphaned, "
                     "{} broken/incorrect links, {} conflicts",
                     name_,
                     result.numProblems(),
                     result.orphaned.size(),
                     result.numBrokenLinks(),
                     result.conflicts.size()));

  return result;
}

void Deployer::keepOrRevertFileModifications(const FileChangeChoices& changes_to_keep)
{
  if(deploy_mode_ == copy)
    return;

  for(const auto& [path, mod_id, keep_change] :
      stv::zip(changes_to_keep.paths, changes_to_keep.mod_ids, changes_to_keep.changes_to_keep))
  {
    const auto target_path = dest_path_ / path;
    const auto mod_file_path = source_path_ / std::to_string(mod_id) / path;
    if(!checkModPathExistsAndMaybeLogError(mod_id) || !pu::exists(target_path))
      continue;
    if(keep_change)
    {
      // The file currently providing the modification: either target_path itself (a real file that
      // replaced the link) or, for an intact link, the file the link resolves to.
      const bool target_is_symlink = sfs::is_symlink(target_path);
      const sfs::path provider_path =
        target_is_symlink ? sfs::read_symlink(target_path) : target_path;
      // Stage the provider into a temporary beside mod_file_path, then atomically rename it into
      // place. The original mod_file_path is only removed once the replacement is safely staged, so
      // an interrupted/failed recovery never leaves the source file missing (audit F126).
      const sfs::path staged_path =
        mod_file_path.parent_path() / (mod_file_path.filename().string() + ".limo_keep_tmp");
      std::error_code ec;
      sfs::remove(staged_path, ec);
      sfs::rename(provider_path, staged_path, ec);
      if(ec)
      {
        // Cross-filesystem or other rename failure: fall back to copy. Guard every call so a
        // failure here cannot escape and leave the source removed.
        ec.clear();
        sfs::copy(provider_path, staged_path, sfs::copy_options::overwrite_existing, ec);
        if(ec)
        {
          log_(Log::LOG_WARNING,
               std::format("Deployer '{}': failed to keep modification for '{}': {}",
                           name_,
                           path.string(),
                           ec.message()));
          sfs::remove(staged_path, ec);
          continue;
        }
        // The provider was copied, so drop the original provider file.
        sfs::remove(provider_path, ec);
      }
      // The staged copy now holds the desired content; replace mod_file_path atomically.
      sfs::remove(mod_file_path, ec);
      sfs::rename(staged_path, mod_file_path, ec);
      if(ec)
      {
        log_(Log::LOG_WARNING,
             std::format("Deployer '{}': failed to keep modification for '{}': {}",
                         name_,
                         path.string(),
                         ec.message()));
        continue;
      }
      // Remove whatever remains at target_path (the now-orphaned link or leftover file).
      sfs::remove(target_path, ec);
    }
    else
      sfs::remove(target_path);
    if(deploy_mode_ == sym_link)
      sfs::create_symlink(mod_file_path, target_path);
    else
      sfs::create_hard_link(mod_file_path, target_path);
  }
}

void Deployer::updateDeployedFilesForMod(int mod_id,
                                         std::optional<ProgressNode*> progress_node) const
{
  std::map<sfs::path, int> deployed_files = loadDeployedFiles(progress_node);
  for(const auto& [path, id] : deployed_files)
  {
    if(id != mod_id)
      continue;
    const sfs::path dest_path = dest_path_ / path;
    const sfs::path source_path = source_path_ / std::to_string(mod_id) / path;

    if(pu::exists(dest_path) && sfs::is_directory(dest_path) || !sfs::exists(source_path) ||
       sfs::is_directory(source_path))
      continue;

    sfs::remove(dest_path);
    if(deploy_mode_ == sym_link)
      sfs::create_symlink(source_path, dest_path);
    else if(deploy_mode_ == DeployMode::copy)
      sfs::copy(source_path, dest_path);
    else
      sfs::create_hard_link(source_path, dest_path);
  }
}

void Deployer::fixInvalidLinkDeployMode()
{
  if(deploy_mode_ != hard_link)
    return;

  const std::string file_name = "_lmm_write_test_file_";
  try
  {
    sfs::remove(source_path_ / file_name);
    sfs::remove(dest_path_ / file_name);

    // The source file must exist before a hard link to it can be created. Without this write
    // the link always failed, so every hard-link deployer was needlessly downgraded to symlinks.
    std::ofstream(source_path_ / file_name) << "test";
    sfs::create_hard_link(source_path_ / file_name, dest_path_ / file_name);
  }
  catch(...)
  {
    log_(Log::LOG_DEBUG,
         std::format("Deployer {} failed to create hard link. Switching to sym link.", name_));
    deploy_mode_ = sym_link;
  }
  try
  {
    sfs::remove(source_path_ / file_name);
    sfs::remove(dest_path_ / file_name);
  }
  catch(...)
  {
    log_(Log::LOG_ERROR, "Failed to write to disk. Ensure that permissions are set correctly.");
  }
}

int Deployer::getDeployPriority() const
{
  return 0;
}

bool Deployer::supportsSorting() const
{
  return true;
}

bool Deployer::supportsReordering() const
{
  return true;
}

bool Deployer::supportsModConflicts() const
{
  return true;
}

bool Deployer::supportsFileConflicts() const
{
  return true;
}

bool Deployer::supportsFileBrowsing() const
{
  return true;
}

bool Deployer::supportsExpandableItems() const
{
  return false;
}

bool Deployer::idsAreSourceReferences() const
{
  return false;
}

std::vector<std::pair<std::string, std::string>> Deployer::getModActions() const
{
  return {};
}

std::vector<std::vector<int>> Deployer::getValidModActions() const
{
  std::vector<std::vector<int>> valid_actions;
  for(int _ = 0; _ < loadorders_[current_profile_]->size(); _++)
    valid_actions.push_back({});
  return valid_actions;
}

void Deployer::applyModAction(int action, int mod_id) {}

bool Deployer::isCaseInvariant() const
{
  return false;
}

bool Deployer::getEnableUnsafeSorting() const
{
  return enable_unsafe_sorting_;
}

void Deployer::setEnableUnsafeSorting(bool enable)
{
  enable_unsafe_sorting_ = enable;
}

void Deployer::removeManagedDirFile(const sfs::path& directory) const
{
  sfs::remove(directory / managed_dir_file_name_);
}

std::vector<std::string> Deployer::getIgnoredFiles() const
{
  return ignored_files_;
}

void Deployer::setIgnoredFiles(const std::vector<std::string>& ignored_files)
{
  ignored_files_ = ignored_files;
}

bool Deployer::isIgnoredFile(const sfs::path& path) const
{
  // Lower-cases a string for case-insensitive comparison.
  const auto to_lower = [](std::string s)
  {
    str::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
  };

  // Matches a basename against a simple glob pattern where '*' matches any sequence of
  // characters. All inputs are expected to already be lower-cased.
  const auto glob_match = [](const std::string& name, const std::string& pattern)
  {
    // Iterative wildcard matching with backtracking on the most recent '*'.
    size_t n = 0, p = 0, star = std::string::npos, match = 0;
    while(n < name.size())
    {
      if(p < pattern.size() && pattern[p] == '*')
      {
        star = p++;
        match = n;
      }
      else if(p < pattern.size() && pattern[p] == name[n])
      {
        p++;
        n++;
      }
      else if(star != std::string::npos)
      {
        p = star + 1;
        n = ++match;
      }
      else
        return false;
    }
    while(p < pattern.size() && pattern[p] == '*')
      p++;
    return p == pattern.size();
  };

  const std::string name = to_lower(path.filename().string());
  if(name.empty())
    return false;
  for(const auto& pattern : ignored_files_)
  {
    const std::string lowered_pattern = to_lower(pattern);
    if(lowered_pattern.find('*') == std::string::npos)
    {
      if(name == lowered_pattern)
        return true;
    }
    else if(glob_match(name, lowered_pattern))
      return true;
  }
  return false;
}

bool Deployer::targetWasDeployedByLimo(const sfs::path& target_path,
                                       int mod_id,
                                       const sfs::path& relative_path) const
{
  if(!pu::exists(target_path) || sfs::is_directory(target_path))
    return false;
  // Copies leave no trace linking them back to the source mod, so the recorded entry is trusted.
  if(deploy_mode_ == copy)
    return true;

  const sfs::path source_path = source_path_ / std::to_string(mod_id) / relative_path;
  try
  {
    if(deploy_mode_ == sym_link)
      return sfs::is_symlink(target_path) && sfs::read_symlink(target_path) == source_path;
    // hard_link: the target is a Limo file iff it still shares an inode with the source file.
    return !sfs::is_symlink(target_path) && sfs::exists(source_path) &&
           sfs::equivalent(source_path, target_path);
  }
  catch(const sfs::filesystem_error&)
  {
    return false;
  }
}
