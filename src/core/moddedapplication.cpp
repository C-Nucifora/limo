#include "moddedapplication.h"
#include <limits>
#include "core/deployerinfo.h"
#include "cyberpunkredmod.h"
#include "cyberpunksetup.h"
#include "deployerfactory.h"
#include "installer.h"
#include "parseerror.h"
#include "pathutils.h"
#include "reversedeployer.h"
#ifdef LIMO_WITH_LOOT
#include "lootdeployer.h" // fork #212
#endif
#include "tw3mergeutil.h"
#include "tw3scriptmerge.h"
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <array>
#include <chrono> // fork #54
#include <cstdlib>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <ranges>
#include <regex>
#include <sstream>
#include <sys/wait.h>

namespace sfs = std::filesystem;
namespace str = std::ranges;
namespace pu = path_utils;


ModdedApplication::ModdedApplication(sfs::path staging_dir,
                                     std::string name,
                                     std::string command,
                                     std::filesystem::path icon_path,
                                     std::string app_version) :
  name_(name), staging_dir_(staging_dir), command_(command), icon_path_(icon_path)
{
  if(sfs::exists(staging_dir / CONFIG_FILE_NAME))
    updateState(true);
  else
  {
    addProfile({ "Default", app_version, -1 });
    updateSettings(true);
  }
  sfs::copy(staging_dir_ / CONFIG_FILE_NAME,
            staging_dir_ / ("." + CONFIG_FILE_NAME + ".bak"),
            sfs::copy_options::overwrite_existing);
}

void ModdedApplication::deployMods()
{
  std::vector<int> deployers;
  for(int i = 0; i < deployers_.size(); i++)
    deployers.push_back(i);
  deployModsFor(deployers);
}

void ModdedApplication::deployModsFor(std::vector<int> deployers)
{
  str::sort(deployers,
            [this](int depl_l, int depl_r)
            {
              return this->deployers_[depl_l]->getDeployPriority() <
                     this->deployers_[depl_r]->getDeployPriority();
            });

  const std::string rule_warnings = checkModRules();
  if(!rule_warnings.empty())
    log_(Log::LOG_WARNING, rule_warnings);

  std::vector<float> weights;
  for(int deployer : deployers)
  {
    const int num_mods = deployers_[deployer]->getNumMods();
    // Reverse deployer operations are faster than other operations
    if(deployers_[deployer]->getType() == DeployerFactory::REVERSEDEPLOYER)
      weights.push_back((int)(num_mods / 8));
    else if(deployers_[deployer]->isAutonomous() || num_mods == 0)
      weights.push_back(1);
    else
      weights.push_back(num_mods);
  }

  runHook("pre-deploy", pre_deploy_hook_);

  ProgressNode node(progress_callback_, weights);
  for(auto [i, deployer] : str::enumerate_view(deployers))
  {
    const auto mod_sizes = deployers_[deployer]->deploy(&(node.child(i)));
    if(!deployers_[deployer]->isAutonomous())
    {
      for(const auto [mod_id, mod_size] : mod_sizes)
      {
        auto mod_iter =
          str::find_if(installed_mods_, [id = mod_id](const Mod& m) { return m.id == id; });
        if(mod_iter != installed_mods_.end())
          mod_iter->size_on_disk = mod_size;
      }
    }
  }

  runHook("post-deploy", post_deploy_hook_);

  updateSettings(true);
}

void ModdedApplication::unDeployMods()
{
  std::vector<int> deployers;
  for(int i = 0; i < deployers_.size(); i++)
    deployers.push_back(i);
  unDeployModsFor(deployers);
}

void ModdedApplication::unDeployModsFor(std::vector<int> deployers)
{
  str::sort(deployers,
            [this](int depl_l, int depl_r)
            {
              return this->deployers_[depl_l]->getDeployPriority() <
                     this->deployers_[depl_r]->getDeployPriority();
            });

  std::vector<float> weights;
  for(int deployer : deployers)
  {
    const int num_mods = deployers_[deployer]->getNumMods();
    if(deployers_[deployer]->isAutonomous() || num_mods == 0)
      weights.push_back(1);
    else
      weights.push_back(num_mods);
  }

  runHook("pre-undeploy", pre_undeploy_hook_);

  ProgressNode node(progress_callback_, weights);
  for(auto [i, deployer] : str::enumerate_view(deployers))
    deployers_[deployer]->unDeploy(&(node.child(i)));

  runHook("post-undeploy", post_undeploy_hook_);

  updateSettings(true);
}

void ModdedApplication::installMod(const ImportModInfo& info)
{
  if(info.replace_mod && info.target_group_id != -1)
  {
    replaceMod(info);
    return;
  }
  ProgressNode progress_node(progress_callback_);
  if(info.target_group_id >= 0 && !info.deployers.empty())
    progress_node.addChildren({ 1.0f, 10.0f, info.deployers.size() > 1 ? 10.0f : 1.0f });
  else if(info.target_group_id >= 0 || !info.deployers.empty())
    progress_node.addChildren({ 1, 10 });
  else
    progress_node.addChildren({ 1 });
  progress_node.child(0).setTotalSteps(1);
  const int mod_id = allocateNewModId();
  last_mod_id_ = mod_id;
  const auto mod_size = Installer::install(info.current_path,
                                           staging_dir_ / std::to_string(mod_id),
                                           info.installer_flags,
                                           info.installer,
                                           info.root_level,
                                           info.files);
  const auto time_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  installed_mods_.emplace_back(mod_id,
                               info.name,
                               info.version,
                               time_now,
                               info.local_source,
                               info.remote_source,
                               time_now,
                               mod_size,
                               time_now,
                               info.remote_mod_id,
                               info.remote_file_id,
                               info.remote_type);
  installer_map_[mod_id] = info.installer;
  progress_node.child(0).advance();
  if(info.target_group_id >= 0)
  {
    if(modHasGroup(info.target_group_id))
      addModToGroup(mod_id, group_map_[info.target_group_id], &progress_node.child(1));
    else
      createGroup(mod_id, info.target_group_id, &progress_node.child(1));
  }

  for(int deployer : info.deployers)
    addModToDeployer(deployer, mod_id, true, &progress_node.child(info.target_group_id >= 0 ? 2 : 1));

  for(auto& tag : auto_tags_)
    tag.updateMods(staging_dir_, std::vector<int>{ mod_id });
  updateAutoTagMap();

  autoAddScriptExtenderTools(mod_id);

  updateSettings(true);
}

int ModdedApplication::createEmptyMod(const std::string& name, const std::string& version)
{
  const int mod_id = allocateNewModId();
  last_mod_id_ = mod_id;
  sfs::create_directories(staging_dir_ / std::to_string(mod_id));
  const auto time_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  installed_mods_.emplace_back(mod_id,
                               name,
                               version,
                               time_now,
                               "",
                               "",
                               time_now,
                               0ul,
                               time_now,
                               -1l,
                               -1l,
                               ImportModInfo::RemoteType::local);
  installer_map_[mod_id] = Installer::SIMPLEINSTALLER;

  for(auto& tag : auto_tags_)
    tag.updateMods(staging_dir_, std::vector<int>{ mod_id });
  updateAutoTagMap();

  updateSettings(true);
  return mod_id;
}

// fork #66
void ModdedApplication::updateModFromLocal(int mod_id, const sfs::path& source_archive)
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](Mod m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    throw std::runtime_error("Error: Unknown mod id: " + std::to_string(mod_id));

  const sfs::path mod_dir = staging_dir_ / std::to_string(mod_id);
  if(!pu::exists(mod_dir) || !sfs::is_directory(mod_dir))
    throw std::runtime_error("Error: Staging directory for mod " + std::to_string(mod_id) +
                             " does not exist.");

  if(!pu::exists(source_archive))
    throw std::runtime_error("Error: Source \"" + source_archive.string() + "\" does not exist.");
  // Accept the same sources a normal install accepts: a directory or a supported archive.
  if(!sfs::is_directory(source_archive) && !Installer::sourceIsArchive(source_archive))
    throw std::runtime_error("Error: Source \"" + source_archive.string() +
                             "\" is not a supported archive.");

  log_(Log::LOG_INFO,
       std::format("Updating files of mod '{}' from '{}'", iter->name, source_archive.string()));

  // Extract into a fresh temporary directory first. Only after a successful extraction do we
  // destroy the existing files, so any failure leaves the current mod intact. The temp dir lives
  // under the staging directory so that the subsequent move/rename stays on the same file system
  // (and so a directory source, whose parent differs, is copied rather than consumed by extract()).
  unsigned tmp_id = 0;
  sfs::path tmp_dir;
  do
    tmp_dir = staging_dir_ / (".lmm_update_" + std::to_string(mod_id) + "_" + std::to_string(tmp_id));
  while(pu::exists(tmp_dir) && tmp_id++ < std::numeric_limits<unsigned>::max());
  if(tmp_id == std::numeric_limits<unsigned>::max())
    throw std::runtime_error("Error: Could not create temporary directory.");

  try
  {
    Installer::extract(source_archive, tmp_dir, {});
  }
  catch(const std::exception& error)
  {
    std::error_code ec;
    sfs::remove_all(tmp_dir, ec);
    throw;
  }

  // Atomic-ish swap: move the freshly extracted files into a temporary "old" location, move the
  // new files into place, then delete the old files. The destructive removal of the old files only
  // happens after the new files are already in place.
  std::error_code ec;
  const sfs::path old_dir =
    staging_dir_ / (".lmm_update_old_" + std::to_string(mod_id) + "_" + std::to_string(tmp_id));
  sfs::rename(mod_dir, old_dir, ec);
  if(ec)
  {
    sfs::remove_all(tmp_dir, ec);
    throw std::runtime_error("Error: Could not replace mod files: " + ec.message());
  }
  sfs::rename(tmp_dir, mod_dir, ec);
  if(ec)
  {
    // Roll back to the original files so the mod is not left without a staging directory.
    std::error_code restore_ec;
    sfs::rename(old_dir, mod_dir, restore_ec);
    sfs::remove_all(tmp_dir, restore_ec);
    throw std::runtime_error("Error: Could not replace mod files: " + ec.message());
  }
  sfs::remove_all(old_dir, ec);

  // Refresh the recorded file size and local source, but keep all other metadata untouched.
  iter->local_source = source_archive;
  unsigned long mod_size = 0;
  for(const auto& dir_entry : sfs::recursive_directory_iterator(mod_dir, ec))
  {
    if(dir_entry.is_regular_file(ec))
      mod_size += dir_entry.file_size(ec);
  }
  iter->size_on_disk = mod_size;

  // Re-run the auto tags for this mod, as the changed files may match different conditions.
  for(auto& tag : auto_tags_)
    tag.updateMods(staging_dir_, std::vector<int>{ mod_id });
  updateAutoTagMap();

  // The on-disk files changed, so conflict groups for every deployer managing this mod must be
  // recomputed before the next deploy, mirroring the normal install path.
  updateDeployerGroups();

  updateSettings(true);
}

void ModdedApplication::uninstallMods(const std::vector<int>& mod_ids,
                                      const std::string& installer_type)
{
  std::vector<float> weights;
  std::vector<std::vector<int>> update_targets;
  for(int depl = 0; depl < deployers_.size(); depl++)
    update_targets.push_back({});
  for(int mod_id : mod_ids)
  {
    if(group_map_.contains(mod_id))
      removeModFromGroup(mod_id, false);
    auto mod_iter = std::find_if(
      installed_mods_.begin(), installed_mods_.end(), [mod_id](Mod m) { return m.id == mod_id; });
    if(mod_iter == installed_mods_.end())
      continue;
    for(int depl = 0; depl < deployers_.size(); depl++)
    {
      if(deployers_[depl]->isAutonomous())
        continue;
      for(int prof = 0; prof < profile_names_.size(); prof++)
      {
        deployers_[depl]->setProfile(prof);
        if(deployers_[depl]->removeMod(mod_id) &&
           str::find(update_targets[depl], prof) == update_targets[depl].end())
        {
          update_targets[depl].push_back(prof);
          weights.push_back(deployers_[depl]->getNumMods());
        }
      }
      deployers_[depl]->setProfile(current_profile_);
    }

    installed_mods_.erase(mod_iter);
    std::string installer = Installer::SIMPLEINSTALLER;
    if(installer_type == "" && installer_map_.contains(mod_id))
      installer = installer_map_[mod_id];
    Installer::uninstall(staging_dir_ / std::to_string(mod_id), installer);

    for(auto& tag : manual_tags_)
      tag.removeMod(mod_id);

    update_ignore_list_.erase(mod_id);
  }

  ProgressNode node(progress_callback_, weights);
  int i = 0;
  for(int depl = 0; depl < update_targets.size(); depl++)
  {
    for(int prof : update_targets[depl])
    {
      deployers_[depl]->setProfile(prof);
      deployers_[depl]->updateConflictGroups(&node.child(i));
      i++;
    }
    deployers_[depl]->setProfile(current_profile_);
  }

  // Drop any mod rules that reference an uninstalled mod, otherwise checkModRules() emits a
  // permanent spurious deploy-time warning for a mod that no longer exists, and stale rules can
  // later match a mod that reuses the same id (limo-app/limo: stale mod rules on uninstall).
  for(int mod_id : mod_ids)
    std::erase_if(mod_rules_,
                  [mod_id](const ModRule& r)
                  { return r.source_mod_id == mod_id || r.target_mod_id == mod_id; });

  updateSettings(true);
}

// fork #148: Merge the staged files of several mods into one target entry, then remove the
// source mods using the normal uninstall path so all metadata stays consistent.
bool ModdedApplication::mergeMods(const std::vector<int>& source_mod_ids, int target_mod_id)
{
  const auto is_installed = [this](int id)
  { return str::find_if(installed_mods_, [id](const Mod& m) { return m.id == id; }) !=
             installed_mods_.end(); };

  // Validate the target.
  if(!is_installed(target_mod_id))
  {
    log_(Log::LOG_WARNING,
         std::format("Merge aborted: target mod (id {}) does not exist.", target_mod_id));
    return false;
  }
  if(str::find(source_mod_ids, target_mod_id) == source_mod_ids.end())
  {
    log_(Log::LOG_WARNING,
         std::format("Merge aborted: target mod (id {}) is not part of the selection.",
                     target_mod_id));
    return false;
  }

  const sfs::path target_dir = staging_dir_ / std::to_string(target_mod_id);
  std::vector<int> merged_sources;
  // Copy each source's staged files into the target, in the given order (later sources may
  // overwrite earlier ones). Source files are only removed after their copy fully succeeded.
  for(int source_id : source_mod_ids)
  {
    if(source_id == target_mod_id)
      continue;
    if(!is_installed(source_id))
    {
      log_(Log::LOG_WARNING,
           std::format("Merge: skipping source mod (id {}) which does not exist.", source_id));
      continue;
    }
    const sfs::path source_dir = staging_dir_ / std::to_string(source_id);
    std::error_code ec;
    if(!sfs::exists(source_dir, ec))
    {
      log_(Log::LOG_WARNING,
           std::format("Merge: staging directory for source mod (id {}) is missing, merging "
                       "metadata only.",
                       source_id));
      merged_sources.push_back(source_id);
      continue;
    }
    sfs::copy(source_dir,
              target_dir,
              sfs::copy_options::recursive | sfs::copy_options::overwrite_existing,
              ec);
    if(ec)
    {
      log_(Log::LOG_ERROR,
           std::format("Merge: failed to copy files of source mod (id {}) into target (id {}): {}",
                       source_id,
                       target_mod_id,
                       ec.message()));
      // Do not remove a source whose copy failed; abort the merge and keep what is already done.
      if(!merged_sources.empty())
        uninstallMods(merged_sources);
      return false;
    }
    merged_sources.push_back(source_id);
  }

  // Remove the now-merged source mods through the regular uninstall path so groups, tags, notes,
  // colors, categories, deployer membership, load order and mod rules are all cleaned up.
  if(!merged_sources.empty())
    uninstallMods(merged_sources);

  log_(Log::LOG_INFO,
       std::format("Merged {} mod(s) into target mod (id {}).",
                   merged_sources.size(),
                   target_mod_id));
  return true;
}

void ModdedApplication::commitChanges()
{
  updateSettings(true);
}

void ModdedApplication::addModToDeployer(int deployer,
                                         int mod_id,
                                         bool update_conflicts,
                                         std::optional<ProgressNode*> progress_node)
{
  if(!deployers_[deployer]->isAutonomous())
  {
    const bool was_added = deployers_[deployer]->addMod(mod_id);
    ProgressNode node(progress_callback_);
    if(update_conflicts && was_added)
      deployers_[deployer]->updateConflictGroups(progress_node ? progress_node : &node);
    else if(progress_node)
    {
      (*progress_node)->setTotalSteps(1);
      (*progress_node)->advance();
    }
    splitMod(mod_id, deployer);
    updateSettings(true);
  }
}

void ModdedApplication::removeNodeFromDeployer(int deployer,
                                              void *node_ptr,
                                              bool update_conflicts,
                                              std::optional<ProgressNode*> progress_node)
{
  if(!deployers_[deployer]->isAutonomous())
  {
    const bool was_removed = deployers_[deployer]->removeNode(node_ptr);
    ProgressNode node(progress_callback_);
    if(update_conflicts && was_removed)
      deployers_[deployer]->updateConflictGroups(progress_node ? progress_node : &node);
    else if(progress_node)
    {
      (*progress_node)->setTotalSteps(1);
      (*progress_node)->advance();
    }
    updateSettings(true);
  }
}

void ModdedApplication::removeModFromDeployer(int deployer,
                                              int mod_id,
                                              bool update_conflicts,
                                              std::optional<ProgressNode*> progress_node)
{
  if(!deployers_[deployer]->isAutonomous())
  {
    const bool was_removed = deployers_[deployer]->removeMod(mod_id);
    ProgressNode node(progress_callback_);
    if(update_conflicts && was_removed)
      deployers_[deployer]->updateConflictGroups(progress_node ? progress_node : &node);
    else if(progress_node)
    {
      (*progress_node)->setTotalSteps(1);
      (*progress_node)->advance();
    }
    updateSettings(true);
  }
}

void ModdedApplication::setModStatus(int deployer, int mod_id, bool status)
{
  // Issue #65: A single logical mod can be split across multiple deployers (e.g. a Data
  // deployer and a plugin/Data-files deployer sharing the same mod id). Toggling its enabled
  // state should affect every deployer that contains the mod id, so the split mod stays
  // consistent instead of leaving part of it enabled and part disabled.
  setModStatusAcrossDeployers(deployer, mod_id, status);
}

void ModdedApplication::setModStatusAcrossDeployers(int source_deployer, int mod_id, bool status)
{
  // Always apply to the deployer the toggle originated from to preserve the previous
  // single-deployer behavior, even if (for some reason) it does not report hasMod.
  if(source_deployer >= 0 && source_deployer < static_cast<int>(deployers_.size()))
    deployers_[source_deployer]->setModStatus(mod_id, status);
  // Mirror the new status onto every other non-autonomous deployer that contains the same mod
  // id. Autonomous deployers (e.g. plugin/LOOT/reverse deployers) manage their own mod set and
  // are intentionally skipped here, mirroring uninstallMods().
  for(int depl = 0; depl < static_cast<int>(deployers_.size()); depl++)
  {
    if(depl == source_deployer)
      continue;
    if(deployers_[depl]->isAutonomous())
      continue;
    if(deployers_[depl]->hasMod(mod_id))
      deployers_[depl]->setModStatus(mod_id, status);
  }
  updateSettings(true);
}

void ModdedApplication::resizeActivePacks()
{
  // Keep one active-pack set per profile; profiles added before this feature existed (or via
  // paths that bypass addProfile) simply get an empty set here.
  if(active_packs_per_profile_.size() != profile_names_.size())
    active_packs_per_profile_.resize(profile_names_.size());
}

std::vector<std::string> ModdedApplication::getPackNames() const
{
  // A pack is just a manual tag; expose every manual tag as a candidate pack.
  std::vector<std::string> names;
  names.reserve(manual_tags_.size());
  for(const auto& tag : manual_tags_)
    names.push_back(tag.getName());
  return names;
}

std::vector<std::string> ModdedApplication::getActivePacks() const
{
  if(current_profile_ < 0 || current_profile_ >= static_cast<int>(active_packs_per_profile_.size()))
    return {};
  const auto& active = active_packs_per_profile_[current_profile_];
  return { active.begin(), active.end() };
}

bool ModdedApplication::packIsActive(const std::string& pack_name) const
{
  if(current_profile_ < 0 || current_profile_ >= static_cast<int>(active_packs_per_profile_.size()))
    return false;
  return active_packs_per_profile_[current_profile_].contains(pack_name);
}

void ModdedApplication::setPackActive(const std::string& pack_name, bool active)
{
  resizeActivePacks();
  if(current_profile_ < 0 || current_profile_ >= static_cast<int>(active_packs_per_profile_.size()))
    return;
  auto& active_set = active_packs_per_profile_[current_profile_];
  if(active)
    active_set.insert(pack_name);
  else
    active_set.erase(pack_name);
  applyActivePacks();
}

void ModdedApplication::applyActivePacks()
{
  resizeActivePacks();
  if(current_profile_ < 0 || current_profile_ >= static_cast<int>(active_packs_per_profile_.size()))
    return;
  const auto& active = active_packs_per_profile_[current_profile_];
  // With no pack active, leave the manual enabled-state untouched (so the feature is opt-in and
  // never silently disables a user's manually-curated set). Still persist the active-pack state.
  if(active.empty())
  {
    updateSettings(true);
    return;
  }
  // Union of every mod belonging to at least one active pack.
  std::set<int> enabled_ids;
  for(const auto& tag : manual_tags_)
  {
    if(!active.contains(tag.getName()))
      continue;
    for(int mod_id : tag.getMods())
      enabled_ids.insert(mod_id);
  }
  // Apply enabled = (mod in union) across all non-autonomous deployers for the current profile.
  for(const auto& mod : installed_mods_)
  {
    const bool status = enabled_ids.contains(mod.id);
    for(const auto& deployer : deployers_)
    {
      if(deployer->isAutonomous())
        continue;
      if(deployer->hasMod(mod.id))
        deployer->setModStatus(mod.id, status);
    }
  }
  updateSettings(true);
}

void ModdedApplication::addDeployer(const EditDeployerInfo& info)
{
  std::string source_dir = staging_dir_;
  const bool is_autonomous = DeployerFactory::AUTONOMOUS_DEPLOYERS.at(info.type);
  if(info.type == DeployerFactory::REVERSEDEPLOYER)
  {
    long id = 0;
    source_dir = staging_dir_ / std::format("rev_depl_{}", id);
    while(pu::exists(source_dir))
      source_dir = staging_dir_ / std::format("rev_depl_{}", ++id);
  }
  else if(is_autonomous)
    source_dir = info.source_dir;
  deployers_.push_back(DeployerFactory::makeDeployer(info.type,
                                                     source_dir,
                                                     info.target_dir,
                                                     info.name,
                                                     info.deploy_mode,
                                                     info.separate_profile_dirs,
                                                     info.update_ignore_list));
  deployers_.back()->setEnableUnsafeSorting(info.enable_unsafe_sorting);
  for(int i = 0; i < profile_names_.size(); i++)
    deployers_.back()->addProfile();
  deployers_.back()->setProfile(current_profile_);
  deployers_.back()->setLog(log_);
  if(!is_autonomous)
  {
    for(int i = 0; i < installed_mods_.size(); i++)
    {
      for(int depl = 0; depl < deployers_.size(); depl++)
      {
        if(deployers_[depl]->hasMod(installed_mods_[i].id))
          splitMod(installed_mods_[i].id, depl);
      }
    }
  }
  updateSettings(true);
}

void ModdedApplication::removeDeployer(int deployer, bool cleanup)
{
  if(deployer < 0 || deployer >= static_cast<int>(deployers_.size()))
    return;
  if(cleanup)
    deployers_[deployer]->cleanup();
  deployers_.erase(deployers_.begin() + deployer);
  updateSettings(true);
}

std::vector<std::string> ModdedApplication::getDeployerNames() const
{
  std::vector<std::string> names;
  for(const auto& deployer : deployers_)
    names.push_back(deployer->getName());
  return names;
}

// fork #49: dry-run preview — compute the deployment plan of every non-autonomous deployer
// without touching the disk.
std::vector<Deployer::DeploymentPlan> ModdedApplication::computeDeploymentPlans() const
{
  std::vector<Deployer::DeploymentPlan> plans;
  for(const auto& deployer : deployers_)
  {
    if(deployer->isAutonomous())
      continue;
    plans.push_back(deployer->computeDeploymentPlan());
  }
  return plans;
}

// fork #212: LOOT-masterlist dirty/clean info from the app's first LOOT deployer (empty when
// built without LOOT or when there is no LOOT deployer).
std::vector<PluginCleanInfoView> ModdedApplication::getPluginCleanInfo() const
{
  std::vector<PluginCleanInfoView> result;
#ifdef LIMO_WITH_LOOT
  for(const auto& deployer : deployers_)
  {
    auto* loot_deployer = dynamic_cast<LootDeployer*>(deployer.get());
    if(loot_deployer == nullptr)
      continue;
    for(const auto& info : loot_deployer->getPluginCleanInfo())
    {
      PluginCleanInfoView view;
      view.plugin = info.plugin;
      view.is_dirty = info.is_dirty;
      view.itm_count = info.itm_count;
      view.deleted_reference_count = info.deleted_reference_count;
      view.deleted_navmesh_count = info.deleted_navmesh_count;
      view.cleaning_utility = info.cleaning_utility;
      result.push_back(view);
    }
    break;
  }
#endif
  return result;
}

// fork #202: ESM/ESL flag info from the app's first plugin deployer (empty if none).
std::vector<PluginDeployer::PluginFlagInfo> ModdedApplication::getPluginFlagInfo() const
{
  for(const auto& deployer : deployers_)
  {
    auto* plugin_deployer = dynamic_cast<PluginDeployer*>(deployer.get());
    if(plugin_deployer != nullptr)
      return plugin_deployer->getPluginFlagInfo();
  }
  return {};
}

std::vector<ModInfo> ModdedApplication::getModInfo() const
{
  std::vector<ModInfo> mod_info{};
  for(const auto& mod : installed_mods_)
  {
    std::vector<std::string> deployer_names;
    std::vector<int> deployer_ids;
    std::vector<bool> statuses;
    for(int i = 0; i < deployers_.size(); i++)
    {
      if(deployers_[i]->isAutonomous())
        continue;
      auto status = deployers_[i]->getModStatus(mod.id);
      if(status)
      {
        deployer_names.push_back(deployers_[i]->getName());
        deployer_ids.push_back(i);
        statuses.push_back(*status);
      }
    }

    int group = -1;
    bool is_active = false;
    if(group_map_.contains(mod.id))
    {
      group = group_map_.at(mod.id);
      is_active = active_group_members_[group] == mod.id;
    }

    mod_info.emplace_back(
      mod,
      deployer_names,
      deployer_ids,
      statuses,
      group,
      is_active,
      manual_tag_map_.contains(mod.id) ? manual_tag_map_.at(mod.id) : std::vector<std::string>{},
      auto_tag_map_.contains(mod.id) ? auto_tag_map_.at(mod.id) : std::vector<std::string>{});
    // fork #199: carry the user-assigned highlight colour with the mod info.
    if(mod_color_map_.contains(mod.id))
      mod_info.back().color = mod_color_map_.at(mod.id);
    // fork #198: carry the user-assigned category with the mod info.
    if(mod_category_map_.contains(mod.id))
      mod_info.back().category = mod_category_map_.at(mod.id);
  }
  return mod_info;
}

std::shared_ptr<TreeItem<DeployerEntry>> ModdedApplication::getLoadorder(int deployer) const
{
  if(deployer < 0 || deployer >= (int)deployers_.size())
    throw std::runtime_error("Error: Invalid deployer index: " + std::to_string(deployer));
  return deployers_[deployer]->getLoadorder();
}

const sfs::path& ModdedApplication::getStagingDir() const
{
  return staging_dir_;
}

void ModdedApplication::setStagingDir(std::string staging_dir, bool move_existing)
{
  if(staging_dir == staging_dir_)
    return;
  if(move_existing)
  {
    const sfs::path dest_root(staging_dir);
    // Move one entry, falling back to copy+remove if rename fails (e.g. cross-device EXDEV).
    const auto move_entry = [](const sfs::path& from, const sfs::path& to)
    {
      std::error_code ec;
      sfs::rename(from, to, ec);
      if(!ec)
        return;
      // Cross-filesystem (or otherwise un-renamable): copy recursively, then remove the source.
      ec.clear();
      sfs::copy(from, to, sfs::copy_options::recursive | sfs::copy_options::overwrite_existing, ec);
      if(ec)
        throw std::runtime_error("Error: Could not move \"" + from.string() + "\" to \"" +
                                 to.string() + "\": " + ec.message());
      std::error_code remove_ec;
      sfs::remove_all(from, remove_ec);
    };

    // Track what has been moved so a mid-way failure can be rolled back, leaving the original
    // staging directory intact rather than half-migrated.
    std::vector<std::pair<sfs::path, sfs::path>> moved;
    try
    {
      for(const auto& mod : installed_mods_)
      {
        const std::string mod_dir = std::to_string(mod.id);
        const sfs::path from = staging_dir_ / mod_dir;
        const sfs::path to = dest_root / mod_dir;
        if(!pu::exists(from))
          continue;
        move_entry(from, to);
        moved.emplace_back(from, to);
      }
      const sfs::path config_from = staging_dir_ / CONFIG_FILE_NAME;
      const sfs::path config_to = dest_root / CONFIG_FILE_NAME;
      move_entry(config_from, config_to);
      moved.emplace_back(config_from, config_to);
    }
    catch(...)
    {
      // Roll back every entry already moved before re-throwing.
      for(auto it = moved.rbegin(); it != moved.rend(); ++it)
      {
        std::error_code ec;
        sfs::rename(it->second, it->first, ec);
        if(ec)
        {
          ec.clear();
          sfs::copy(it->second,
                    it->first,
                    sfs::copy_options::recursive | sfs::copy_options::overwrite_existing,
                    ec);
          std::error_code remove_ec;
          sfs::remove_all(it->second, remove_ec);
        }
      }
      throw;
    }
  }
  staging_dir_ = staging_dir;
  updateState(true);
}

const std::string& ModdedApplication::name() const
{
  return name_;
}

void ModdedApplication::setName(const std::string& newName)
{
  name_ = newName;
  updateSettings(true);
}

int ModdedApplication::getNumDeployers() const
{
  return deployers_.size();
}

const std::string& ModdedApplication::getConfigFileName() const
{
  return CONFIG_FILE_NAME;
}

void ModdedApplication::changeModName(int mod_id, const std::string& new_name)
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](Mod m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    throw std::runtime_error("Error: Unknown mod id: " + std::to_string(mod_id));
  iter->name = new_name;
  updateSettings(true);
}

void ModdedApplication::exportModArchive(int mod_id,
                                         const std::filesystem::path& target_archive) const
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](Mod m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    throw std::runtime_error("Error: Unknown mod id: " + std::to_string(mod_id));

  const sfs::path source_dir = staging_dir_ / std::to_string(mod_id);
  if(!pu::exists(source_dir) || !sfs::is_directory(source_dir))
    throw std::runtime_error("Error: Staging directory for mod " + std::to_string(mod_id) +
                             " does not exist.");

  log_(Log::LOG_INFO,
       std::format("Exporting mod '{}' to '{}'", iter->name, target_archive.string()));

  struct archive* dest = archive_write_new();
  if(dest == nullptr)
    throw std::runtime_error("Error: Could not allocate archive for export.");
  // Ensure the archive is always freed, even if an exception is thrown below.
  struct ArchiveGuard
  {
    struct archive* a;
    ~ArchiveGuard() { archive_write_free(a); }
  } guard{ dest };

  archive_write_set_format_zip(dest);
  if(archive_write_open_filename(dest, target_archive.string().c_str()) != ARCHIVE_OK)
    throw std::runtime_error("Error: Could not open archive '" + target_archive.string() +
                             "' for writing: " + archive_error_string(dest));

  std::array<char, 16384> buffer;
  for(const auto& dir_entry : sfs::recursive_directory_iterator(source_dir))
  {
    if(!dir_entry.is_regular_file())
      continue;

    const sfs::path& path = dir_entry.path();
    const std::string entry_name = sfs::relative(path, source_dir).string();

    struct archive_entry* entry = archive_entry_new();
    struct EntryGuard
    {
      struct archive_entry* e;
      ~EntryGuard() { archive_entry_free(e); }
    } entry_guard{ entry };

    archive_entry_set_pathname(entry, entry_name.c_str());
    archive_entry_set_size(entry, static_cast<la_int64_t>(sfs::file_size(path)));
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);

    if(archive_write_header(dest, entry) != ARCHIVE_OK)
      throw std::runtime_error("Error: Could not write archive header for '" + entry_name +
                               "': " + archive_error_string(dest));

    std::ifstream file(path, std::ios::binary);
    if(!file.is_open())
      throw std::runtime_error("Error: Could not open file '" + path.string() + "' for export.");
    while(file)
    {
      file.read(buffer.data(), buffer.size());
      const std::streamsize read_count = file.gcount();
      if(read_count <= 0)
        break;
      if(archive_write_data(dest, buffer.data(), static_cast<size_t>(read_count)) < 0)
        throw std::runtime_error("Error: Could not write data for '" + entry_name +
                                 "': " + archive_error_string(dest));
    }
  }

  if(archive_write_close(dest) != ARCHIVE_OK)
    throw std::runtime_error("Error: Could not finalize archive '" + target_archive.string() +
                             "': " + archive_error_string(dest));
}

std::vector<ConflictInfo> ModdedApplication::getFileConflicts(int deployer,
                                                              int mod_id,
                                                              bool show_disabled) const
{
  if(deployer < 0 || deployer >= (int)deployers_.size())
    throw std::runtime_error("Error: Invalid deployer index: " + std::to_string(deployer));
  ProgressNode node(progress_callback_);
  auto conflicts = deployers_[deployer]->getFileConflicts(mod_id, show_disabled, &node);
  if(deployers_[deployer]->isAutonomous())
    return conflicts;
  for(auto& [_, ids, names] : conflicts)
  {
    for(int id : ids)
      names.push_back(getModName(id));
  }
  return conflicts;
}

AppInfo ModdedApplication::getAppInfo() const
{
  AppInfo info;
  info.name = name_;
  info.staging_dir = staging_dir_.string();
  info.command = command_;
  info.num_mods = installed_mods_.size();
  info.app_version = (current_profile_ >= 0 && current_profile_ < static_cast<int>(app_versions_.size()))
                       ? app_versions_[current_profile_]
                       : std::string{};
  info.steam_app_id = steam_app_id_;
  for(const auto& deployer : deployers_)
  {
    info.deployers.push_back(deployer->getName());
    info.deployer_types.push_back(deployer->getType());
    info.target_dirs.push_back(deployer->getDestPath());
    info.deployer_source_dirs.push_back(deployer->getSourcePath());
    info.deployer_mods.push_back(deployer->getNumMods());
    info.deploy_modes.push_back(deployer->getDeployMode());
    info.deployer_is_case_invariant.push_back(deployer->isCaseInvariant());
  }
  info.tools = tools_;
  for(const auto& tag : manual_tags_)
    info.num_mods_per_manual_tag[tag.getName()] = tag.getNumMods();
  for(const auto& tag : auto_tags_)
  {
    info.num_mods_per_auto_tag[tag.getName()] = tag.getNumMods();
    info.auto_tags[tag.getName()] = { tag.getExpression(), tag.getConditions() };
  }
  return info;
}

void ModdedApplication::addTool(const Tool& tool)
{
  tools_.push_back(tool);
  updateSettings(true);
}

void ModdedApplication::removeTool(int tool_id)
{
  if(tool_id < tools_.size() && tool_id >= 0)
  {
    tools_.erase(tools_.begin() + tool_id);
    updateSettings(true);
  }
}

std::vector<Tool> ModdedApplication::getTools() const
{
  return tools_;
}

const std::string& ModdedApplication::command() const
{
  return command_;
}

void ModdedApplication::setCommand(const std::string& newCommand)
{
  command_ = newCommand;
  updateSettings(true);
}

void ModdedApplication::editDeployer(int deployer, const EditDeployerInfo& info)
{
  if(deployer < 0 || deployer >= (int)deployers_.size())
    throw std::runtime_error("Error: Invalid deployer index: " + std::to_string(deployer));
  if(deployers_[deployer]->getType() == info.type)
  {
    deployers_[deployer]->setName(info.name);
    deployers_[deployer]->setDestPath(info.target_dir);
    deployers_[deployer]->setDeployMode(info.deploy_mode);
    deployers_[deployer]->setEnableUnsafeSorting(info.enable_unsafe_sorting);
  }
  else
  {
    if(info.type == DeployerFactory::REVERSEDEPLOYER)
    {
      long id = 0;
      sfs::path source_dir = staging_dir_ / std::format("rev_depl_{}", id);
      while(pu::exists(source_dir))
        source_dir = staging_dir_ / std::format("rev_depl_{}", ++id);
      json_settings_["deployers"][deployer]["source_path"] = source_dir.string();
      json_settings_["deployers"][deployer]["update_profiles"] = true;
    }
    else if(DeployerFactory::AUTONOMOUS_DEPLOYERS.at(info.type))
      json_settings_["deployers"][deployer]["source_path"] = info.source_dir;
    else
      json_settings_["deployers"][deployer]["source_path"] = staging_dir_.string();
    json_settings_["deployers"][deployer]["name"] = info.name;
    json_settings_["deployers"][deployer]["dest_path"] = info.target_dir;
    json_settings_["deployers"][deployer]["type"] = info.type;
    json_settings_["deployers"][deployer]["deploy_mode"] = info.deploy_mode;
    json_settings_["deployers"][deployer]["enable_unsafe_sorting"] = info.enable_unsafe_sorting;
    updateState();
  }
  if(deployers_[deployer]->isAutonomous() && info.type != DeployerFactory::REVERSEDEPLOYER)
    deployers_[deployer]->setSourcePath(info.source_dir);
  if(info.type == DeployerFactory::REVERSEDEPLOYER)
  {
    auto depl = static_cast<ReverseDeployer*>(deployers_[deployer].get());
    depl->enableSeparateDirs(info.separate_profile_dirs);
    if(!info.update_ignore_list && depl->getNumIgnoredFiles() != 0)
      depl->deleteIgnoredFiles();
    else if(info.update_ignore_list && depl->getNumIgnoredFiles() == 0)
      depl->updateIgnoredFiles(true);
  }
  updateSettings(true);
}

std::unordered_set<int> ModdedApplication::getModConflicts(int deployer, int mod_id)
{
  if(deployer < 0 || deployer >= (int)deployers_.size())
    throw std::runtime_error("Error: Invalid deployer index: " + std::to_string(deployer));
  ProgressNode node(progress_callback_);
  return deployers_[deployer]->getModConflicts(mod_id, &node);
}

void ModdedApplication::setProfile(int profile)
{
  if(profile < 0 || profile >= profile_names_.size())
    return;
  bak_man_.setProfile(profile);
  for(const auto& deployer : deployers_)
    deployer->setProfile(profile);
  current_profile_ = profile;
  // Persist the active profile so it is remembered across restarts.
  updateSettings(true);
}

void ModdedApplication::addProfile(const EditProfileInfo& info)
{
  profile_names_.push_back(info.name);
  app_versions_.push_back(info.app_version);
  for(const auto& deployer : deployers_)
    deployer->addProfile(info.source);
  bak_man_.addProfile(info.source);
  // fork #232: a duplicated profile (source != -1) inherits the source's active packs; a
  // fresh profile starts with none.
  resizeActivePacks();
  if(info.source >= 0 && info.source < static_cast<int>(active_packs_per_profile_.size()) &&
     !active_packs_per_profile_.empty())
    active_packs_per_profile_.back() = active_packs_per_profile_[info.source];
  updateSettings(true);
}

void ModdedApplication::removeProfile(int profile)
{
  if(profile < 0 || profile >= profile_names_.size())
    return;
  for(const auto& deployer : deployers_)
    deployer->removeProfile(profile);
  profile_names_.erase(profile_names_.begin() + profile);
  app_versions_.erase(app_versions_.begin() + profile);
  // fork #232: drop the removed profile's active-pack set.
  if(profile < static_cast<int>(active_packs_per_profile_.size()))
    active_packs_per_profile_.erase(active_packs_per_profile_.begin() + profile);
  bak_man_.removeProfile(profile);
  if(profile == current_profile_)
    setProfile(0);
  else if(profile < current_profile_)
    setProfile(current_profile_ - 1);
  updateSettings(true);
}

std::vector<std::string> ModdedApplication::getProfileNames() const
{
  return profile_names_;
}

void ModdedApplication::editProfile(int profile, const EditProfileInfo& info)
{
  if(profile < 0 || profile >= profile_names_.size())
    return;
  profile_names_[profile] = info.name;
  app_versions_[profile] = info.app_version;
  updateSettings(true);
}

void ModdedApplication::editTool(int tool_id, const Tool& new_tool)
{
  if(tool_id >= 0 && tool_id < tools_.size())
    tools_[tool_id] = new_tool;
  updateSettings(true);
}

std::tuple<int, std::string, std::string> ModdedApplication::verifyDeployerDirectories()
{
  std::tuple<int, std::string, std::string> ret{ 0, "", "" };
  for(const auto& depl : deployers_)
  {
    auto [cur_code, message] = depl->verifyDirectories();
    if(cur_code)
    {
      ret = { cur_code, depl->destPath(), message };
      return ret;
    }
  }
  return ret;
}

void ModdedApplication::addModToGroup(int mod_id,
                                      int group,
                                      std::optional<ProgressNode*> progress_node)
{
  if(group < 0 || group >= groups_.size() || group_map_.contains(mod_id))
    return;
  groups_[group].push_back(mod_id);
  group_map_[mod_id] = group;
  active_group_members_[group] = mod_id;
  ProgressNode node(progress_callback_);
  updateDeployerGroups(progress_node ? progress_node : &node);
  updateSettings(true);
}

void ModdedApplication::removeModFromGroup(int mod_id,
                                           bool update_conflicts,
                                           std::optional<ProgressNode*> progress_node)
{
  if(!group_map_.contains(mod_id))
    return;
  int group = group_map_[mod_id];
  groups_[group].erase(std::find(groups_[group].begin(), groups_[group].end(), mod_id));

  if(!groups_[group].empty())
  {
    active_group_members_[group] = groups_[group][0];
    std::vector<std::vector<int>> update_targets;
    std::vector<float> weights;
    for(int depl = 0; depl < deployers_.size(); depl++)
    {
      update_targets.push_back({});
      if(deployers_[depl]->isAutonomous())
        continue;
      for(int prof = 0; prof < profile_names_.size(); prof++)
      {
        deployers_[depl]->setProfile(prof);
        auto loadorder = deployers_[depl]->getLoadorder()->getTraversal();
        auto iter = str::find_if(
          loadorder, [mod_id](const auto& entry) { return entry.lock()->getData()->id == mod_id; });
        if(iter != loadorder.end() && !iter->lock()->getData()->isSeparator)
        {
          const int new_member_id = active_group_members_[group];
          const bool enabled =
            static_pointer_cast<DeployerModInfo>(iter->lock()->getData())->enabled;
          deployers_[depl]->addMod(new_member_id, enabled, false);
          // addMod restructures the load-order tree, invalidating the `loadorder` snapshot taken
          // above. The previous code swapped stale pointers (loadorder.back()/iter) and
          // segfaulted; re-fetch the tree and swap the live nodes instead, placing the new group
          // member where the removed mod was.
          auto fresh_loadorder = deployers_[depl]->getLoadorder()->getTraversal();
          auto new_member_iter =
            str::find_if(fresh_loadorder,
                         [new_member_id](const auto& entry)
                         { return entry.lock()->getData()->id == new_member_id; });
          auto removed_iter =
            str::find_if(fresh_loadorder,
                         [mod_id](const auto& entry)
                         { return entry.lock()->getData()->id == mod_id; });
          if(new_member_iter != fresh_loadorder.end() && removed_iter != fresh_loadorder.end())
            deployers_[depl]->swapNodes(new_member_iter->lock(), removed_iter->lock());
          update_targets[depl].push_back(prof);
          weights.push_back(fresh_loadorder.size());
        }
      }
      deployers_[depl]->setProfile(current_profile_);
    }

    ProgressNode node = progress_node ? **progress_node : ProgressNode(progress_callback_);
    if(!update_conflicts)
    {
      node.setTotalSteps(1);
      node.advance();
    }
    else
    {
      node.addChildren(weights);
      int i = 0;
      for(int depl = 0; depl < update_targets.size(); depl++)
      {
        for(int prof : update_targets[depl])
        {
          deployers_[depl]->setProfile(prof);
          deployers_[depl]->updateConflictGroups(&node.child(i));
          i++;
        }
        deployers_[depl]->setProfile(current_profile_);
      }
    }
  }

  if(groups_[group].size() == 1)
    group_map_.erase(groups_[group][0]);
  if(groups_[group].size() < 2)
    // Erase the now-empty/singleton group from every per-group parallel vector in lockstep.
    eraseGroup(group);
  group_map_.erase(mod_id);
  updateSettings(true);
}

void ModdedApplication::createGroup(int first_mod_id,
                                    int second_mod_id,
                                    std::optional<ProgressNode*> progress_node)
{
  if(group_map_.contains(first_mod_id))
  {
    addModToGroup(second_mod_id, group_map_[first_mod_id]);
    return;
  }
  if(group_map_.contains(second_mod_id))
  {
    addModToGroup(first_mod_id, group_map_[second_mod_id]);
    return;
  }
  groups_.push_back({ first_mod_id, second_mod_id });
  int group = groups_.size() - 1;
  group_map_[first_mod_id] = group;
  group_map_[second_mod_id] = group;
  active_group_members_.push_back(first_mod_id);
  group_names_.push_back("");
  group_notes_.push_back("");
  ProgressNode node(progress_callback_);
  updateDeployerGroups(progress_node ? progress_node : &node);
  updateSettings(true);
}

void ModdedApplication::changeActiveGroupMember(int group,
                                                int mod_id,
                                                std::optional<ProgressNode*> progress_node)
{
  if(group < 0 || group >= groups_.size() ||
     std::find(groups_[group].begin(), groups_[group].end(), mod_id) == groups_[group].end())
    return;
  active_group_members_[group] = mod_id;
  ProgressNode node(progress_callback_);
  updateDeployerGroups(progress_node ? progress_node : &node);
  updateSettings(true);
}

void ModdedApplication::changeModVersion(int mod_id, const std::string& new_version)
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](Mod m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    throw std::runtime_error("Error: Unknown mod id: " + std::to_string(mod_id));
  iter->version = new_version;
  updateSettings(true);
}

int ModdedApplication::getNumGroups()
{
  return groups_.size();
}

std::string ModdedApplication::getGroupName(int group) const
{
  if(group < 0 || group >= (int)group_names_.size())
    return "";
  return group_names_[group];
}

void ModdedApplication::setGroupName(int group, const std::string& name)
{
  if(group < 0 || group >= (int)group_names_.size())
    return;
  group_names_[group] = name;
  updateSettings(true);
}

std::string ModdedApplication::getGroupNotes(int group) const
{
  if(group < 0 || group >= (int)group_notes_.size())
    return "";
  return group_notes_[group];
}

void ModdedApplication::setGroupNotes(int group, const std::string& notes)
{
  if(group < 0 || group >= (int)group_notes_.size())
    return;
  group_notes_[group] = notes;
  updateSettings(true);
}

std::vector<std::pair<std::string, std::string>> ModdedApplication::getGroupMetadata() const
{
  std::vector<std::pair<std::string, std::string>> result;
  result.reserve(groups_.size());
  for(int i = 0; i < (int)groups_.size(); i++)
    result.emplace_back(i < (int)group_names_.size() ? group_names_[i] : "",
                        i < (int)group_notes_.size() ? group_notes_[i] : "");
  return result;
}

std::vector<int> ModdedApplication::getGroupMembers(int group) const
{
  if(group < 0 || group >= (int)groups_.size())
    return {};
  return groups_[group];
}

int ModdedApplication::getActiveGroupMember(int group) const
{
  if(group < 0 || group >= (int)active_group_members_.size())
    return -1;
  return active_group_members_[group];
}

bool ModdedApplication::modHasGroup(int mod_id)
{
  return group_map_.contains(mod_id);
}

int ModdedApplication::getModGroup(int mod_id)
{
  if(!group_map_.contains(mod_id))
    return -1;
  return group_map_[mod_id];
}

void ModdedApplication::sortModsByConflicts(int deployer)
{
  if(deployer < 0 || deployer >= (int)deployers_.size())
    throw std::runtime_error("Error: Invalid deployer index: " + std::to_string(deployer));
  ProgressNode node(progress_callback_);
  deployers_[deployer]->sortModsByConflicts(&node);
  updateSettings(true);
}

std::vector<std::vector<int>> ModdedApplication::getConflictGroups(int deployer)
{
  if(deployer < 0 || deployer >= (int)deployers_.size())
    throw std::runtime_error("Error: Invalid deployer index: " + std::to_string(deployer));
  return deployers_[deployer]->getConflictGroups();
}

void ModdedApplication::updateModDeployers(const std::vector<int>& mod_ids,
                                           const std::vector<bool>& deployers)
{
  std::vector<float> weights;
  for(const auto& depl : deployers_)
    weights.push_back(depl->isAutonomous() ? 1 : depl->getNumMods());
  ProgressNode node(progress_callback_, weights);
  std::optional<ProgressNode*> dummy_node{};
  for(int i = 0; i < mod_ids.size(); i++)
  {
    const int mod_id = mod_ids[i];
    const bool is_last_mod = i == (mod_ids.size() - 1);
    for(int depl = 0; depl < deployers.size(); depl++)
    {
      if(deployers_[depl]->isAutonomous())
        continue;
      if(deployers[depl])
        addModToDeployer(depl, mod_id, is_last_mod, is_last_mod ? &node.child(depl) : dummy_node);
      else
        removeModFromDeployer(
          depl, mod_id, is_last_mod, is_last_mod ? &node.child(depl) : dummy_node);
    }
  }
}

int ModdedApplication::verifyStagingDir(sfs::path staging_dir)
{
  try
  {
    Json::Value val;
    std::ifstream file(staging_dir / CONFIG_FILE_NAME, std::fstream::binary);
    if(file.is_open())
      file >> val;
    file.close();
  }
  catch(std::ios_base::failure& f)
  {
    return 1;
  }
  catch(Json::Exception& e)
  {
    return 2;
  }
  return 0;
}

DeployerInfo ModdedApplication::getDeployerInfo(int deployer)
{
  auto root = std::make_shared<TreeItem<DeployerEntry>>(std::make_shared<DeployerEntry>(true, "Root"));
  if(!(deployers_[deployer]->isAutonomous()))
  {
    std::map<std::string, int> mods_per_tag;
    for(const auto& tag : manual_tags_)
      mods_per_tag[tag.getName()] = tag.getNumMods();

    auto loadorder = deployers_[deployer]->getLoadorder();
    std::vector<std::string> mod_names;
    mod_names.reserve(loadorder->size());
    for(auto& entry_weak : loadorder->getTraversalItems())
    {
      auto entry = static_pointer_cast<DeployerModInfo>(entry_weak.lock());
      if (entry->isSeparator) continue;
      auto mod_iter =
        std::ranges::find_if(installed_mods_, [&entry](auto& mod) { return mod.id == entry->id; });
      if(mod_iter == installed_mods_.end()) // load order references an uninstalled mod (desync)
        continue;
      auto mod_name = mod_iter->name;
      entry->name = mod_name;
      mod_names.push_back(mod_name);
      if(manual_tag_map_.contains(entry->id))
      {
        auto map = manual_tag_map_.at(entry->id);
        entry->manual_tags.insert(entry->manual_tags.end(),
                      map.begin(), map.end());
        std::sort(entry->manual_tags.begin(), entry->manual_tags.end());
        auto last = std::unique(entry->manual_tags.begin(), entry->manual_tags.end());
        entry->manual_tags.erase(last, entry->manual_tags.end());
      }

      if(auto_tag_map_.contains(entry->id)) {
        auto map = auto_tag_map_.at(entry->id);
        entry->auto_tags.insert(entry->auto_tags.end(),
                            map.begin(), map.end());
        std::sort(entry->auto_tags.begin(), entry->auto_tags.end());
        auto last = std::unique(entry->auto_tags.begin(), entry->auto_tags.end());
        entry->auto_tags.erase(last, entry->auto_tags.end());
      }
    }
    for(const auto& tag : auto_tags_)
    {
      if(mods_per_tag.contains(tag.getName()))
        mods_per_tag[tag.getName()] += tag.getNumMods();
      else
        mods_per_tag[tag.getName()] = tag.getNumMods();
    }
    return {
             deployers_[deployer]->getConflictGroups(),
             false,
             mods_per_tag,
             loadorder,
             false,
             false,
             deployers_[deployer]->supportsSorting(),
             deployers_[deployer]->supportsReordering(),
             deployers_[deployer]->supportsModConflicts(),
             deployers_[deployer]->supportsFileConflicts(),
             deployers_[deployer]->supportsFileBrowsing(),
             deployers_[deployer]->supportsExpandableItems(),
             deployers_[deployer]->getType(),
             deployers_[deployer]->idsAreSourceReferences(),
             {},
             deployers_[deployer]->getModActions(),
             deployers_[deployer]->getValidModActions(),
             deployers_[deployer]->getEnableUnsafeSorting() };
  }
  else
  {
    auto loadorder = *deployers_[deployer]->getLoadorder();
    std::vector<std::string> mod_names;
    if(deployers_[deployer]->idsAreSourceReferences())
    {
      mod_names.reserve(loadorder.size());
      auto names = deployers_[deployer]->getModNames();
      for(int i = 0; i < loadorder.size(); i++)
      {
        int id = loadorder[i]->getData()->id;
        auto mod_info = static_pointer_cast<DeployerModInfo>(loadorder[i]->getData());
        std::string mod_name;
        if(id == -1)
        {
          mod_name = "Vanilla";
          mod_names.push_back(mod_name);
          auto item = make_shared<DeployerModInfo>(false, names[i], mod_name, mod_info->id, mod_info->enabled);
          root->emplace_back(item);
          continue;
        }
        auto iter =
          std::ranges::find_if(installed_mods_, [id = id](auto& mod) { return mod.id == id; });
        if(iter == installed_mods_.end())
          mod_name = "Vanilla";
        else
          mod_name = iter->name;
        mod_names.push_back(mod_name);
        auto item = make_shared<DeployerModInfo>(false, names[i], mod_name, mod_info->id, mod_info->enabled);
        root->emplace_back(item);
      }
    }
    bool separate_dirs = false;
    bool has_ignored_files = false;
    if(deployers_[deployer]->getType() == DeployerFactory::REVERSEDEPLOYER)
    {
      auto depl = static_cast<ReverseDeployer*>(deployers_[deployer].get());
      separate_dirs = depl->usesSeparateDirs();
      has_ignored_files = depl->getNumIgnoredFiles() != 0;
    }
    return {
             deployers_[deployer]->getConflictGroups(),
             true,
             {},
             std::move(root),
             separate_dirs,
             has_ignored_files,
             deployers_[deployer]->supportsSorting(),
             deployers_[deployer]->supportsReordering(),
             deployers_[deployer]->supportsModConflicts(),
             deployers_[deployer]->supportsFileConflicts(),
             deployers_[deployer]->supportsFileBrowsing(),
             deployers_[deployer]->supportsExpandableItems(),
             deployers_[deployer]->getType(),
             deployers_[deployer]->idsAreSourceReferences(),
             mod_names,
             deployers_[deployer]->getModActions(),
             deployers_[deployer]->getValidModActions(),
             deployers_[deployer]->getEnableUnsafeSorting() };
  }
}

void ModdedApplication::setLog(const std::function<void(Log::LogLevel, const std::string&)>& newLog)
{
  log_ = newLog;
  for(auto& deployer : deployers_)
    deployer->setLog(newLog);
}

std::string ModdedApplication::getPreDeployHook() const
{
  return pre_deploy_hook_;
}

std::string ModdedApplication::getPostDeployHook() const
{
  return post_deploy_hook_;
}

std::string ModdedApplication::getPreUnDeployHook() const
{
  return pre_undeploy_hook_;
}

std::string ModdedApplication::getPostUnDeployHook() const
{
  return post_undeploy_hook_;
}

void ModdedApplication::setDeployHooks(const std::string& pre_deploy,
                                       const std::string& post_deploy,
                                       const std::string& pre_undeploy,
                                       const std::string& post_undeploy)
{
  pre_deploy_hook_ = pre_deploy;
  post_deploy_hook_ = post_deploy;
  pre_undeploy_hook_ = pre_undeploy;
  post_undeploy_hook_ = post_undeploy;
  updateSettings(true);
}

int ModdedApplication::allocateNewModId() const
{
  int mod_id = 0;
  // Compare explicitly on Mod::id rather than relying on Mod::operator<.
  for(const Mod& m : installed_mods_)
  {
    if(m.id >= mod_id)
      mod_id = m.id + 1;
  }
  while(pu::exists(staging_dir_ / std::to_string(mod_id)) &&
        mod_id < std::numeric_limits<int>().max())
    mod_id++;
  if(mod_id == std::numeric_limits<int>().max())
    throw std::runtime_error("Error: Could not generate new mod id.");
  return mod_id;
}

void ModdedApplication::eraseGroup(int group)
{
  if(group < 0 || group >= (int)groups_.size())
    return;
  // Keep all per-group parallel vectors strictly in lockstep; only erase from a vector if the
  // index is in range (older configs may have shorter name/note vectors).
  groups_.erase(groups_.begin() + group);
  if(group < (int)active_group_members_.size())
    active_group_members_.erase(active_group_members_.begin() + group);
  if(group < (int)group_names_.size())
    group_names_.erase(group_names_.begin() + group);
  if(group < (int)group_notes_.size())
    group_notes_.erase(group_notes_.begin() + group);
  for(auto& pair : group_map_)
  {
    if(pair.second > group)
      pair.second--;
  }
}

Json::Value ModdedApplication::filterLoadorderForInstalledMods(const Json::Value& loadorder,
                                                               const std::string& context) const
{
  // Drop any saved load-order entries referencing mods that are not installed here, so
  // setLoadorder never produces dangling ids. Separators (entries without a "status") are kept.
  const auto is_installed = [this](int mod_id)
  {
    return std::find_if(installed_mods_.begin(),
                        installed_mods_.end(),
                        [mod_id](const Mod& m) { return m.id == mod_id; }) != installed_mods_.end();
  };
  std::function<Json::Value(const Json::Value&)> filter_node = [&](const Json::Value& node)
  {
    Json::Value out = node;
    out.removeMember("children");
    if(node.isMember("children"))
    {
      out["children"] = Json::Value(Json::arrayValue);
      for(const Json::Value& child : node["children"])
      {
        if(child.isMember("status"))
        {
          const int mod_id = child["id"].asInt();
          if(!is_installed(mod_id))
          {
            log_(Log::LOG_WARNING,
                 std::format("Skipping unknown mod id {} while {}.", mod_id, context));
            continue;
          }
        }
        out["children"].append(filter_node(child));
      }
    }
    return out;
  };

  Json::Value filtered;
  filtered["children"] = Json::Value(Json::arrayValue);
  for(const Json::Value& child : loadorder["children"])
  {
    if(child.isMember("status"))
    {
      const int mod_id = child["id"].asInt();
      if(!is_installed(mod_id))
      {
        log_(Log::LOG_WARNING,
             std::format("Skipping unknown mod id {} while {}.", mod_id, context));
        continue;
      }
    }
    filtered["children"].append(filter_node(child));
  }
  return filtered;
}

int ModdedApplication::runHook(const std::string& hook_name, const std::string& command) const
{
  if(command.empty())
    return 0;

  // The hook string is treated as a complete command line authored by the user
  // (just like a Tool command overwrite, see issue #32) and is handed to the
  // shell verbatim. It is never re-escaped or wrapped beyond what the user typed.
  log_(Log::LOG_INFO, std::format("Running {} hook: {}", hook_name, command));
  const int status = std::system(command.c_str());
  const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
  if(exit_code != 0)
    // Policy: log a warning and continue. Deployment is not aborted because a
    // failing hook (e.g. an optional notification) should not block mod changes.
    log_(Log::LOG_WARNING,
         std::format("{} hook exited with code {} (continuing).", hook_name, exit_code));
  else
    log_(Log::LOG_INFO, std::format("{} hook finished successfully.", hook_name));
  return exit_code;
}

void ModdedApplication::addBackupTarget(const sfs::path& path,
                                        const std::string& name,
                                        const std::vector<std::string>& backup_names)
{
  bak_man_.addTarget(path, name, backup_names);
  updateSettings(true);
}

void ModdedApplication::removeBackupTarget(int target_id)
{
  if(target_id < 0 || target_id >= bak_man_.getNumTargets())
    return;
  bak_man_.removeTarget(target_id);
  updateSettings(true);
}

void ModdedApplication::removeAllBackupTargets()
{
  // Iterate in reverse: removeBackupTarget erases the target, so forward iteration would shift
  // the remaining targets and skip every other one.
  for(int target = bak_man_.getNumTargets() - 1; target >= 0; target--)
    removeBackupTarget(target);
}

void ModdedApplication::addBackup(int target_id, const std::string& name, int source)
{
  if(target_id < 0 || target_id >= bak_man_.getNumTargets())
    return;
  bak_man_.addBackup(target_id, name, source);
}

void ModdedApplication::removeBackup(int target_id, int backup_id)
{
  if(target_id < 0 || target_id >= bak_man_.getNumTargets() || backup_id < 0 ||
     backup_id >= bak_man_.getNumBackups(target_id))
    return;
  bak_man_.removeBackup(target_id, backup_id);
}

void ModdedApplication::setActiveBackup(int target_id, int backup_id)
{
  if(target_id < 0 || target_id >= bak_man_.getNumTargets() || backup_id < 0 ||
     backup_id >= bak_man_.getNumBackups(target_id))
    return;
  bak_man_.setActiveBackup(target_id, backup_id);
}

std::vector<BackupTarget> ModdedApplication::getBackupTargets() const
{
  return bak_man_.getTargets();
}

void ModdedApplication::setBackupName(int target_id, int backup_id, const std::string& name)
{
  if(target_id < 0 || target_id >= bak_man_.getNumTargets() || backup_id < 0 ||
     backup_id >= bak_man_.getNumBackups(target_id))
    return;
  bak_man_.setBackupName(target_id, backup_id, name);
}

void ModdedApplication::setBackupTargetName(int target_id, const std::string& name)
{
  if(target_id < 0 || target_id >= bak_man_.getNumTargets())
    return;
  bak_man_.setBackupTargetName(target_id, name);
}

void ModdedApplication::overwriteBackup(int target_id, int source_backup, int dest_backup)
{
  if(target_id < 0 || target_id >= bak_man_.getNumTargets())
    return;
  bak_man_.overwriteBackup(target_id, source_backup, dest_backup);
}

void ModdedApplication::cleanupFailedInstallation()
{
  Installer::cleanupFailedInstallation(staging_dir_, last_mod_id_);
  auto iter = std::find_if(installed_mods_.begin(),
                           installed_mods_.end(),
                           [this](const Mod& m) { return m.id == this->last_mod_id_; });
  if(iter != installed_mods_.end())
    uninstallMods({ last_mod_id_ });
  last_mod_id_ = -1;
}

void ModdedApplication::setProgressCallback(const std::function<void(float)>& progress_callback)
{
  progress_callback_ = progress_callback;
}

void ModdedApplication::uninstallGroupMembers(const std::vector<int>& mod_ids)
{
  std::vector<int> uninstall_targets;
  for(int active_id : mod_ids)
  {
    if(!group_map_.contains(active_id))
      continue;
    for(int mod_id : groups_[group_map_[active_id]])
    {
      if(mod_id != active_id)
        uninstall_targets.push_back(mod_id);
    }
  }
  uninstallMods(uninstall_targets);
}

void ModdedApplication::addManualTag(const std::string& tag_name)
{
  if(str::find(manual_tags_, tag_name) != manual_tags_.end())
    throw std::runtime_error(
      std::format("Error: A tag with the name '{}' already exists.", tag_name));
  manual_tags_.emplace_back(tag_name);
  updateSettings(true);
}

void ModdedApplication::removeManualTag(const std::string& tag_name, bool update_map)
{
  auto iter = str::find(manual_tags_, tag_name);
  if(iter != manual_tags_.end())
    manual_tags_.erase(iter);
  // fork #232: a removed tag can no longer be an active pack in any profile.
  for(auto& active : active_packs_per_profile_)
    active.erase(tag_name);
  if(update_map)
    updateManualTagMap();
  updateSettings(true);
}

void ModdedApplication::changeManualTagName(const std::string& old_name,
                                            const std::string& new_name,
                                            bool update_map)
{
  auto old_iter = str::find(manual_tags_, old_name);
  if(old_iter == manual_tags_.end())
    return;
  auto new_iter = str::find(manual_tags_, new_name);
  if(new_iter != manual_tags_.end())
    throw std::runtime_error(
      std::format("Error: Cannot rename tag '{}', because a tag with the name '{}' already exists.",
                  old_name,
                  new_name));
  old_iter->setName(new_name);
  // fork #232: carry an active pack's name across the rename in every profile.
  for(auto& active : active_packs_per_profile_)
  {
    if(active.erase(old_name) > 0)
      active.insert(new_name);
  }
  if(update_map)
    updateManualTagMap();
  updateSettings(true);
}

void ModdedApplication::addTagsToMods(const std::vector<std::string>& tag_names,
                                      const std::vector<int>& mod_ids)
{
  for(const auto& tag_name : tag_names)
  {
    auto tag = str::find(manual_tags_, tag_name);
    if(tag == manual_tags_.end())
      return;
    for(int mod : mod_ids)
      tag->addMod(mod);
  }
  updateManualTagMap();
  updateSettings(true);
}

void ModdedApplication::removeTagsFromMods(const std::vector<std::string>& tag_names,
                                           const std::vector<int>& mod_ids)
{
  for(const auto& tag_name : tag_names)
  {
    auto tag = str::find(manual_tags_, tag_name);
    if(tag == manual_tags_.end())
      return;
    for(int mod : mod_ids)
      tag->removeMod(mod);
  }
  updateManualTagMap();
  updateSettings(true);
}

void ModdedApplication::setTagsForMods(const std::vector<std::string>& tag_names,
                                       const std::vector<int> mod_ids)
{
  for(auto& tag : manual_tags_)
  {
    if(str::find(tag_names, tag) != tag_names.end())
    {
      for(int mod : mod_ids)
        tag.addMod(mod);
    }
    else
    {
      for(int mod : mod_ids)
        tag.removeMod(mod);
    }
  }
  updateManualTagMap();
  updateSettings(true);
}

void ModdedApplication::editManualTags(const std::vector<EditManualTagAction>& actions)
{
  auto old_tags = manual_tags_;
  try
  {
    for(const auto& action : actions)
    {
      if(action.getType() == EditManualTagAction::ActionType::add)
        addManualTag(action.getName());
      else if(action.getType() == EditManualTagAction::ActionType::remove)
        removeManualTag(action.getName(), false);
      else if(action.getType() == EditManualTagAction::ActionType::rename)
        changeManualTagName(action.getName(), action.getNewName(), false);
    }
  }
  catch(std::runtime_error& e)
  {
    manual_tags_ = old_tags;
    throw e;
  }
  updateManualTagMap();
  updateSettings(true);
}

void ModdedApplication::addAutoTag(const std::string& tag_name,
                                   const std::string& expression,
                                   const std::vector<TagCondition>& conditions,
                                   bool update)
{
  if(std::find(auto_tags_.begin(), auto_tags_.end(), tag_name) != auto_tags_.end())
    throw std::runtime_error(
      std::format("Error: A tag with the name '{}' already exists.", tag_name));

  auto_tags_.emplace_back(tag_name, expression, conditions);
  auto select_id = [](const auto& mod) { return mod.id; };
  if(expression != "")
    auto_tags_.back().reapplyMods(staging_dir_, str::transform_view(installed_mods_, select_id));
  if(update)
  {
    updateAutoTagMap();
    updateSettings(true);
  }
}

void ModdedApplication::addAutoTag(const Json::Value& json_tag, bool update)
{
  if(std::find(auto_tags_.begin(), auto_tags_.end(), json_tag["name"].asString()) !=
     auto_tags_.end())
    throw std::runtime_error(
      std::format("Error: A tag with the name '{}' already exists.", json_tag["name"].asString()));

  auto_tags_.emplace_back(json_tag);
  auto select_id = [](const auto& mod) { return mod.id; };
  if(json_tag["expression"].asString() != "")
    auto_tags_.back().reapplyMods(staging_dir_, str::transform_view(installed_mods_, select_id));
  if(update)
  {
    updateAutoTagMap();
    updateSettings(true);
  }
}

void ModdedApplication::removeAutoTag(const std::string& tag_name, bool update)
{
  auto iter = std::find(auto_tags_.begin(), auto_tags_.end(), tag_name);
  if(iter == auto_tags_.end())
    return;
  auto_tags_.erase(iter);
  if(update)
  {
    updateAutoTagMap();
    updateSettings(true);
  }
}

void ModdedApplication::renameAutoTag(const std::string& old_name,
                                      const std::string& new_name,
                                      bool update)
{
  auto iter = std::find(auto_tags_.begin(), auto_tags_.end(), old_name);
  if(iter == auto_tags_.end())
    return;
  if(std::find(auto_tags_.begin(), auto_tags_.end(), new_name) != auto_tags_.end())
    throw std::runtime_error(
      std::format("Error: Cannot rename tag '{}', because a tag with the name '{}' already exists.",
                  old_name,
                  new_name));

  iter->setName(new_name);
  if(update)
  {
    updateAutoTagMap();
    updateSettings(true);
  }
}

void ModdedApplication::changeAutoTagEvaluator(const std::string& tag_name,
                                               const std::string& expression,
                                               const std::vector<TagCondition>& conditions,
                                               bool update)
{
  auto iter = std::find(auto_tags_.begin(), auto_tags_.end(), tag_name);
  if(iter == auto_tags_.end())
    return;

  iter->setEvaluator(expression, conditions);
  auto select_id = [](const auto& mod) { return mod.id; };
  if(update)
  {
    iter->reapplyMods(staging_dir_, str::transform_view(installed_mods_, select_id));
    updateAutoTagMap();
    updateSettings(true);
  }
}

void ModdedApplication::editAutoTags(const std::vector<EditAutoTagAction>& actions)
{
  auto old_tags = auto_tags_;
  try
  {
    std::vector<std::string> reapply_targets;
    for(const auto& action : actions)
    {
      if(action.getType() == EditAutoTagAction::ActionType::add)
        addAutoTag(action.getName(), action.getExpression(), action.getConditions(), false);
      else if(action.getType() == EditAutoTagAction::ActionType::remove)
        removeAutoTag(action.getName(), false);
      else if(action.getType() == EditAutoTagAction::ActionType::rename)
        renameAutoTag(action.getName(), action.getNewName(), false);
      else if(action.getType() == EditAutoTagAction::ActionType::change_evaluator)
      {
        changeAutoTagEvaluator(
          action.getName(), action.getExpression(), action.getConditions(), false);
        reapply_targets.push_back(action.getName());
      }
    }
    if(!reapply_targets.empty())
    {
      log_(Log::LOG_INFO, "Reapplying auto tags with edited conditions to all mods...");
      ProgressNode node(progress_callback_);
      node.addChildren({ 1.0f, std::min(8.0f, (float)reapply_targets.size()) });
      node.child(0).setTotalSteps(installed_mods_.size());
      std::vector<float> weights;
      for(const auto& tag : reapply_targets)
      {
        auto iter = std::find(auto_tags_.begin(), auto_tags_.end(), tag);
        if(iter != auto_tags_.end())
          weights.push_back(iter->getNumConditions());
      }
      node.child(1).addChildren(weights);
      for(int i = 0; i < weights.size(); i++)
        node.child(1).child(i).setTotalSteps(installed_mods_.size());

      auto select_id = [](const auto& mod) { return mod.id; };
      auto mods = str::transform_view(installed_mods_, select_id);
      const auto files = AutoTag::readModFiles(staging_dir_, mods, &node.child(0));
      for(int i = 0; i < reapply_targets.size(); i++)
      {
        auto iter = std::find(auto_tags_.begin(), auto_tags_.end(), reapply_targets[i]);
        if(iter != auto_tags_.end())
          iter->reapplyMods(files, mods, &node.child(1).child(i));
      }
    }
  }
  catch(std::runtime_error& e)
  {
    auto_tags_ = old_tags;
    throw e;
  }
  updateAutoTagMap();
  updateSettings(true);
}

void ModdedApplication::reapplyAutoTags()
{
  log_(Log::LOG_INFO, "Reapplying auto tags to all mods...");
  ProgressNode node(progress_callback_);
  node.addChildren({ 1.0f, 8.0f });
  node.child(0).setTotalSteps(installed_mods_.size());
  std::vector<float> weights;
  for(auto& tag : auto_tags_)
    weights.push_back(tag.getNumConditions());
  node.child(1).addChildren(weights);
  for(int i = 0; i < weights.size(); i++)
    node.child(1).child(i).setTotalSteps(installed_mods_.size());
  auto select_id = [](const auto& mod) { return mod.id; };
  auto mods = str::transform_view(installed_mods_, select_id);
  const auto files = AutoTag::readModFiles(staging_dir_, mods, &node.child(0));
  for(int i = 0; i < auto_tags_.size(); i++)
    auto_tags_[i].reapplyMods(files, mods, &node.child(1).child(i));
  updateAutoTagMap();
  updateSettings(true);
}

void ModdedApplication::updateAutoTags(const std::vector<int> mod_ids)
{
  log_(Log::LOG_INFO, std::format("Reapplying auto tags to {} mods...", mod_ids.size()));
  ProgressNode node(progress_callback_);
  node.addChildren(
    { 1.0f, std::max(1.0f, 8.0f * (float)mod_ids.size() / (float)installed_mods_.size()) });
  node.child(0).setTotalSteps(mod_ids.size());
  std::vector<float> weights;
  for(auto& tag : auto_tags_)
    weights.push_back(tag.getNumConditions());
  node.child(1).addChildren(weights);
  for(int i = 0; i < weights.size(); i++)
    node.child(1).child(i).setTotalSteps(mod_ids.size());
  const auto files = AutoTag::readModFiles(staging_dir_, mod_ids, &node.child(0));
  for(int i = 0; i < auto_tags_.size(); i++)
    auto_tags_[i].updateMods(files, mod_ids, &node.child(1).child(i));
  updateAutoTagMap();
  updateSettings(true);
}

void ModdedApplication::deleteAllData()
{
  // Iterate in reverse: removeDeployer erases deployers_[i], so forward iteration would shift
  // the vector and skip every other deployer (leaving its mods deployed with no .lmmfiles).
  for(int i = static_cast<int>(deployers_.size()) - 1; i >= 0; i--)
    removeDeployer(i, true);
  // Use the non-throwing overloads and continue past individual failures so a single unremovable
  // file does not abort the whole teardown; log every failure instead.
  const auto remove_all_logged = [this](const sfs::path& path)
  {
    std::error_code ec;
    sfs::remove_all(path, ec);
    if(ec)
      log_(Log::LOG_ERROR,
           std::format("Could not remove '{}': {}", path.string(), ec.message()));
  };
  for(const auto& mod : installed_mods_)
    remove_all_logged(staging_dir_ / std::to_string(mod.id));
  std::error_code ec;
  sfs::remove(staging_dir_ / CONFIG_FILE_NAME, ec);
  if(ec)
    log_(Log::LOG_ERROR,
         std::format("Could not remove '{}': {}",
                     (staging_dir_ / CONFIG_FILE_NAME).string(),
                     ec.message()));
  remove_all_logged(getDownloadDir());
}

void ModdedApplication::setAppVersion(const std::string& app_version)
{
  app_versions_[current_profile_] = app_version;
  updateSettings(true);
}

void ModdedApplication::setModSources(int mod_id,
                                      const std::string& local_source,
                                      const std::string& remote_source)
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](Mod m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    throw std::runtime_error("Error: Unknown mod id: " + std::to_string(mod_id));
  iter->local_source = local_source;
  iter->remote_source = remote_source;
  updateSettings(true);
}

nexus::Page ModdedApplication::getNexusPage(int mod_id)
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](Mod m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    throw std::runtime_error("Error: Unknown mod id: " + std::to_string(mod_id));
  return nexus::Api::getNexusPage(iter->remote_source);
}

void ModdedApplication::checkForModUpdates()
{
  std::vector<int> target_mod_indices;
  for(const auto& [i, mod] : str::enumerate_view(installed_mods_))
  {
    // Pinned mods are included too: performUpdateCheck only suppresses the notification while the
    // remote version still equals the pin, so a strictly newer remote version still surfaces
    // (limo-app/limo: pinned mods were ignored forever). Mods explicitly marked update-ignored
    // (limo-app/limo#144) are excluded entirely.
    if(!isUpdateIgnored(mod.id) && nexus::Api::modUrlIsValid(mod.remote_source) &&
       mod.remote_update_time <= mod.install_time)
      target_mod_indices.push_back(i);
  }
  performUpdateCheck(target_mod_indices);
}

void ModdedApplication::checkModsForUpdates(const std::vector<int>& mod_ids)
{
  std::vector<int> target_mod_indices;
  for(const auto& [i, mod] : str::enumerate_view(installed_mods_))
  {
    if(str::find(mod_ids, mod.id) != mod_ids.end() && !isUpdateIgnored(mod.id) &&
       nexus::Api::modUrlIsValid(mod.remote_source) && mod.remote_update_time <= mod.install_time)
      target_mod_indices.push_back(i);
  }
  performUpdateCheck(target_mod_indices);
}

void ModdedApplication::suppressUpdateNotification(const std::vector<int>& mod_ids)
{
  for(int mod_id : mod_ids)
  {
    auto iter = std::find_if(installed_mods_.begin(),
                             installed_mods_.end(),
                             [mod_id](const Mod& mod) { return mod.id == mod_id; });
    if(iter != installed_mods_.end() && iter->remote_update_time > iter->install_time)
      iter->suppress_update_time =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  }
  updateSettings(true);
}

void ModdedApplication::setUpdateIgnored(int mod_id, bool ignored)
{
  if(ignored)
    update_ignore_list_.insert(mod_id);
  else
    update_ignore_list_.erase(mod_id);
  log_(Log::LOG_DEBUG,
       std::format("Updates for mod {} are now {}.",
                   mod_id,
                   ignored ? "ignored" : "checked"));
  updateSettings(true);
}

bool ModdedApplication::isUpdateIgnored(int mod_id) const
{
  return update_ignore_list_.contains(mod_id);
}

ExternalChangesInfo ModdedApplication::getExternalChanges(int deployer)
{
  ExternalChangesInfo info;
  ProgressNode node(progress_callback_);
  info.file_changes = deployers_[deployer]->getExternallyModifiedFiles({ &node });
  info.deployer_id = deployer;
  info.deployer_name = deployers_[deployer]->getName();
  return info;
}

void ModdedApplication::keepOrRevertFileModifications(
  int deployer,
  const FileChangeChoices& changes_to_keep) const
{
  deployers_[deployer]->keepOrRevertFileModifications(changes_to_keep);
}

void ModdedApplication::fixInvalidHardLinkDeployers()
{
  for(auto& depl : deployers_)
    depl->fixInvalidLinkDeployMode();
}

void ModdedApplication::exportConfiguration(const std::vector<int>& deployers,
                                            const std::vector<std::string>& auto_tags)
{
  Json::Value json;
  json["name"] = name_;

  int i = 0;
  for(int deployer_id : deployers)
  {
    if(deployer_id < 0 || deployer_id >= deployers_.size())
      continue;

    const auto& deployer = deployers_[deployer_id];
    json["deployers"][i]["type"] = deployer->getType();
    json["deployers"][i]["name"] = deployer->getName();
    json["deployers"][i]["target_dir"] = generalizeSteamPath(deployer->getDestPath());
    if(deployer->isAutonomous())
      json["deployers"][i]["source_dir"] = generalizeSteamPath(deployer->getSourcePath());
    // use hard link by default; import will auto change this to sym link when needed
    json["deployers"][i]["deploy_mode"] =
      deployer->getDeployMode() == Deployer::copy ? "copy" : "hard_link";
    if(deployer->getType() == DeployerFactory::REVERSEDEPLOYER)
    {
      auto rev_depl = static_cast<ReverseDeployer*>(deployer.get());
      json["deployers"][i]["uses_separate_dirs"] = rev_depl->usesSeparateDirs();
      json["deployers"][i]["update_ignore_list"] = true;
    }
    i++;
  }

  i = 0;
  for(const auto& tag : auto_tags)
  {
    auto iter = str::find_if(auto_tags_, [name = tag](auto tag) { return tag.getName() == name; });
    if(iter == auto_tags_.end())
      continue;

    json["auto_tags"][i] = iter->toJson();
    json["auto_tags"][i].removeMember("mod_ids");
    i++;
  }

  sfs::path path = staging_dir_ / (export_file_name + ".json");
  if(pu::exists(path))
  {
    i = 1;
    do
      path = staging_dir_ / (export_file_name + std::format("_{}.json", i++));
    while(pu::exists(path));
  }
  log_(Log::LOG_INFO,
       std::format("Exporting configuration for '{}' to '{}'", name_, path.string()));
  std::ofstream file(path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not write to \"" + path.string() + "\".");
  file << json;
}

void ModdedApplication::exportInstance(const sfs::path& target) const
{
  // Reuse the on-disk settings as the basis for the bundle. json_settings_ is kept in sync
  // with the object state by updateSettings, so it already contains the full config
  // (app + deployers + profiles + tools + groups + tags + rules), minus the mod blobs.
  Json::Value bundle;
  bundle["format"] = "limo_instance";
  bundle["version"] = INSTANCE_BUNDLE_VERSION;
  bundle["exported_name"] = name_;

  Json::Value config = json_settings_;
  // Generalize the icon path so the bundle does not pin a machine specific Steam path.
  if(config.isMember("icon_path"))
    config["icon_path"] = generalizeSteamPath(config["icon_path"].asString());
  // Generalize deployer destination paths; source paths are already stored relocatable
  // (STAGING_TOKEN or an autonomous path) by updateSettings.
  if(config.isMember("deployers"))
  {
    for(Json::Value& deployer : config["deployers"])
    {
      if(deployer.isMember("dest_path"))
        deployer["dest_path"] = generalizeSteamPath(deployer["dest_path"].asString());
    }
  }
  // Generalize tool paths if present. Tools serialize via Tool::toJson; we only touch obvious
  // path-like string members to stay conservative.
  if(config.isMember("tools"))
  {
    for(Json::Value& tool : config["tools"])
    {
      for(const char* key : { "path", "command", "working_directory", "icon_path" })
      {
        if(tool.isMember(key) && tool[key].isString())
          tool[key] = generalizeSteamPath(tool[key].asString());
      }
    }
  }

  bundle["config"] = config;

  sfs::path out_path = target;
  if(sfs::is_directory(target))
    out_path = target / INSTANCE_BUNDLE_FILE_NAME;

  log_(Log::LOG_INFO,
       std::format("Exporting instance '{}' to '{}'", name_, out_path.string()));
  sfs::path tmp_path = out_path;
  tmp_path += ".tmp";
  std::ofstream file(tmp_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not write to \"" + out_path.string() + "\".");
  file << bundle;
  file.close();
  sfs::rename(tmp_path, out_path);
}

Json::Value ModdedApplication::parseInstanceBundle(const sfs::path& bundle)
{
  sfs::path in_path = bundle;
  if(sfs::is_directory(bundle))
    in_path = bundle / INSTANCE_BUNDLE_FILE_NAME;

  std::ifstream file(in_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not read from \"" + in_path.string() + "\".");
  Json::Value root;
  try
  {
    file >> root;
  }
  catch(const Json::Exception& e)
  {
    throw ParseError("Could not parse instance bundle \"" + in_path.string() + "\": " + e.what());
  }
  file.close();

  if(!root.isMember("format") || root["format"].asString() != "limo_instance")
    throw ParseError("\"" + in_path.string() + "\" is not a valid Limo instance bundle.");
  if(root.isMember("version") && root["version"].asInt() > INSTANCE_BUNDLE_VERSION)
    throw ParseError(std::format(
      "Instance bundle \"{}\" was created by a newer version of Limo (bundle version {}).",
      in_path.string(),
      root["version"].asInt()));
  if(!root.isMember("config"))
    throw ParseError("Instance bundle \"" + in_path.string() + "\" is missing its config.");

  return root["config"];
}

void ModdedApplication::importInstanceInto(const sfs::path& bundle, const sfs::path& staging_dir)
{
  if(sfs::exists(staging_dir / CONFIG_FILE_NAME))
    throw std::runtime_error("Error: A config file already exists in \"" + staging_dir.string() +
                             "\". Refusing to overwrite an existing instance.");

  Json::Value config = parseInstanceBundle(bundle);

  // Security: an imported bundle is untrusted input. Both deploy "hooks" and per-tool "command"
  // overwrites are command lines that get handed to the shell (std::system / popen) during normal
  // use, so importing them verbatim would let a malicious bundle run arbitrary commands without
  // the user ever reviewing them. Strip these executable fields here; the user can re-add hooks
  // and tool commands deliberately after reviewing the imported instance.
  if(config.isMember("hooks"))
  {
    config.removeMember("hooks");
    Log::log(Log::LOG_WARNING,
             "Removed deploy hooks from imported instance bundle for security; re-add them "
             "manually after reviewing the imported instance.");
  }
  if(config.isMember("tools") && config["tools"].isArray())
  {
    bool stripped_tool_command = false;
    for(Json::Value& tool : config["tools"])
    {
      if(tool.isObject() && tool.isMember("command") && !tool["command"].asString().empty())
      {
        tool["command"] = "";
        stripped_tool_command = true;
      }
    }
    if(stripped_tool_command)
      Log::log(Log::LOG_WARNING,
               "Removed custom tool command overwrites from imported instance bundle for "
               "security; re-add them manually after reviewing the imported instance.");
  }

  sfs::create_directories(staging_dir);
  sfs::path config_path = staging_dir / CONFIG_FILE_NAME;
  sfs::path tmp_path = staging_dir / (CONFIG_FILE_NAME + ".tmp");
  std::ofstream file(tmp_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not write to \"" + config_path.string() + "\".");
  file << config;
  file.close();
  sfs::rename(tmp_path, config_path);
  // A ModdedApplication constructed on staging_dir will now load this config. Steam/home
  // path tokens and STAGING_TOKEN are resolved lazily during that load, so the imported
  // instance is immediately usable on the new machine.
}

void ModdedApplication::exportProfile(int profile, const sfs::path& target) const
{
  if(profile < 0 || profile >= (int)profile_names_.size())
    throw std::runtime_error("Error: Invalid profile index: " + std::to_string(profile));

  // Build the JSON document describing the profile. The load order is stored as the full
  // TREE, serialized exactly as in the on-disk settings (TreeItem::toJson -> { children: [...] }),
  // so it round-trips through Deployer::setLoadorder(Json::Value) on import.
  Json::Value bundle;
  bundle["format"] = "limo_profile";
  bundle["version"] = PROFILE_BUNDLE_VERSION;
  bundle["exported_instance"] = name_;
  bundle["name"] = profile_names_[profile];
  bundle["app_version"] = app_versions_[profile];

  for(int depl = 0; depl < (int)deployers_.size(); depl++)
  {
    // Autonomous deployers manage their own load order independently of Limo profiles and have
    // no per-profile data to export.
    if(deployers_[depl]->isAutonomous())
      continue;

    // Temporarily switch the deployer to the requested profile to read its load order and
    // conflict groups, then restore the previously active profile. The RAII guard restores the
    // profile even if serialization below throws, so the deployer is never left on the wrong
    // profile.
    struct ProfileGuard
    {
      Deployer* deployer;
      int previous_profile;
      ~ProfileGuard() { deployer->setProfile(previous_profile); }
    } profile_guard{ deployers_[depl].get(), deployers_[depl]->getProfile() };
    deployers_[depl]->setProfile(profile);

    Json::Value depl_json;
    depl_json["name"] = deployers_[depl]->getName();
    depl_json["type"] = deployers_[depl]->getType();
    // Full load-order tree: { children: [ {id,status}|{name,expanded,children:[...]} , ... ] }.
    depl_json["loadorder"] = deployers_[depl]->getLoadorder()->toJson();

    const auto conflict_groups = deployers_[depl]->getConflictGroups();
    for(int group = 0; group < (int)conflict_groups.size(); group++)
    {
      for(int i = 0; i < (int)conflict_groups[group].size(); i++)
        depl_json["conflict_groups"][group][i] = conflict_groups[group][i];
    }

    bundle["deployers"].append(depl_json);
  }

  // Instance level group / active-member info, included for reference (import does not re-apply
  // it, but it documents which mod of each conflict group was active when the profile was saved).
  for(int group = 0; group < (int)groups_.size(); group++)
  {
    Json::Value group_json;
    group_json["name"] = group < (int)group_names_.size() ? group_names_[group] : std::string();
    group_json["active_member"] =
      group < (int)active_group_members_.size() ? active_group_members_[group] : -1;
    for(int i = 0; i < (int)groups_[group].size(); i++)
      group_json["members"][i] = groups_[group][i];
    bundle["groups"].append(group_json);
  }

  sfs::path out_path = target;
  if(sfs::is_directory(target))
    out_path = target / (profile_names_[profile] + ".zip");

  log_(Log::LOG_INFO,
       std::format(
         "Exporting profile '{}' to '{}'", profile_names_[profile], out_path.string()));

  // Serialize the JSON to a string so it can be written as a single archive entry. This reuses
  // the same libarchive zip-write pattern as exportModArchive.
  std::ostringstream oss;
  oss << bundle;
  const std::string json_data = oss.str();

  struct archive* dest = archive_write_new();
  if(dest == nullptr)
    throw std::runtime_error("Error: Could not allocate archive for export.");
  struct ArchiveGuard
  {
    struct archive* a;
    ~ArchiveGuard() { archive_write_free(a); }
  } guard{ dest };

  archive_write_set_format_zip(dest);
  if(archive_write_open_filename(dest, out_path.string().c_str()) != ARCHIVE_OK)
    throw std::runtime_error("Error: Could not open archive '" + out_path.string() +
                             "' for writing: " + archive_error_string(dest));

  struct archive_entry* entry = archive_entry_new();
  struct EntryGuard
  {
    struct archive_entry* e;
    ~EntryGuard() { archive_entry_free(e); }
  } entry_guard{ entry };

  archive_entry_set_pathname(entry, PROFILE_BUNDLE_FILE_NAME.c_str());
  archive_entry_set_size(entry, static_cast<la_int64_t>(json_data.size()));
  archive_entry_set_filetype(entry, AE_IFREG);
  archive_entry_set_perm(entry, 0644);

  if(archive_write_header(dest, entry) != ARCHIVE_OK)
    throw std::runtime_error("Error: Could not write archive header for '" +
                             PROFILE_BUNDLE_FILE_NAME + "': " + archive_error_string(dest));
  if(archive_write_data(dest, json_data.data(), json_data.size()) < 0)
    throw std::runtime_error("Error: Could not write data for '" + PROFILE_BUNDLE_FILE_NAME +
                             "': " + archive_error_string(dest));

  if(archive_write_close(dest) != ARCHIVE_OK)
    throw std::runtime_error("Error: Could not finalize archive '" + out_path.string() +
                             "': " + archive_error_string(dest));
}

void ModdedApplication::importProfile(const sfs::path& bundle)
{
  // Resolve the input: either a directory containing the extracted JSON, or a zip archive.
  std::string json_data;
  sfs::path direct = bundle;
  if(sfs::is_directory(bundle))
    direct = bundle / PROFILE_BUNDLE_FILE_NAME;

  if(direct.filename() == PROFILE_BUNDLE_FILE_NAME && sfs::exists(direct))
  {
    std::ifstream file(direct, std::fstream::binary);
    if(!file.is_open())
      throw std::runtime_error("Error: Could not read from \"" + direct.string() + "\".");
    std::ostringstream oss;
    oss << file.rdbuf();
    json_data = oss.str();
  }
  else
  {
    // Read PROFILE_BUNDLE_FILE_NAME out of the zip using the same libarchive read pattern as
    // the installer.
    struct archive* source = archive_read_new();
    if(source == nullptr)
      throw std::runtime_error("Error: Could not allocate archive for import.");
    struct ReadGuard
    {
      struct archive* a;
      ~ReadGuard() { archive_read_free(a); }
    } guard{ source };

    archive_read_support_filter_all(source);
    archive_read_support_format_all(source);
    if(archive_read_open_filename(source, bundle.string().c_str(), 10240) != ARCHIVE_OK)
      throw std::runtime_error("Error: Could not open archive '" + bundle.string() +
                               "' for reading: " + archive_error_string(source));

    struct archive_entry* entry = nullptr;
    bool found = false;
    while(archive_read_next_header(source, &entry) == ARCHIVE_OK)
    {
      if(std::string(archive_entry_pathname(entry)) != PROFILE_BUNDLE_FILE_NAME)
        continue;
      const void* buff = nullptr;
      size_t size = 0;
      la_int64_t offset = 0;
      int return_code = ARCHIVE_OK;
      while((return_code = archive_read_data_block(source, &buff, &size, &offset)) == ARCHIVE_OK)
        json_data.append(static_cast<const char*>(buff), size);
      if(return_code != ARCHIVE_EOF)
        throw std::runtime_error("Error: Could not read '" + PROFILE_BUNDLE_FILE_NAME +
                                 "' from archive: " + archive_error_string(source));
      found = true;
      break;
    }
    if(!found)
      throw ParseError("\"" + bundle.string() + "\" does not contain a '" +
                       PROFILE_BUNDLE_FILE_NAME + "' entry.");
  }

  Json::Value root;
  {
    std::istringstream iss(json_data);
    iss >> root;
  }

  if(!root.isMember("format") || root["format"].asString() != "limo_profile")
    throw ParseError("\"" + bundle.string() + "\" is not a valid Limo profile bundle.");
  if(root.isMember("version") && root["version"].asInt() > PROFILE_BUNDLE_VERSION)
    throw ParseError(std::format(
      "Profile bundle \"{}\" was created by a newer version of Limo (bundle version {}).",
      bundle.string(),
      root["version"].asInt()));

  EditProfileInfo info;
  info.name = root.get("name", "Imported profile").asString();
  info.app_version = root.get("app_version", "").asString();
  info.source = -1;
  log_(Log::LOG_INFO, std::format("Importing profile '{}'", info.name));

  // Append the new profile. This adds an empty load order for every (non-autonomous) deployer.
  addProfile(info);
  const int new_profile = (int)profile_names_.size() - 1;

  // Index the saved per-deployer data by (name, type) so we can match it against this instance's
  // deployers regardless of ordering / count differences. Keying by name alone would silently
  // drop one load order when two deployers share a name; keying by (name, type) keeps them
  // distinct and we warn when even that pair collides.
  std::map<std::pair<std::string, std::string>, const Json::Value*> saved_by_name_type;
  for(const Json::Value& depl_json : root["deployers"])
  {
    const std::pair<std::string, std::string> key{ depl_json["name"].asString(),
                                                   depl_json.get("type", "").asString() };
    if(saved_by_name_type.contains(key))
      log_(Log::LOG_WARNING,
           std::format("Profile bundle contains multiple deployers named '{}' of the same type; "
                       "only the last saved load order will be applied.",
                       key.first));
    saved_by_name_type[key] = &depl_json;
  }

  for(int depl = 0; depl < (int)deployers_.size(); depl++)
  {
    if(deployers_[depl]->isAutonomous())
      continue;
    const std::pair<std::string, std::string> key{ deployers_[depl]->getName(),
                                                   deployers_[depl]->getType() };
    auto iter = saved_by_name_type.find(key);
    // Backward compatibility: bundles created before "type" was exported store an empty type,
    // so fall back to matching the same name with an empty saved type.
    if(iter == saved_by_name_type.end())
      iter = saved_by_name_type.find({ deployers_[depl]->getName(), "" });
    if(iter == saved_by_name_type.end())
    {
      log_(Log::LOG_DEBUG,
           std::format("No saved load order for deployer '{}' in profile bundle; left empty.",
                       deployers_[depl]->getName()));
      continue;
    }
    const Json::Value& depl_json = *iter->second;

    Json::Value filtered =
      filterLoadorderForInstalledMods(depl_json["loadorder"], "importing profile");

    deployers_[depl]->setProfile(new_profile);
    deployers_[depl]->setLoadorder(filtered);

    // Restore conflict groups, dropping any unknown mod ids.
    std::vector<std::vector<int>> conflict_groups;
    for(const Json::Value& group_json : depl_json["conflict_groups"])
    {
      std::vector<int> new_group;
      for(const Json::Value& mod_json : group_json)
      {
        const int mod_id = mod_json.asInt();
        if(std::find_if(installed_mods_.begin(),
                        installed_mods_.end(),
                        [mod_id](const Mod& m) { return m.id == mod_id; }) != installed_mods_.end())
          new_group.push_back(mod_id);
      }
      conflict_groups.push_back(std::move(new_group));
    }
    deployers_[depl]->setConflictGroups(conflict_groups);
    deployers_[depl]->setProfile(current_profile_);
  }

  updateSettings(true);
}

// fork #54: deploy restore points (load-order snapshots).
void ModdedApplication::createRestorePoint(const std::string& name)
{
  Json::Value point;
  point["name"] = name;
  point["timestamp"] = static_cast<Json::Int64>(
    std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch())
      .count());
  point["deployers"] = Json::Value(Json::arrayValue);

  // Snapshot the current load order of every deployer whose load order is persisted in the
  // config (the non-autonomous deployers; mirrors the per-deployer loadorder persistence in
  // updateSettings). The tree JSON already encodes enabled/disabled and group state.
  for(int depl = 0; depl < (int)deployers_.size(); depl++)
  {
    if(deployers_[depl]->isAutonomous())
      continue;
    Json::Value depl_json;
    depl_json["name"] = deployers_[depl]->getName();
    depl_json["loadorder"] = deployers_[depl]->getLoadorder()->toJson();
    point["deployers"].append(depl_json);
  }

  restore_points_.append(point);

  // Trim to the most recent MAX_RESTORE_POINTS, dropping the oldest (front) entries.
  while((int)restore_points_.size() > MAX_RESTORE_POINTS)
  {
    Json::Value removed;
    restore_points_.removeIndex(0, &removed);
  }

  updateSettings(true);
}

std::vector<RestorePoint> ModdedApplication::getRestorePoints() const
{
  std::vector<RestorePoint> points;
  points.reserve(restore_points_.size());
  for(const Json::Value& point : restore_points_)
  {
    RestorePoint rp;
    rp.name = point.get("name", "").asString();
    rp.timestamp = point.get("timestamp", 0).asInt64();
    points.push_back(std::move(rp));
  }
  return points;
}

void ModdedApplication::restoreRestorePoint(int index)
{
  if(index < 0 || index >= (int)restore_points_.size())
    return;

  const Json::Value& point = restore_points_[index];

  // Index the saved per-deployer load orders by deployer name so we can match them against this
  // instance's deployers regardless of ordering / count differences.
  std::map<std::string, const Json::Value*> saved_by_name;
  for(const Json::Value& depl_json : point["deployers"])
    saved_by_name[depl_json["name"].asString()] = &depl_json;

  for(int depl = 0; depl < (int)deployers_.size(); depl++)
  {
    if(deployers_[depl]->isAutonomous())
      continue;
    auto iter = saved_by_name.find(deployers_[depl]->getName());
    if(iter == saved_by_name.end())
      continue;
    const Json::Value& depl_json = *iter->second;

    // Drop any saved entries referencing mods no longer installed, so setLoadorder never
    // produces dangling ids. Separators (entries without an "id") are kept. This mirrors the
    // filtering done in importProfile before reusing the same Deployer::setLoadorder path.
    Json::Value filtered =
      filterLoadorderForInstalledMods(depl_json["loadorder"], "restoring load order");

    // Reuse the same apply path the config load uses for the tree load-order format.
    deployers_[depl]->setLoadorder(filtered);
  }

  updateSettings(true);
}

void ModdedApplication::deleteRestorePoint(int index)
{
  if(index < 0 || index >= (int)restore_points_.size())
    return;

  Json::Value remaining(Json::arrayValue);
  for(int i = 0; i < (int)restore_points_.size(); i++)
  {
    if(i == index)
      continue;
    remaining.append(restore_points_[i]);
  }
  restore_points_ = remaining;

  updateSettings(true);
}

void ModdedApplication::updateIgnoredFiles(int deployer)
{
  if(deployers_[deployer]->getType() != DeployerFactory::REVERSEDEPLOYER)
  {
    log_(Log::LOG_DEBUG, "Ignored files can only be updated for ReverseDeployers.");
    return;
  }
  auto depl = static_cast<ReverseDeployer*>(deployers_[deployer].get());
  depl->updateIgnoredFiles(true);
}

void ModdedApplication::addModToIgnoreList(int deployer, int mod_id)
{
  if(deployers_[deployer]->getType() != DeployerFactory::REVERSEDEPLOYER)
  {
    log_(Log::LOG_DEBUG, "Ignored files can only be updated for ReverseDeployers.");
    return;
  }
  auto depl = static_cast<ReverseDeployer*>(deployers_[deployer].get());
  depl->addModToIgnoreList(mod_id);
}

// fork #81: re-scan every ReverseDeployer's target directory so externally produced files
// (e.g. tool output picked up by a reverse deployer) become visible without a deploy cycle.
void ModdedApplication::refreshReverseDeployers()
{
  for(const auto& deployer : deployers_)
  {
    if(deployer->getType() != DeployerFactory::REVERSEDEPLOYER)
      continue;
    auto depl = static_cast<ReverseDeployer*>(deployer.get());
    depl->updateManagedFiles(true);
  }
}

void ModdedApplication::applyModAction(int deployer, int action, int mod_id)
{
  deployers_[deployer]->applyModAction(action, mod_id);
  updateSettings(true);
}

std::filesystem::path ModdedApplication::getDownloadDir() const
{
  return staging_dir_ / DOWNLOAD_DIR;
}

// fork #145: compute downloaded archives belonging to outdated mod versions.
std::pair<std::vector<PrunableArchive>, unsigned long>
ModdedApplication::getPrunableArchives() const
{
  std::vector<PrunableArchive> prunable;
  unsigned long total_size = 0;

  std::error_code ec;
  const sfs::path download_dir = sfs::canonical(getDownloadDir(), ec);
  if(ec)
    return { prunable, total_size };

  // Collect the local_source archives of every mod which is the active member of its group.
  // These are the in-use archives and must never be pruned.
  std::set<sfs::path> keep;
  for(int group = 0; group < (int)groups_.size(); group++)
  {
    const int active_id = getActiveGroupMember(group);
    auto iter = std::find_if(installed_mods_.begin(),
                             installed_mods_.end(),
                             [active_id](const Mod& m) { return m.id == active_id; });
    if(iter == installed_mods_.end())
      continue;
    std::error_code keep_ec;
    const sfs::path canon = sfs::canonical(iter->local_source, keep_ec);
    if(!keep_ec)
      keep.insert(canon);
  }

  // For every inactive group member, its local_source is an outdated version. Prune it only
  // if it resides inside the download directory, still exists, and is not also an in-use
  // archive for some other mod.
  std::set<sfs::path> seen;
  for(int group = 0; group < (int)groups_.size(); group++)
  {
    const int active_id = getActiveGroupMember(group);
    for(const int mod_id : getGroupMembers(group))
    {
      if(mod_id == active_id)
        continue;
      auto iter = std::find_if(installed_mods_.begin(),
                               installed_mods_.end(),
                               [mod_id](const Mod& m) { return m.id == mod_id; });
      if(iter == installed_mods_.end())
        continue;

      std::error_code mod_ec;
      const sfs::path canon = sfs::canonical(iter->local_source, mod_ec);
      if(mod_ec)
        continue;
      // Skip if this archive is in use, already queued, or not a regular file.
      if(keep.contains(canon) || seen.contains(canon))
        continue;
      if(!sfs::is_regular_file(canon, mod_ec) || mod_ec)
        continue;
      // Only prune archives that actually live inside the download directory.
      std::error_code rel_ec;
      const sfs::path rel = sfs::relative(canon, download_dir, rel_ec);
      if(rel_ec || rel.empty() || *rel.begin() == "..")
        continue;

      std::error_code size_ec;
      const auto file_size = sfs::file_size(canon, size_ec);
      if(size_ec)
        continue;

      seen.insert(canon);
      prunable.push_back({ canon, static_cast<unsigned long>(file_size) });
      total_size += static_cast<unsigned long>(file_size);
    }
  }

  return { prunable, total_size };
}

// fork #145: delete the supplied archive files, swallowing all filesystem errors.
int ModdedApplication::pruneArchives(const std::vector<std::filesystem::path>& paths) const
{
  int deleted = 0;
  for(const auto& path : paths)
  {
    std::error_code ec;
    if(sfs::remove(path, ec) && !ec)
    {
      deleted++;
      Log::info("Pruned outdated archive '" + path.string() + "'.");
    }
    else if(ec)
      Log::debug("Could not prune archive '" + path.string() + "': " + ec.message());
  }
  return deleted;
}

void ModdedApplication::setModNote(int mod_id, const std::string& note)
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](const Mod& m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    return;
  iter->note = note;
  updateSettings(true);
}

// fork #199: assign/clear a highlight colour for a mod and persist it.
void ModdedApplication::setModColor(int mod_id, const std::string& hex)
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](const Mod& m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    return;
  if(hex.empty())
    mod_color_map_.erase(mod_id);
  else
    mod_color_map_[mod_id] = hex;
  updateSettings(true);
}

// fork #199: return the highlight colour for a mod, or empty string if none.
std::string ModdedApplication::getModColor(int mod_id) const
{
  auto iter = mod_color_map_.find(mod_id);
  if(iter == mod_color_map_.end())
    return "";
  return iter->second;
}

// fork #199: return the full mod-id -> colour map for bulk UI consumption.
std::map<int, std::string> ModdedApplication::getModColors() const
{
  return mod_color_map_;
}

// fork #198: assign/clear a free-text category for a mod and persist it.
void ModdedApplication::setModCategory(int mod_id, const std::string& category)
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](const Mod& m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    return;
  if(category.empty())
    mod_category_map_.erase(mod_id);
  else
    mod_category_map_[mod_id] = category;
  updateSettings(true);
}

// fork #198: return the category for a mod, or empty string if none.
std::string ModdedApplication::getModCategory(int mod_id) const
{
  auto iter = mod_category_map_.find(mod_id);
  if(iter == mod_category_map_.end())
    return "";
  return iter->second;
}

void ModdedApplication::pinModVersion(int mod_id)
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](const Mod& m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    return;
  iter->pinned_version = iter->version;
  updateSettings(true);
}

void ModdedApplication::unpinModVersion(int mod_id)
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](const Mod& m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    return;
  iter->pinned_version = "";
  updateSettings(true);
}

sfs::path ModdedApplication::iconPath() const
{
  return icon_path_;
}

void ModdedApplication::setIconPath(const sfs::path& icon_path)
{
  icon_path_ = icon_path;
  updateSettings(true);
}

void ModdedApplication::updateSettings(bool write)
{
  json_settings_.clear();
  json_settings_["name"] = name_;
  json_settings_["command"] = command_;
  json_settings_["icon_path"] = icon_path_.string();
  for(int group = 0; group < (int)groups_.size(); group++)
  {
    json_settings_["groups"][group]["active_member"] = active_group_members_[group];
    json_settings_["groups"][group]["name"] =
      group < (int)group_names_.size() ? group_names_[group] : "";
    json_settings_["groups"][group]["notes"] =
      group < (int)group_notes_.size() ? group_notes_[group] : "";
    for(int i = 0; i < (int)groups_[group].size(); i++)
    {
      json_settings_["groups"][group]["members"][i] = groups_[group][i];
    }
  }

  for(int i = 0; i < profile_names_.size(); i++)
    json_settings_["profiles"][i]["name"] = profile_names_[i];

  for(int i = 0; i < app_versions_.size(); i++)
    json_settings_["profiles"][i]["app_version"] = app_versions_[i];

  // fork #232: persist each profile's active modpacks (manual-tag names).
  for(int i = 0; i < (int)active_packs_per_profile_.size() && i < (int)profile_names_.size(); i++)
  {
    json_settings_["profiles"][i]["active_packs"] = Json::Value(Json::arrayValue);
    int j = 0;
    for(const auto& pack_name : active_packs_per_profile_[i])
      json_settings_["profiles"][i]["active_packs"][j++] = pack_name;
  }

  for(int i = 0; i < installed_mods_.size(); i++)
  {
    json_settings_["installed_mods"][i] = installed_mods_[i].toJson();
    json_settings_["installed_mods"][i]["installer"] = installer_map_[installed_mods_[i].id];
    // fork #199: persist the optional highlight colour alongside the mod entry.
    auto color_iter = mod_color_map_.find(installed_mods_[i].id);
    if(color_iter != mod_color_map_.end() && !color_iter->second.empty())
      json_settings_["installed_mods"][i]["color"] = color_iter->second;
    // fork #198: persist the optional category alongside the mod entry.
    auto category_iter = mod_category_map_.find(installed_mods_[i].id);
    if(category_iter != mod_category_map_.end() && !category_iter->second.empty())
      json_settings_["installed_mods"][i]["category"] = category_iter->second;
  }

  for(int depl = 0; depl < deployers_.size(); depl++)
  {
    json_settings_["deployers"][depl]["dest_path"] = deployers_[depl]->getDestPath();
    if(deployers_[depl]->isAutonomous())
      json_settings_["deployers"][depl]["source_path"] =
        relativizeToStaging(deployers_[depl]->sourcePath());
    else
      // Non-autonomous deployers always source from the staging directory; store this in a
      // relocatable form so the instance can be moved to a different staging path.
      json_settings_["deployers"][depl]["source_path"] = STAGING_TOKEN;
    json_settings_["deployers"][depl]["name"] = deployers_[depl]->getName();
    json_settings_["deployers"][depl]["type"] = deployers_[depl]->getType();
    json_settings_["deployers"][depl]["deploy_mode"] = deployers_[depl]->getDeployMode();
    json_settings_["deployers"][depl]["enable_unsafe_sorting"] =
      deployers_[depl]->getEnableUnsafeSorting();

    if(!deployers_[depl]->isAutonomous())
    {
      for(int prof = 0; prof < profile_names_.size(); prof++)
      {
        deployers_[depl]->setProfile(prof);
        json_settings_["deployers"][depl]["profiles"][prof]["name"] = profile_names_[prof];
        auto loadorder = deployers_[depl]->getLoadorder();
        json_settings_["deployers"][depl]["profiles"][prof]["loadorder"] = loadorder->toJson();
        auto conflict_groups = deployers_[depl]->getConflictGroups();
        for(int group = 0; group < conflict_groups.size(); group++)
        {
          for(int i = 0; i < conflict_groups[group].size(); i++)
            json_settings_["deployers"][depl]["profiles"][prof]["conflict_groups"][group][i] =
              conflict_groups[group][i];
        }
      }
    }
    deployers_[depl]->setProfile(current_profile_);
  }

  for(int tool = 0; tool < tools_.size(); tool++)
    json_settings_["tools"][tool] = tools_[tool].toJson();

  const auto targets = bak_man_.getTargets();
  for(int i = 0; i < targets.size(); i++)
    json_settings_["backup_targets"][i]["path"] = targets[i].path.string();

  for(int i = 0; i < manual_tags_.size(); i++)
    json_settings_["manual_tags"][i] = manual_tags_[i].toJson();

  for(int i = 0; i < auto_tags_.size(); i++)
  {
    if(!auto_tags_[i].getExpression().empty())
      json_settings_["auto_tags"][i] = auto_tags_[i].toJson();
  }

  json_settings_["steam_app_id"] = steam_app_id_;

  // fork #59: persist the configured vanilla Witcher 3 scripts root.
  json_settings_["tw3_vanilla_scripts_root"] = tw3_vanilla_scripts_root_;

  for(int i = 0; i < mod_rules_.size(); i++)
    json_settings_["mod_rules"][i] = mod_rules_[i].toJson();

  // Persist the active profile so the user's selection is restored on restart.
  // Without this the currently selected profile was never written and always
  // reset to 0 on the next launch.
  json_settings_["current_profile"] = current_profile_;

  {
    int i = 0;
    for(int mod_id : update_ignore_list_)
      json_settings_["update_ignore_list"][i++] = mod_id;
  }

  json_settings_["hooks"]["pre_deploy"] = pre_deploy_hook_;
  json_settings_["hooks"]["post_deploy"] = post_deploy_hook_;
  json_settings_["hooks"]["pre_undeploy"] = pre_undeploy_hook_;
  json_settings_["hooks"]["post_undeploy"] = post_undeploy_hook_;

  // fork #54: persist deploy restore points (load-order snapshots).
  json_settings_["restore_points"] = restore_points_;

  if(write)
    writeSettings();
}

void ModdedApplication::writeSettings() const
{
  // Refuse to overwrite an existing settings file when the last load failed to parse it.
  // Otherwise a partial/empty in-memory config could silently destroy a recoverable file.
  if(settings_load_failed_ && sfs::exists(staging_dir_ / CONFIG_FILE_NAME))
  {
    log_(Log::LOG_ERROR,
         "Refusing to overwrite settings file \"" +
           (staging_dir_ / CONFIG_FILE_NAME).string() +
           "\" because it could not be parsed during loading. Resolve or remove the corrupt "
           "file (a \"." +
           CONFIG_FILE_NAME + ".corrupt\" backup may have been created) before saving.");
    return;
  }
  sfs::path settings_file_path = staging_dir_ / (CONFIG_FILE_NAME + ".tmp");
  std::ofstream file(settings_file_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not write to \"" + settings_file_path.string() + "\".");
  file << json_settings_;
  file.close();
  sfs::rename(settings_file_path, staging_dir_ / CONFIG_FILE_NAME);
}

void ModdedApplication::readSettings()
{
  json_settings_.clear();
  sfs::path settings_file_path = staging_dir_ / CONFIG_FILE_NAME;
  std::ifstream file(settings_file_path, std::fstream::binary);
  if(!file.is_open())
    throw std::runtime_error("Error: Could not read from \"" + settings_file_path.string() + "\".");
  try
  {
    file >> json_settings_;
    file.close();
  }
  catch(...)
  {
    file.close();
    // The settings file is present but could not be parsed (corruption, partial write, ...).
    // Preserve the on-disk file so the user can recover and make sure no later write path
    // overwrites it with an empty/partial config.
    settings_load_failed_ = true;
    json_settings_.clear();
    try
    {
      sfs::path corrupt_path = staging_dir_ / ("." + CONFIG_FILE_NAME + ".corrupt");
      sfs::copy(settings_file_path, corrupt_path, sfs::copy_options::overwrite_existing);
      log_(Log::LOG_ERROR,
           "Could not parse settings file \"" + settings_file_path.string() +
             "\". A backup has been written to \"" + corrupt_path.string() +
             "\". The existing settings file has been left untouched to allow recovery.");
    }
    catch(const std::exception& backup_error)
    {
      log_(Log::LOG_ERROR,
           "Could not parse settings file \"" + settings_file_path.string() +
             "\" and additionally failed to create a backup: " + backup_error.what() +
             ". The existing settings file has been left untouched to allow recovery.");
    }
    throw;
  }
  // Successful parse: the in-memory state once again reflects the on-disk file, so writes are
  // safe again.
  settings_load_failed_ = false;
}

void ModdedApplication::updateState(bool read)
{
  installed_mods_.clear();
  deployers_.clear();
  groups_.clear();
  group_map_.clear();
  active_group_members_.clear();
  group_names_.clear();
  group_notes_.clear();
  profile_names_.clear();
  bak_man_.reset();
  tools_.clear();
  profile_names_.clear();
  app_versions_.clear();
  manual_tags_.clear();
  manual_tag_map_.clear();
  active_packs_per_profile_.clear(); // fork #232
  auto_tags_.clear();
  auto_tag_map_.clear();
  installer_map_.clear();
  mod_color_map_.clear(); // fork #199
  mod_category_map_.clear(); // fork #198
  mod_rules_.clear();
  update_ignore_list_.clear();
  restore_points_ = Json::Value(Json::arrayValue); // fork #54

  if(read)
  {
    if(!sfs::exists(staging_dir_ / CONFIG_FILE_NAME))
      return;
    readSettings();
  }

  if(!json_settings_.isMember("name"))
    throw ParseError("Name is missing in \"" + (staging_dir_ / CONFIG_FILE_NAME).string() + "\"");
  name_ = json_settings_["name"].asString();

  if(!json_settings_.isMember("command"))
    throw ParseError("Command is missing in \"" + (staging_dir_ / CONFIG_FILE_NAME).string() +
                     "\"");
  command_ = json_settings_["command"].asString();

  if(!json_settings_.isMember("icon_path"))
    throw ParseError("Icon path is missing in \"" + (staging_dir_ / CONFIG_FILE_NAME).string() +
                     "\"");
  icon_path_ = json_settings_["icon_path"].asString();

  if(!json_settings_.isMember("profiles"))
    throw ParseError("Profiles are missing in \"" + (staging_dir_ / CONFIG_FILE_NAME).string() +
                     "\"");

  Json::Value profiles = json_settings_["profiles"];
  for(int i = 0; i < profiles.size(); i++)
  {
    profile_names_.push_back(profiles[i]["name"].asString());
    app_versions_.push_back(profiles[i]["app_version"].asString());
    // fork #232: restore this profile's active modpacks (absent in older configs).
    std::set<std::string> active_packs;
    if(profiles[i].isMember("active_packs"))
      for(const auto& pack_name : profiles[i]["active_packs"])
        active_packs.insert(pack_name.asString());
    active_packs_per_profile_.push_back(active_packs);
  }

  // Restore the previously active profile. This is read before the deployer
  // loop below so the deployers/backup manager are set to the correct profile.
  // Older config files may not contain this field, in which case profile 0 is
  // used. The value is bounds-checked against the available profiles.
  current_profile_ = 0;
  if(json_settings_.isMember("current_profile"))
  {
    int stored_profile = json_settings_["current_profile"].asInt();
    if(stored_profile >= 0 && stored_profile < profile_names_.size())
      current_profile_ = stored_profile;
  }

  Json::Value installed_mods = json_settings_["installed_mods"];
  for(int i = 0; i < installed_mods.size(); i++)
  {
    installed_mods_.emplace_back(installed_mods[i]);
    std::string installer = installed_mods[i]["installer"].asString();
    std::vector<std::string> types = Installer::INSTALLER_TYPES;
    if(std::find(types.begin(), types.end(), installer) == types.end())
      throw ParseError("Unknown installer type: " + installer + " in \"" +
                       (staging_dir_ / CONFIG_FILE_NAME).string() + "\"");
    installer_map_[installed_mods[i]["id"].asInt()] = installer;
    // fork #199: restore the optional highlight colour; missing field means no colour.
    if(installed_mods[i].isMember("color"))
    {
      std::string color = installed_mods[i]["color"].asString();
      if(!color.empty())
        mod_color_map_[installed_mods[i]["id"].asInt()] = color;
    }
    // fork #198: restore the optional category; missing field means no category.
    if(installed_mods[i].isMember("category"))
    {
      std::string category = installed_mods[i]["category"].asString();
      if(!category.empty())
        mod_category_map_[installed_mods[i]["id"].asInt()] = category;
    }
  }
  Json::Value groups = json_settings_["groups"];
  for(int group = 0; group < groups.size(); group++)
  {
    groups_.push_back(std::vector<int>{});
    for(int i = 0; i < groups[group]["members"].size(); i++)
    {
      int mod_id = groups[group]["members"][i].asInt();
      if(std::find_if(installed_mods_.begin(),
                      installed_mods_.end(),
                      [mod_id](const Mod& m) { return m.id == mod_id; }) == installed_mods_.end())
        throw ParseError("Unknown mod id in group " + std::to_string(group) + ": " +
                         std::to_string(mod_id) + " in \"" +
                         (staging_dir_ / CONFIG_FILE_NAME).string() + "\"");
      if(std::find(groups_[group].begin(), groups_[group].end(), mod_id) != groups_[group].end())
        throw ParseError("Duplicate mod id in group " + std::to_string(group) + ": " +
                         std::to_string(mod_id) + " in \"" +
                         (staging_dir_ / CONFIG_FILE_NAME).string() + "\"");
      group_map_[mod_id] = group;
      groups_[group].push_back(mod_id);
    }
    if(!groups[group].isMember("active_member"))
      throw ParseError("Invalid active group member: missing in \"" +
                       (staging_dir_ / CONFIG_FILE_NAME).string() + "\"");
    int active_member = groups[group]["active_member"].asInt();
    if(std::find(groups_[group].begin(), groups_[group].end(), active_member) ==
       groups_[group].end())
      throw ParseError("Invalid active group member: " + std::to_string(active_member) + " in \"" +
                       (staging_dir_ / CONFIG_FILE_NAME).string() + "\"");
    active_group_members_.push_back(active_member);
    group_names_.push_back(groups[group].get("name", "").asString());
    group_notes_.push_back(groups[group].get("notes", "").asString());
  }
  Json::Value deployers = json_settings_["deployers"];
  for(int depl = 0; depl < deployers.size(); depl++)
  {
    std::vector<std::string> types = DeployerFactory::DEPLOYER_TYPES;
    std::string type = deployers[depl]["type"].asString();
    if(std::find(types.begin(), types.end(), type) == types.end())
      throw ParseError("Unknown deployer type: " + type + " in \"" +
                       (staging_dir_ / CONFIG_FILE_NAME).string() + "\"");

    Deployer::DeployMode deploy_mode = Deployer::hard_link;
    if(deployers[depl].isMember("use_copy_deployment"))
      deploy_mode =
        deployers[depl]["use_copy_deployment"].asBool() ? Deployer::copy : Deployer::hard_link;
    else
      deploy_mode = static_cast<Deployer::DeployMode>(deployers[depl]["deploy_mode"].asInt());
    deployers_.push_back(
      DeployerFactory::makeDeployer(type,
                                    resolveFromStaging(deployers[depl]["source_path"].asString()),
                                    sfs::path(deployers[depl]["dest_path"].asString()),
                                    deployers[depl]["name"].asString(),
                                    deploy_mode));
    if(deployers[depl].isMember("enable_unsafe_sorting"))
      deployers_.back()->setEnableUnsafeSorting(deployers[depl]["enable_unsafe_sorting"].asBool());

    if(!deployers_[depl]->isAutonomous())
    {
      for(int prof = 0; prof < profile_names_.size(); prof++)
      {
        deployers_[depl]->addProfile();
        deployers_[depl]->setProfile(prof);
        Json::Value loadorder = deployers[depl]["profiles"][prof]["loadorder"];

        // Check if the config is using the new loadorder format or not for backwards compatibility
        if (!loadorder.isNull() && loadorder.isArray())
        {
          for(int mod = 0; mod < loadorder.size(); mod++)
          {
            // Skip malformed legacy entries instead of trusting implicit JSON conversions.
            if(!loadorder[mod].isObject() || !loadorder[mod].isMember("id") ||
               !loadorder[mod].isMember("enabled"))
            {
              log_(Log::LOG_WARNING,
                   "Skipping malformed load order entry in \"" +
                     (staging_dir_ / CONFIG_FILE_NAME).string() + "\"");
              continue;
            }
            int mod_id = loadorder[mod]["id"].asInt();
            if(std::find_if(installed_mods_.begin(),
                            installed_mods_.end(),
                            [mod_id](const Mod& m)
                            { return m.id == mod_id; }) == installed_mods_.end())
              throw ParseError("Unknown mod id in deployers: " + std::to_string(mod_id) + " in \"" +
                              (staging_dir_ / CONFIG_FILE_NAME).string() + "\"");
            // The enclosing `if(!isAutonomous())` already excludes autonomous deployers, so the
            // membership check only needs the group/active-member condition.
            if(!group_map_.contains(mod_id) ||
               active_group_members_[group_map_[mod_id]] == mod_id)
              deployers_[depl]->addMod(mod_id, loadorder[mod]["enabled"].asBool(), false);
          }
        }
        else {
          deployers_[depl]->setLoadorder(loadorder);
        }
        Json::Value conflict_groups_json = deployers[depl]["profiles"][prof]["conflict_groups"];
        std::vector<std::vector<int>> conflict_groups;
        for(int group = 0; group < conflict_groups_json.size(); group++)
        {
          std::vector<int> new_group;
          for(int mod = 0; mod < conflict_groups_json[group].size(); mod++)
            new_group.push_back(conflict_groups_json[group][mod].asInt());
          conflict_groups.push_back(std::move(new_group));
        }
        deployers_[depl]->setConflictGroups(conflict_groups);
      }
    }
    if(type == DeployerFactory::REVERSEDEPLOYER)
    {
      if(deployers[depl].get("update_profiles", false).asBool())
      {
        json_settings_["deployers"][depl]["update_profiles"] = false;
        for(int i = 0; i < profile_names_.size(); i++)
          deployers_[depl]->addProfile();
      }
      auto rev_depl = static_cast<ReverseDeployer*>(deployers_[depl].get());
      if(rev_depl->getNumProfiles() != profile_names_.size())
        throw ParseError(std::format(
          "Mismatch in profile count for deployer '{}'. {} profiles found, expected {}.",
          rev_depl->getName(),
          rev_depl->getNumProfiles(),
          profile_names_.size()));
    }
    deployers_[depl]->setProfile(current_profile_);
  }
  Json::Value tools = json_settings_["tools"];
  for(int tool = 0; tool < tools.size(); tool++)
    tools_.emplace_back(tools[tool]);

  for(int prof = 0; prof < profile_names_.size(); prof++)
    bak_man_.addProfile();
  bak_man_.setProfile(current_profile_);
  Json::Value backup_targets = json_settings_["backup_targets"];
  for(int target = 0; target < backup_targets.size(); target++)
    bak_man_.addTarget(backup_targets[target]["path"].asString());
  bak_man_.setLog(log_);

  if(json_settings_.isMember("manual_tags"))
  {
    for(auto& tag_entry : json_settings_["manual_tags"])
    {
      if(str::find_if(manual_tags_,
                      [name = tag_entry["name"].asString()](auto tag)
                      { return tag.getName() == name; }) != manual_tags_.end())
        throw ParseError(
          std::format("Manual tag \"{}\" found more than once.", tag_entry["name"].asString()));
      manual_tags_.emplace_back(tag_entry);
    }
    updateManualTagMap();
  }

  if(json_settings_.isMember("auto_tags"))
  {
    for(auto& tag_entry : json_settings_["auto_tags"])
    {
      if(str::find_if(auto_tags_,
                      [name = tag_entry["name"].asString()](auto tag)
                      { return tag.getName() == name; }) != auto_tags_.end())
        throw ParseError(
          std::format("Auto tag \"{}\" found more than once.", tag_entry["name"].asString()));
      auto_tags_.emplace_back(tag_entry);
    }
    updateAutoTagMap();
  }

  steam_app_id_ = -1;
  if(json_settings_.isMember("steam_app_id"))
    steam_app_id_ = json_settings_["steam_app_id"].asInt64();
  else
    updateSteamAppId();

  // fork #59: restore the configured vanilla Witcher 3 scripts root (missing => empty).
  tw3_vanilla_scripts_root_ = "";
  if(json_settings_.isMember("tw3_vanilla_scripts_root"))
    tw3_vanilla_scripts_root_ = json_settings_["tw3_vanilla_scripts_root"].asString();

  if(json_settings_.isMember("mod_rules"))
  {
    for(const auto& rule_entry : json_settings_["mod_rules"])
      mod_rules_.emplace_back(rule_entry);
  }

  if(json_settings_.isMember("update_ignore_list"))
  {
    const Json::Value update_ignore_list = json_settings_["update_ignore_list"];
    for(int i = 0; i < update_ignore_list.size(); i++)
      update_ignore_list_.insert(update_ignore_list[i].asInt());
  }

  pre_deploy_hook_ = "";
  post_deploy_hook_ = "";
  pre_undeploy_hook_ = "";
  post_undeploy_hook_ = "";
  if(json_settings_.isMember("hooks"))
  {
    const Json::Value hooks = json_settings_["hooks"];
    if(hooks.isMember("pre_deploy"))
      pre_deploy_hook_ = hooks["pre_deploy"].asString();
    if(hooks.isMember("post_deploy"))
      post_deploy_hook_ = hooks["post_deploy"].asString();
    if(hooks.isMember("pre_undeploy"))
      pre_undeploy_hook_ = hooks["pre_undeploy"].asString();
    if(hooks.isMember("post_undeploy"))
      post_undeploy_hook_ = hooks["post_undeploy"].asString();
  }

  // fork #54: load persisted restore points (backward compatible: missing key = empty).
  restore_points_ = Json::Value(Json::arrayValue);
  if(json_settings_.isMember("restore_points") && json_settings_["restore_points"].isArray())
    restore_points_ = json_settings_["restore_points"];

  updateSteamIconPath();
}

std::string ModdedApplication::getModName(int mod_id) const
{
  auto iter = std::find_if(
    installed_mods_.begin(), installed_mods_.end(), [mod_id](Mod m) { return m.id == mod_id; });
  if(iter == installed_mods_.end())
    return "";
  return iter->name;
}

void ModdedApplication::updateDeployerGroups(std::optional<ProgressNode*> progress_node)
{
  std::vector<std::vector<int>> update_targets;
  for(int depl = 0; depl < deployers_.size(); depl++)
  {
    update_targets.push_back({});
    if(deployers_[depl]->isAutonomous())
      continue;
    for(int profile = 0; profile < profile_names_.size(); profile++)
    {
      deployers_[depl]->setProfile(profile);
      std::vector<bool> completed_groups(active_group_members_.size());
      std::fill(completed_groups.begin(), completed_groups.end(), false);
      for(const auto& entry_weak : *deployers_[depl]->getLoadorder())
      {
        auto entry = entry_weak.lock();
        if(!group_map_.contains(entry->id))
          continue;
        const int group = group_map_[entry->id];
        if(!completed_groups[group])
        {
          completed_groups[group] = true;
          if(deployers_[depl]->swapMod(entry->id, active_group_members_[group]))
            update_targets[depl].push_back(profile);
        }
        else if(deployers_[depl]->removeMod(entry->id))
          update_targets[depl].push_back(profile);
      }
    }
    deployers_[depl]->setProfile(current_profile_);
  }
  if(progress_node)
  {
    std::vector<float> weights;
    for(int depl = 0; depl < update_targets.size(); depl++)
    {
      for(int profile : update_targets[depl])
      {
        deployers_[depl]->setProfile(profile);
        weights.push_back(deployers_[depl]->getNumMods());
      }
      deployers_[depl]->setProfile(current_profile_);
    }
    (*progress_node)->addChildren(weights);
  }
  int i = 0;
  for(int depl = 0; depl < update_targets.size(); depl++)
  {
    for(int profile : update_targets[depl])
    {
      deployers_[depl]->setProfile(profile);
      deployers_[depl]->updateConflictGroups(progress_node ? &(*progress_node)->child(i)
                                                           : std::optional<ProgressNode*>{});
      i++;
    }
    deployers_[depl]->setProfile(current_profile_);
  }
}

void ModdedApplication::splitMod(int mod_id, int deployer)
{
  if(deployers_[deployer]->isAutonomous())
    return;

  std::map<int, sfs::path> managed_sub_dirs;
  for(int i = 0; i < deployers_.size(); i++)
  {
    if(i == deployer || deployers_[i]->isAutonomous())
      continue;
    auto cur_depl_path = deployers_[i]->getDestPath();
    if(!cur_depl_path.ends_with("/"))
      cur_depl_path += "/";
    auto target_depl_path = deployers_[deployer]->getDestPath();
    if(!target_depl_path.ends_with("/"))
      target_depl_path += "/";
    const auto pos = cur_depl_path.find(target_depl_path);
    if(pos != std::string::npos)
    {
      std::string sub_dir = cur_depl_path.substr(pos + target_depl_path.size());
      if(sub_dir.starts_with("/"))
        sub_dir = sub_dir.substr(1);
      managed_sub_dirs[i] = sub_dir;
    }
  }
  if(managed_sub_dirs.empty())
    return;

  for(const auto& [depl, dir] : managed_sub_dirs)
  {
    const auto mod_dir_optional =
      pu::pathExists(dir,
                     staging_dir_ / std::to_string(mod_id),
                     deployers_[deployer]->getType() == DeployerFactory::CASEMATCHINGDEPLOYER);
    if(!mod_dir_optional)
      continue;
    const auto mod_dir = staging_dir_ / std::to_string(mod_id) / mod_dir_optional->string();

    ImportModInfo info;
    info.deployers = { depl };
    info.target_group_id = -1;
    auto iter =
      str::find_if(installed_mods_, [mod_id](const auto& mod) { return mod.id == mod_id; });
    if(iter == installed_mods_.end())
      throw std::runtime_error(std::format("Invalid mod id {}", mod_id));
    info.name = iter->name + " [" + deployers_[depl]->getName() + "]";
    info.version = iter->version;
    info.installer = Installer::SIMPLEINSTALLER;
    info.installer_flags = Installer::Flag::preserve_case | Installer::Flag::preserve_directories;
    info.files = {};
    info.root_level = 0;
    info.current_path = mod_dir;
    info.local_source = iter->local_source;
    info.remote_source = iter->remote_source;
    info.remote_mod_id = iter->remote_mod_id;
    info.remote_file_id = iter->remote_file_id;
    info.remote_type = iter->remote_type;

    log_(
      Log::LOG_WARNING,
      std::format(
        "Mod '{}' has been split because it contains" " a sub-directory managed by deployer '{}'.",
        iter->name,
        deployers_[depl]->getName()));
    installMod(info);
    sfs::remove_all(mod_dir);
  }
}

void ModdedApplication::replaceMod(const ImportModInfo& info)
{
  if(!info.replace_mod || info.target_group_id == -1)
  {
    installMod(info);
    return;
  }
  auto index =
    str::find_if(installed_mods_, [group = info.target_group_id](const Mod& m) { return m.id == group; });
  if(index == installed_mods_.end())
    throw std::runtime_error(std::format("Invalid group '{}' for mod '{}'", info.target_group_id, info.name));

  int mod_id = 0;
  if(!installed_mods_.empty())
    mod_id = std::max_element(installed_mods_.begin(), installed_mods_.end())->id + 1;
  while(pu::exists(staging_dir_ / std::to_string(mod_id)) &&
        mod_id < std::numeric_limits<int>().max())
    mod_id++;
  if(mod_id == std::numeric_limits<int>().max())
    throw std::runtime_error("Error: Could not generate new mod id.");
  const sfs::path tmp_replace_dir =
    staging_dir_ / (std::string("tmp_replace_") + std::to_string(mod_id));

  const auto mod_size = Installer::install(info.current_path,
                                           tmp_replace_dir,
                                           info.installer_flags,
                                           info.installer,
                                           info.root_level,
                                           info.files);
  const sfs::path old_mod_path = staging_dir_ / std::to_string(info.target_group_id);
  sfs::remove_all(old_mod_path);
  sfs::rename(tmp_replace_dir, old_mod_path);

  index->name = info.name;
  index->version = info.version;
  index->remote_source = info.remote_source;
  index->local_source = info.local_source;
  index->install_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  index->remote_update_time = index->install_time;
  index->size_on_disk = mod_size;
  index->remote_mod_id = info.remote_mod_id;
  index->remote_file_id = info.remote_file_id;
  index->remote_type = info.remote_type;

  std::vector<float> weights_profiles;
  std::vector<float> weights_mods;
  std::vector<std::vector<int>> update_targets;
  for(int depl = 0; depl < deployers_.size(); depl++)
  {
    bool was_split = false;
    update_targets.push_back({});
    if(deployers_[depl]->hasMod(info.target_group_id))
      weights_mods.push_back(deployers_[depl]->getNumMods());
    else
      weights_mods.push_back(0);
    if(deployers_[depl]->isAutonomous())
      continue;
    for(int prof = 0; prof < profile_names_.size(); prof++)
    {
      deployers_[depl]->setProfile(prof);
      if(deployers_[depl]->hasMod(info.target_group_id))
      {
        update_targets[depl].push_back(prof);
        weights_profiles.push_back(deployers_[depl]->getNumMods());
        if(!was_split)
        {
          was_split = true;
          splitMod(info.target_group_id, depl);
        }
      }
    }
    deployers_[depl]->setProfile(current_profile_);
  }

  ProgressNode node(progress_callback_, { 10.0f, 6.0f });
  node.child(0).addChildren(weights_mods);
  node.child(1).addChildren(weights_profiles);
  int i = 0;
  for(int depl = 0; depl < update_targets.size(); depl++)
  {
    deployers_[depl]->updateDeployedFilesForMod(info.target_group_id, &node.child(0).child(depl));
    for(int prof : update_targets[depl])
    {
      deployers_[depl]->setProfile(prof);
      deployers_[depl]->updateConflictGroups(&node.child(1).child(i));
      i++;
    }
    deployers_[depl]->setProfile(current_profile_);
  }

  for(auto& tag : auto_tags_)
    tag.updateMods(staging_dir_, std::vector<int>{ info.target_group_id });
  updateAutoTagMap();

  updateSettings(true);
}

void ModdedApplication::updateManualTagMap()
{
  manual_tag_map_.clear();
  for(const auto& mod : installed_mods_)
    manual_tag_map_[mod.id] = {};
  for(const auto& tag : manual_tags_)
  {
    for(int mod_id : tag.getMods())
      manual_tag_map_[mod_id].push_back(tag.getName());
  }
}

void ModdedApplication::updateAutoTagMap()
{
  auto_tag_map_.clear();
  for(const auto& mod : installed_mods_)
    auto_tag_map_[mod.id] = {};
  for(const auto& tag : auto_tags_)
  {
    for(int mod_id : tag.getMods())
      auto_tag_map_[mod_id].push_back(tag.getName());
  }
}

void ModdedApplication::performUpdateCheck(const std::vector<int>& target_mod_indices)
{
  if(target_mod_indices.empty())
  {
    log_(Log::LOG_INFO, "None of the selected mods has a valid remote source.");
    return;
  }
  log_(Log::LOG_INFO,
       std::format("Checking for updates for {} mod{}...",
                   target_mod_indices.size(),
                   target_mod_indices.size() > 1 ? "s" : ""));
  ProgressNode node(progress_callback_);
  node.setTotalSteps(target_mod_indices.size());
  int num_available_updates = 0;
  for(int i : target_mod_indices)
  {
    // Mods explicitly marked update-ignored (limo-app/limo#144) are skipped entirely so their
    // remote_update_time is never advanced and they never surface as out of date.
    if(isUpdateIgnored(installed_mods_[i].id))
    {
      node.advance();
      continue;
    }
    const auto remote_mod = nexus::Api::getNexusPage(installed_mods_[i].remote_source).mod;
    installed_mods_[i].remote_update_time = remote_mod.updated_time;
    // A pinned mod stays quiet while the remote version still matches the pin; once the remote
    // publishes a different (newer) version the normal timestamp gate surfaces it again
    // (limo-app/limo: pins must not silence genuinely newer releases).
    if(!installed_mods_[i].pinned_version.empty() &&
       remote_mod.version == installed_mods_[i].pinned_version)
      installed_mods_[i].remote_update_time = installed_mods_[i].install_time;
    if(installed_mods_[i].remote_update_time > installed_mods_[i].install_time)
      num_available_updates++;
    node.advance();
  }
  if(num_available_updates > 0)
    log_(Log::LOG_INFO,
         std::format("Found updates for {} mod{}.",
                     num_available_updates,
                     num_available_updates == 1 ? "" : "s"));
  else
    log_(Log::LOG_INFO, "No mod updates found.");
  updateSettings(true);
}

std::string ModdedApplication::relativizeToStaging(const sfs::path& path) const
{
  std::error_code ec;
  const sfs::path canonical_staging = sfs::weakly_canonical(staging_dir_, ec);
  const sfs::path base = ec ? staging_dir_ : canonical_staging;
  const sfs::path canonical_path = sfs::weakly_canonical(path, ec);
  const sfs::path target = ec ? path : canonical_path;

  const std::string base_str = base.string();
  const std::string target_str = target.string();
  if(base_str.empty() || target_str.size() < base_str.size() ||
     target_str.compare(0, base_str.size(), base_str) != 0)
    return path.string();
  // Only treat as relative if the staging prefix ends on a path boundary.
  if(target_str.size() > base_str.size() && target_str[base_str.size()] != '/')
    return path.string();

  std::string remainder = target_str.substr(base_str.size());
  return STAGING_TOKEN + remainder;
}

sfs::path ModdedApplication::resolveFromStaging(const std::string& path) const
{
  if(!path.starts_with(STAGING_TOKEN))
    return sfs::path(path);
  std::string remainder = path.substr(STAGING_TOKEN.size());
  if(!remainder.empty() && remainder.front() == '/')
    remainder.erase(0, 1);
  if(remainder.empty())
    return staging_dir_;
  return staging_dir_ / remainder;
}

std::string ModdedApplication::generalizeSteamPath(const std::string& path) const
{
  std::string modified_path = path;
  std::regex install_regex(R"((\/.*\/steamapps\/common\/.*?)(?:\/.*)?)");
  std::regex prefix_regex(
    R"((\/.*\/steamapps\/compatdata\/\d+\/pfx\/(?:drive_c|dosdevices\/c:))(?:\/.*)?)");
  std::regex home_regex(R"(((?:\/home\/.+?)|~)(?:\/.*)?)");
  std::smatch match;
  if(std::regex_match(path, match, install_regex))
    modified_path.replace(0, match[1].length(), "$STEAM_INSTALL_PATH$");
  else if(std::regex_match(path, match, prefix_regex))
    modified_path.replace(0, match[1].length(), "$STEAM_PREFIX_PATH$");
  else if(std::regex_match(path, match, home_regex))
    modified_path.replace(0, match[1].length(), "$HOME$");
  return modified_path;
}

void ModdedApplication::updateSteamIconPath()
{
  std::regex old_path_regex(R"((.*?/steam/appcache/librarycache)/(\d+)_icon\.jpg)");
  std::smatch match;
  std::string path_str = icon_path_.string();
  std::regex_match(path_str, match, old_path_regex);
  if(match.empty() || sfs::exists(icon_path_))
    return;

  sfs::path steam_path(match[1].str());
  steam_path /= match[2].str();
  if(!sfs::exists(steam_path))
    return;

  std::regex name_regex(R"(([0-9a-fA-F]{40})\.jpg)");
  for(const auto& dir_entry : sfs::directory_iterator(steam_path))
  {
    const std::string file_name = dir_entry.path().filename();
    if(std::regex_match(file_name, name_regex))
    {
      icon_path_ = steam_path / file_name;
      updateSettings(true);
      return;
    }
  }
}

void ModdedApplication::updateSteamAppId()
{
  if(steam_app_id_ != -1)
    return;

  std::regex old_path_regex(R"(.*?/steam/appcache/librarycache/(\d+)_icon\.jpg)");
  std::smatch match;
  std::string path_str = icon_path_.string();
  if(std::regex_match(path_str, match, old_path_regex))
  {
    try
    {
      steam_app_id_ = std::stol(match[1]);
    }
    catch(const std::exception& e)
    {
      steam_app_id_ = -1;
    }
    return;
  }

  std::regex new_path_regex(R"(.*?/steam/appcache/librarycache/(\d+)/.*)");
  if(std::regex_match(path_str, match, new_path_regex))
  {
    try
    {
      steam_app_id_ = std::stol(match[1]);
    }
    catch(const std::exception& e)
    {
      steam_app_id_ = -1;
    }
    return;
  }

  std::regex steam_regex(R"(/steamapps/compatdata/(\d+))");
  for(const auto& depl : deployers_)
  {
    std::smatch match;
    std::string path = depl->getDestPath();
    if(std::regex_search(path, match, steam_regex))
    {
      try
      {
        steam_app_id_ = std::stol(match[1]);
      }
      catch(const std::exception& e)
      {
        continue;
      }
      return;
    }
  }
}

std::unordered_set<int> ModdedApplication::getEnabledModIds() const
{
  std::unordered_set<int> enabled;
  for(const auto& depl : deployers_)
  {
    if(depl->isAutonomous())
      continue;
    for(const auto& entry_weak : *depl->getLoadorder())
    {
      auto entry = std::static_pointer_cast<DeployerModInfo>(entry_weak.lock());
      if(!entry || entry->isSeparator)
        continue;
      if(entry->enabled)
        enabled.insert(entry->id);
    }
  }
  return enabled;
}

std::unordered_set<int> ModdedApplication::getDeployedModIds() const
{
  std::unordered_set<int> present;
  for(const auto& depl : deployers_)
  {
    if(depl->isAutonomous())
      continue;
    for(const auto& entry_weak : *depl->getLoadorder())
    {
      auto entry = std::static_pointer_cast<DeployerModInfo>(entry_weak.lock());
      if(!entry || entry->isSeparator)
        continue;
      present.insert(entry->id);
    }
  }
  return present;
}

const std::vector<ModRule>& ModdedApplication::getModRules() const
{
  return mod_rules_;
}

std::vector<ModRule> ModdedApplication::getModRulesFor(int source_mod_id) const
{
  std::vector<ModRule> result;
  for(const auto& rule : mod_rules_)
  {
    if(rule.source_mod_id == source_mod_id)
      result.push_back(rule);
  }
  return result;
}

void ModdedApplication::addModRule(const ModRule& rule)
{
  if(str::find(mod_rules_, rule) != mod_rules_.end())
    return;
  mod_rules_.push_back(rule);
  updateSettings(true);
}

void ModdedApplication::removeModRule(const ModRule& rule)
{
  auto it = str::find(mod_rules_, rule);
  if(it == mod_rules_.end())
    return;
  mod_rules_.erase(it);
  updateSettings(true);
}

void ModdedApplication::setModRulesFor(int source_mod_id, const std::vector<ModRule>& rules)
{
  std::erase_if(mod_rules_, [source_mod_id](const ModRule& r)
  {
    return r.source_mod_id == source_mod_id;
  });
  for(const auto& rule : rules)
    mod_rules_.push_back(rule);
  updateSettings(true);
}

std::string ModdedApplication::checkModRules() const
{
  if(mod_rules_.empty())
    return {};

  const auto enabled = getEnabledModIds();
  const auto present = getDeployedModIds();

  std::string warnings;

  for(const auto& rule : mod_rules_)
  {
    // Only evaluate rules whose source mod is currently enabled.
    if(!enabled.contains(rule.source_mod_id))
      continue;

    const std::string source_name = getModName(rule.source_mod_id);
    const std::string target_name = getModName(rule.target_mod_id);
    const std::string target_label = target_name.empty()
                                       ? "(id " + std::to_string(rule.target_mod_id) + ")"
                                       : "\"" + target_name + "\"";

    if(rule.type == RuleType::requires_mod)
    {
      // Violation: target is absent or disabled.
      if(!enabled.contains(rule.target_mod_id))
      {
        const bool absent = !present.contains(rule.target_mod_id);
        warnings += std::format("  - \"{}\" requires {} which is {}.\n",
                                source_name,
                                target_label,
                                absent ? "not installed in any deployer" : "disabled");
      }
    }
    else // conflicts_with
    {
      // Violation: both source and target are enabled.
      if(enabled.contains(rule.target_mod_id))
      {
        warnings += std::format("  - \"{}\" conflicts with {} but both are enabled.\n",
                                source_name,
                                target_label);
      }
    }
  }

  if(warnings.empty())
    return {};

  return "Mod rule violations detected before deployment:\n" + warnings +
         "Deployment will proceed; resolve conflicts manually.";
}

std::vector<std::pair<int, sfs::path>> ModdedApplication::getEnabledModPathsInLoadOrder(
  int deployer) const
{
  std::vector<std::pair<int, sfs::path>> result;
  if(deployer < 0 || deployer >= static_cast<int>(deployers_.size()))
    return result;
  for(const auto& entry_weak : *deployers_[deployer]->getLoadorder())
  {
    auto entry = std::static_pointer_cast<DeployerModInfo>(entry_weak.lock());
    if(!entry || entry->isSeparator || !entry->enabled)
      continue;
    result.emplace_back(entry->id, staging_dir_ / std::to_string(entry->id));
  }
  return result;
}

std::string ModdedApplication::mergeTw3Scripts(int deployer)
{
  if(deployer < 0 || deployer >= static_cast<int>(deployers_.size()))
    return "Invalid deployer.";
  const auto mods = getEnabledModPathsInLoadOrder(deployer);
  if(mods.empty())
    return "No enabled mods to merge scripts for.";
  const sfs::path output_dir = staging_dir_ / "tw3_merged_scripts";
  // fork #59: use the configured vanilla scripts root as the 3-way merge base when it is set and
  // present on disk; otherwise fall back to the current 2-way merge.
  std::optional<sfs::path> vanilla_root;
  if(!tw3_vanilla_scripts_root_.empty() && sfs::exists(sfs::path(tw3_vanilla_scripts_root_)))
    vanilla_root = sfs::path(tw3_vanilla_scripts_root_);
  const auto result = tw3_script_merge::mergeScripts(mods, output_dir, vanilla_root);
  if(result.scripts_merged == 0)
    return "No scripts are shared between two or more enabled mods; nothing to merge.";
  std::string msg =
    std::format("Merged {} conflicting script(s): {} auto-resolved, {} need manual review.\n\n"
                "Merged scripts written to:\n{}\n",
                result.scripts_merged,
                result.conflicts_auto_resolved,
                result.conflicts_unresolved,
                output_dir.string());
  if(!result.unresolved_paths.empty())
  {
    msg += "\nScripts containing conflict markers (resolve by hand):\n";
    for(const auto& p : result.unresolved_paths)
      msg += "  - " + p + "\n";
  }
  msg += "\nNote: experimental. The merged-scripts folder must be added as a high-priority mod "
         "for the game to use it, and no vanilla script base was supplied.";
  return msg;
}

// fork #59: configured vanilla Witcher 3 scripts root accessors.
void ModdedApplication::setTw3VanillaScriptsRoot(const std::string& path)
{
  tw3_vanilla_scripts_root_ = path;
  updateSettings(true);
}

std::string ModdedApplication::getTw3VanillaScriptsRoot() const
{
  return tw3_vanilla_scripts_root_;
}

std::string ModdedApplication::mergeTw3Config(int deployer)
{
  if(deployer < 0 || deployer >= static_cast<int>(deployers_.size()))
    return "Invalid deployer.";
  const auto mods = getEnabledModPathsInLoadOrder(deployer);
  if(mods.empty())
    return "No enabled mods to merge config for.";
  std::vector<Tw3MergeUtil::MergeSource> sources;
  for(const auto& [id, path] : mods)
    sources.push_back(Tw3MergeUtil::MergeSource{ id, path });
  const sfs::path game_root = deployers_[deployer]->getDestPath();
  const auto result = Tw3MergeUtil::mergeInputXml(game_root, sources);
  std::string msg = std::format("input.xml merge: {}\n  mods merged: {}, entries added: {}, "
                                "file {}.\n",
                                result.success ? "ok" : "failed",
                                result.mods_merged,
                                result.entries_merged,
                                result.changed ? "updated" : "unchanged");
  if(!result.message.empty())
    msg += "  " + result.message + "\n";
  msg += "\nNote: experimental. Only input.xml is merged; the *.settings files live in the "
         "Proton Documents tree and are not auto-located yet.";
  return msg;
}

std::string ModdedApplication::getCyberpunkSetupInfo(int deployer)
{
  std::string msg = "Cyberpunk 2077 mod setup checklist:\n";
  for(const auto& item : cyberpunk_setup::setupChecklist())
    msg += "  - " + item + "\n";
  msg += std::format("\nRequired Steam launch options:\n  {}\n",
                     cyberpunk_setup::REQUIRED_LAUNCH_OPTIONS);
  msg += std::format("\nInstall required dependencies in the Proton prefix with:\n  {}\n",
                     cyberpunk_setup::protontricksCommand());
  if(deployer >= 0 && deployer < static_cast<int>(deployers_.size()))
  {
    const sfs::path target = deployers_[deployer]->getDestPath();
    const int mode = static_cast<int>(deployers_[deployer]->getDeployMode());
    const auto warning = cyberpunk_setup::checkCyberpunkDeployment(mode, staging_dir_, target);
    if(warning)
      msg += "\nWARNING about this deployer's deploy mode:\n  " + *warning + "\n";
    else
      msg += "\nThis deployer's deploy mode looks safe for CET/RED4ext.\n";
  }
  return msg;
}

std::string ModdedApplication::buildRedmodDeployCommand(int deployer)
{
  if(deployer < 0 || deployer >= static_cast<int>(deployers_.size()))
    return {};
  const auto mods = getEnabledModPathsInLoadOrder(deployer);
  const sfs::path game_root = deployers_[deployer]->getDestPath();
  std::vector<std::pair<int, sfs::path>> redmod_sources;
  std::vector<std::string> names;
  for(const auto& [id, path] : mods)
  {
    const auto redmod = cyberpunk_redmod::parseRedMod(path);
    if(redmod)
    {
      redmod_sources.emplace_back(id, path);
      names.push_back(redmod->name);
    }
  }
  if(redmod_sources.empty())
    return {};
  cyberpunk_redmod::layoutRedMods(redmod_sources, game_root);
  return cyberpunk_redmod::redmodDeployCommand(game_root, names, {});
}

void ModdedApplication::autoAddScriptExtenderTools(int mod_id)
{
  // Well known script extender / launcher executables (lower case for case insensitive match).
  static const std::vector<std::string> loader_names{ "skse64_loader.exe", "skse_loader.exe",
                                                      "f4se_loader.exe",   "fnvse_loader.exe",
                                                      "fose_loader.exe",   "obse_loader.exe",
                                                      "obse64_loader.exe", "nvse_loader.exe" };
  try
  {
    const sfs::path mod_dir = staging_dir_ / std::to_string(mod_id);
    if(!sfs::exists(mod_dir))
      return;

    for(const auto& entry : sfs::recursive_directory_iterator(mod_dir))
    {
      if(!entry.is_regular_file())
        continue;
      std::string file_name = entry.path().filename().string();
      std::string lower_name = file_name;
      std::transform(
        lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) {
          return std::tolower(c);
        });
      if(str::find(loader_names, lower_name) == loader_names.end())
        continue;

      const sfs::path exe_path = entry.path();

      // Avoid adding the same executable twice (compare by file name, case insensitive).
      bool already_present = false;
      for(const auto& tool : tools_)
      {
        std::string existing = tool.getExecutablePath().filename().string();
        std::transform(existing.begin(), existing.end(), existing.begin(), [](unsigned char c) {
          return std::tolower(c);
        });
        if(existing == lower_name)
        {
          already_present = true;
          break;
        }
      }
      if(already_present)
        continue;

      // Mirror the configuration of an existing tool for this app where possible so the new
      // tool uses the correct runtime / wine prefix / proton app id. The required wine prefix
      // is only known to the UI, so fall back to a best effort wine tool with an empty prefix
      // (the user can adjust the prefix afterwards if needed).
      const Tool* template_tool = nullptr;
      for(const auto& tool : tools_)
      {
        if(tool.getRuntime() == Tool::wine || tool.getRuntime() == Tool::protontricks)
        {
          template_tool = &tool;
          break;
        }
      }

      const std::string tool_name = exe_path.stem().string();
      if(template_tool != nullptr && template_tool->getRuntime() == Tool::protontricks)
      {
        tools_.emplace_back(tool_name,
                            icon_path_,
                            exe_path,
                            template_tool->usesFlatpakRuntime(),
                            template_tool->getSteamAppId(),
                            exe_path.parent_path(),
                            std::map<std::string, std::string>{},
                            std::string{},
                            template_tool->getProtontricksArguments());
      }
      else
      {
        const sfs::path prefix_path =
          template_tool != nullptr ? template_tool->getPrefixPath() : sfs::path{};
        tools_.emplace_back(tool_name,
                            icon_path_,
                            exe_path,
                            prefix_path,
                            exe_path.parent_path(),
                            std::map<std::string, std::string>{},
                            std::string{});
      }
      log_(Log::LOG_INFO,
           std::format("Automatically added script extender tool '{}' for '{}'.",
                       tool_name,
                       file_name));
    }
  }
  catch(const std::exception& e)
  {
    // Never let tool detection break a successful mod install.
    log_(Log::LOG_WARNING,
         std::format("Failed to auto-add script extender tools: {}", e.what()));
  }
}
