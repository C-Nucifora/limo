#include "cyberpunkdeployer.h"
#include "pathutils.h"
#include <algorithm>
#include <format>
#include <ranges>
#include <string_view>
#include <utility>

namespace sfs = std::filesystem;
namespace str = std::ranges;
namespace pu = path_utils;


CyberpunkDeployer::CyberpunkDeployer(const sfs::path& source_path,
                                     const sfs::path& dest_path,
                                     const std::string& name,
                                     DeployMode deploy_mode) :
  CaseMatchingDeployer(source_path, dest_path, name, deploy_mode)
{
  type_ = "Cyberpunk 2077 Deployer";
}

std::map<int, unsigned long> CyberpunkDeployer::deploy(const std::vector<int>& loadorder,
                                                       std::optional<ProgressNode*> progress_node)
{
  // Mirror CaseMatchingDeployer::deploy: run the case matching rename pass and update conflict
  // groups, then perform the archive-aware deployment in place of Deployer::deploy. The progress
  // node is split the same way (rename, conflicts, deploy).
  if(progress_node)
    (*progress_node)->addChildren({ 2, 1, 3 });
  adaptLoadorderFiles(loadorder,
                      progress_node ? &(*progress_node)->child(0) : std::optional<ProgressNode*>{});
  updateConflictGroups(progress_node ? &(*progress_node)->child(1)
                                     : std::optional<ProgressNode*>{});

  // Build the destination keyed deployment maps (with .archive prefixes applied) and drive the
  // same primitives Deployer::deploy uses, so backup/restore and the .lmmfiles manifest stay
  // consistent. The maps are keyed by destination path, which is exactly what loadDeployedFiles,
  // backupOrRestoreFiles and saveDeployedFiles operate on.
  auto [source_files, source_paths, mod_sizes] = getDeploymentMaps(loadorder);
  log_(Log::LOG_INFO,
       std::format("Deployer '{}': Deploying {} files for {} mods...",
                   name_,
                   source_files.size(),
                   loadorder.size()));

  std::optional<ProgressNode*> deploy_node =
    progress_node ? std::optional<ProgressNode*>(&(*progress_node)->child(2))
                  : std::optional<ProgressNode*>{};
  if(deploy_node)
    (*deploy_node)->addChildren({ 2, 5, 1 });
  std::map<sfs::path, int> dest_files = loadDeployedFiles(
    deploy_node ? &(*deploy_node)->child(0) : std::optional<ProgressNode*>{});
  backupOrRestoreFiles(source_files, dest_files);
  deployFilesWithRemap(source_files,
                       source_paths,
                       deploy_node ? &(*deploy_node)->child(1) : std::optional<ProgressNode*>{});
  saveDeployedFiles(source_files,
                    deploy_node ? &(*deploy_node)->child(2) : std::optional<ProgressNode*>{});
  // The deployed set just changed, so re-check that every framework an enabled mod relies on is
  // actually installed and warn about any that are missing. The load order passed here is exactly
  // the set of enabled mods being deployed.
  warnAboutMissingFrameworks(loadorder);
  return mod_sizes;
}

bool CyberpunkDeployer::addMod(int mod_id, bool enabled, bool update_conflicts)
{
  const bool added = CaseMatchingDeployer::addMod(mod_id, enabled, update_conflicts);
  // Only the addition of an enabled mod can introduce a new (possibly unsatisfied) framework
  // dependency, so re-running the check on a no-op or a disabled add would be wasted work.
  if(added && enabled)
    warnAboutMissingFrameworks(getEnabledModIds());
  return added;
}

bool CyberpunkDeployer::isOrderedArchive(const sfs::path& relative_path) const
{
  // Must be a file directly inside archive/pc/mod/ (not in a deeper sub directory) with the
  // .archive extension. Comparison is case insensitive to be robust against odd packaging.
  if(pu::toLowerCase(relative_path.extension()) != ARCHIVE_EXTENSION)
    return false;
  return pu::toLowerCase(relative_path.parent_path()) == pu::toLowerCase(ARCHIVE_MOD_DIR);
}

sfs::path CyberpunkDeployer::destinationPath(const sfs::path& relative_path,
                                             int loadorder_index) const
{
  if(!isOrderedArchive(relative_path))
    return relative_path;
  // Prepend a zero padded load order prefix to the file name only; keep the directory intact.
  // TODO(cp2077): load orders with more than 9999 mods would break the fixed 4 digit width; this
  // is far beyond any realistic Cyberpunk setup, so a wider field is not currently warranted.
  const std::string prefixed_name =
    std::format("{:04d}_{}", loadorder_index, relative_path.filename().string());
  return relative_path.parent_path() / prefixed_name;
}

std::tuple<std::map<sfs::path, int>, std::map<sfs::path, sfs::path>, std::map<int, unsigned long>>
CyberpunkDeployer::getDeploymentMaps(const std::vector<int>& loadorder) const
{
  std::map<sfs::path, int> source_files{};
  std::map<sfs::path, sfs::path> source_paths{};
  std::map<int, unsigned long> mod_sizes{};
  // Iterate from the back of the load order to the front, mirroring
  // Deployer::getDeploymentSourceFilesAndModSizes. std::map::insert keeps the first inserted value
  // for a given key, so for files that collide on the same destination path the mod earlier in the
  // iteration (later in the load order) wins. This preserves the base class semantics of "later
  // mods overwrite earlier mods" for all non archive files. Ordering sensitive .archive files
  // never collide here because each receives a unique, index derived file name prefix.
  for(int i = loadorder.size() - 1; i >= 0; i--)
  {
    if(!checkModPathExistsAndMaybeLogError(loadorder[i]))
      continue;
    const sfs::path mod_base_path = source_path_ / std::to_string(loadorder[i]);
    unsigned long mod_size = 0;
    for(const auto& dir_entry : sfs::recursive_directory_iterator(mod_base_path))
    {
      // Security: never deploy symlinks (a mod could link to a file outside its directory).
      if(dir_entry.is_symlink())
        continue;
      const bool is_regular_file = dir_entry.is_regular_file();
      if(is_regular_file)
        mod_size += dir_entry.file_size();
      if(!is_regular_file && !dir_entry.is_directory())
        continue;
      const sfs::path relative_path = pu::getRelativePath(dir_entry.path(), mod_base_path);
      const sfs::path dest_relative_path = destinationPath(relative_path, i);
      if(source_files.insert({ dest_relative_path, loadorder[i] }).second)
        source_paths[dest_relative_path] = relative_path;
    }
    mod_sizes[loadorder[i]] = mod_size;
  }
  return { source_files, source_paths, mod_sizes };
}

void CyberpunkDeployer::deployFilesWithRemap(
  const std::map<sfs::path, int>& dest_files,
  const std::map<sfs::path, sfs::path>& source_paths,
  std::optional<ProgressNode*> progress_node) const
{
  // Mirrors Deployer::deployFiles but resolves the source through source_paths, because the
  // destination relative path may differ from the source relative path (archive prefixing).
  if(progress_node)
    (*progress_node)->setTotalSteps(dest_files.size());

  for(const auto& [dest_relative_path, id] : dest_files)
  {
    const sfs::path dest_path = dest_path_ / dest_relative_path;
    if(!checkModPathExistsAndMaybeLogError(id))
    {
      // Advance on this skip too, so the progress bar still reaches 100% when a mod's source path
      // is missing (mirrors Tw3Deployer::deployFilesWithPathMap) (limo-app/limo: progress stall).
      if(progress_node)
        (*progress_node)->advance();
      continue;
    }
    const auto source_iter = source_paths.find(dest_relative_path);
    if(source_iter == source_paths.end())
    {
      // The dest->source maps are built in lock-step, so this should be unreachable. Skip rather
      // than silently read from the (prefixed) destination path, which would not exist on disk.
      log_(Log::LOG_DEBUG,
           std::format("Deployer '{}': no source mapping for deployed path '{}'; skipping.",
                       name_,
                       dest_relative_path.string()));
      continue;
    }
    const sfs::path source_path = source_path_ / std::to_string(id) / source_iter->second;
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
    sfs::remove(dest_path);
    try
    {
      if(deploy_mode_ == copy)
        sfs::copy_file(source_path, dest_path);
      else if(deploy_mode_ == sym_link)
        sfs::create_symlink(source_path, dest_path);
      else
        sfs::create_hard_link(source_path, dest_path);
    }
    catch(const sfs::filesystem_error& e)
    {
      // Hard links can't span filesystems; under Flatpak the sandbox can also place the staging and
      // target dirs on different mounts. Give actionable guidance instead of the raw errno
      // (limo-app/limo#13, #143). Mirrors Deployer::deployFiles.
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

void CyberpunkDeployer::scanFile(const sfs::path& relative_path,
                                 std::array<bool, NUM_FRAMEWORKS>& required,
                                 std::array<bool, NUM_FRAMEWORKS>& installed) const
{
  // Match everything case insensitively against the generic (forward slash) form of the path, which
  // is how Cyberpunk's own directory layout is written and how mods are packaged.
  const std::string path = pu::toLowerCase(relative_path.generic_string());
  const auto starts_with = [&path](std::string_view prefix)
  { return path.rfind(prefix, 0) == 0; };
  const auto ends_with = [&path](std::string_view suffix)
  { return path.size() >= suffix.size() && path.compare(path.size() - suffix.size(),
                                                        suffix.size(),
                                                        suffix) == 0; };

  // --- Installed: signature files shipped by each framework. Checked first because some signature
  // files would otherwise also look like a generic dependency-implying file (e.g. a RED4ext plugin
  // dll). ---
  if(path == "bin/x64/plugins/cyber_engine_tweaks.asi")
    installed[CET] = true;
  else if(path == "red4ext/red4ext.dll")
    installed[RED4EXT] = true;
  else if(path == "engine/tools/scc.exe")
    installed[REDSCRIPT] = true;
  else if(starts_with("red4ext/plugins/archivexl/"))
  {
    // ArchiveXL and TweakXL are themselves RED4ext plugins, so their presence also satisfies
    // RED4ext (additionally normalised in warnAboutMissingFrameworks for robustness).
    installed[ARCHIVEXL] = true;
    installed[RED4EXT] = true;
  }
  else if(starts_with("red4ext/plugins/tweakxl/"))
  {
    installed[TWEAKXL] = true;
    installed[RED4EXT] = true;
  }

  // --- Required: kinds of files that imply a dependency on a framework. ---
  if(starts_with("r6/scripts/") && ends_with(".reds"))
    required[REDSCRIPT] = true;
  else if(starts_with("r6/tweaks/") &&
          (ends_with(".yaml") || ends_with(".yml") || ends_with(".tweak")))
    required[TWEAKXL] = true;
  else if(ends_with(".xl"))
    required[ARCHIVEXL] = true;
  else if(starts_with("red4ext/plugins/"))
    required[RED4EXT] = true;
  else if(starts_with("bin/x64/plugins/cyber_engine_tweaks/mods/"))
    required[CET] = true;
}

std::pair<std::array<bool, CyberpunkDeployer::NUM_FRAMEWORKS>,
          std::array<bool, CyberpunkDeployer::NUM_FRAMEWORKS>>
CyberpunkDeployer::scanFrameworks(const std::vector<int>& mod_ids) const
{
  std::array<bool, NUM_FRAMEWORKS> required{};
  std::array<bool, NUM_FRAMEWORKS> installed{};
  // Inspect the source files of every given mod. Source enumeration mirrors getDeploymentMaps so
  // the result reflects exactly what would be deployed, independent of any actual deployment.
  for(int id : mod_ids)
  {
    if(!checkModPathExistsAndMaybeLogError(id))
      continue;
    const sfs::path mod_base_path = source_path_ / std::to_string(id);
    for(const auto& dir_entry : sfs::recursive_directory_iterator(mod_base_path))
    {
      if(dir_entry.is_symlink() || !dir_entry.is_regular_file())
        continue;
      scanFile(pu::getRelativePath(dir_entry.path(), mod_base_path), required, installed);
    }
  }
  return { required, installed };
}

std::vector<int> CyberpunkDeployer::getEnabledModIds()
{
  std::vector<int> mod_ids{};
  for(const auto& entry_weak : getLoadorder()->getTraversalItems())
  {
    const auto entry = std::static_pointer_cast<DeployerModInfo>(entry_weak.lock());
    if(entry && !entry->isSeparator && entry->enabled)
      mod_ids.push_back(entry->id);
  }
  return mod_ids;
}

std::vector<std::string> CyberpunkDeployer::getMissingFrameworks() const
{
  // Reading the load order tree only refreshes an internal cache, so gathering the enabled mods is
  // logically const; the const_cast keeps the public query const as its documented contract.
  const std::vector<int> mod_ids = const_cast<CyberpunkDeployer*>(this)->getEnabledModIds();
  auto [required, installed] = scanFrameworks(mod_ids);
  // ArchiveXL and TweakXL both ship as RED4ext plugins, so either one being installed means RED4ext
  // is present too, even if red4ext/red4ext.dll is not managed through Limo.
  if(installed[ARCHIVEXL] || installed[TWEAKXL])
    installed[RED4EXT] = true;

  std::vector<std::string> missing{};
  for(int i = 0; i < NUM_FRAMEWORKS; i++)
  {
    if(required[i] && !installed[i])
      missing.push_back(FRAMEWORK_NAMES[i]);
  }
  return missing;
}

void CyberpunkDeployer::warnAboutMissingFrameworks(const std::vector<int>& mod_ids) const
{
  auto [required, installed] = scanFrameworks(mod_ids);
  if(installed[ARCHIVEXL] || installed[TWEAKXL])
    installed[RED4EXT] = true;

  for(int i = 0; i < NUM_FRAMEWORKS; i++)
  {
    if(required[i] && !installed[i])
      log_(Log::LOG_WARNING,
           std::format("Deployer '{}': One or more enabled mods require the '{}' framework, but it "
                       "does not appear to be installed. Such mods will not work until it is added.",
                       name_,
                       FRAMEWORK_NAMES[i]));
  }
}
