#include "tw3deployer.h"
#include "pathutils.h"
#include <algorithm>
#include <format>

namespace sfs = std::filesystem;
namespace pu = path_utils;


Tw3Deployer::Tw3Deployer(const sfs::path& source_path,
                         const sfs::path& dest_path,
                         const std::string& name,
                         DeployMode deploy_mode) :
  CaseMatchingDeployer(source_path, dest_path, name, deploy_mode)
{
  type_ = "Witcher 3 Deployer";
}

std::map<int, unsigned long> Tw3Deployer::deploy(const std::vector<int>& loadorder,
                                                 std::optional<ProgressNode*> progress_node)
{
  // Mirror CaseMatchingDeployer::deploy: first adapt file name case in the source mods, then
  // update conflict groups, then perform the actual deployment. The third sub-step replaces the
  // call to Deployer::deploy with a variant that rewrites top-level mod* folder names so the
  // on-disk alphabetical folder order matches the load order.
  if(progress_node)
    (*progress_node)->addChildren({ 2, 1, 3 });
  adaptLoadorderFiles(loadorder,
                      progress_node ? &(*progress_node)->child(0) : std::optional<ProgressNode*>{});
  updateConflictGroups(progress_node ? &(*progress_node)->child(1)
                                     : std::optional<ProgressNode*>{});

  std::optional<ProgressNode*> deploy_node =
    progress_node ? &(*progress_node)->child(2) : std::optional<ProgressNode*>{};
  if(deploy_node)
    (*deploy_node)->addChildren({ 2, 5, 1 });

  auto [source_files, path_map, mod_sizes] = buildLoadOrderPathMap(loadorder);
  log_(Log::LOG_INFO,
       std::format("Deployer '{}': Deploying {} files for {} mods...",
                   name_,
                   source_files.size(),
                   loadorder.size()));

  std::map<sfs::path, int> dest_files =
    loadDeployedFiles(deploy_node ? &(*deploy_node)->child(0) : std::optional<ProgressNode*>{});
  backupOrRestoreFiles(source_files, dest_files);
  deployFilesWithPathMap(
    source_files,
    path_map,
    deploy_node ? &(*deploy_node)->child(1) : std::optional<ProgressNode*>{});
  saveDeployedFiles(source_files,
                    deploy_node ? &(*deploy_node)->child(2) : std::optional<ProgressNode*>{});
  return mod_sizes;
}

sfs::path Tw3Deployer::rewriteTopLevelModFolder(const sfs::path& relative_path, int index) const
{
  // Split the relative path into its first component and the remainder.
  auto iter = relative_path.begin();
  if(iter == relative_path.end())
    return relative_path;
  const std::string first_component = iter->string();

  // Only rewrite folders the game would load, i.e. those whose name starts with "mod"
  // (case insensitively). Everything else is deployed unchanged.
  const std::string lower = pu::toLowerCase(first_component);
  if(!lower.starts_with("mod"))
    return relative_path;

  // Keep the leading "mod" the game requires, insert the zero-padded index plus an underscore
  // directly after it, and drop the original leading "mod" from the remainder.
  // e.g. "modFoo" at index 3 -> "mod0003_Foo".
  const std::string remainder = first_component.substr(3);
  const std::string new_first =
    std::format("mod{:0{}}_{}", index, LOAD_ORDER_PREFIX_DIGITS, remainder);

  // Reassemble the path with the rewritten first component.
  sfs::path rewritten = new_first;
  for(++iter; iter != relative_path.end(); ++iter)
    rewritten /= *iter;
  return rewritten;
}

std::tuple<std::map<sfs::path, int>, std::map<sfs::path, sfs::path>, std::map<int, unsigned long>>
Tw3Deployer::buildLoadOrderPathMap(const std::vector<int>& loadorder) const
{
  std::map<sfs::path, int> source_files{};
  std::map<sfs::path, sfs::path> path_map{};
  std::map<int, unsigned long> mod_sizes{};

  // Iterate from the last mod in the load order to the first. As in
  // Deployer::getDeploymentSourceFilesAndModSizes, std::map::insert does not overwrite an existing
  // key, so for any destination path the first mod encountered (the one later in the load order)
  // wins. For files which keep their own mod* folder this is irrelevant because each mod is placed
  // in a uniquely prefixed folder; it still correctly resolves conflicts between loose files that
  // are not located under a mod* folder.
  for(int i = loadorder.size() - 1; i >= 0; i--)
  {
    const int mod_id = loadorder[i];
    if(!checkModPathExistsAndMaybeLogError(mod_id))
      continue;
    const sfs::path mod_base_path = source_path_ / std::to_string(mod_id);

    // Determine whether this mod has at least one top-level mod* folder. If not, every top-level
    // entry is wrapped in a synthesized "mod{index:04d}_{mod_id}" folder so the game recognizes it.
    bool has_mod_folder = false;
    for(const auto& dir_entry : sfs::directory_iterator(mod_base_path))
    {
      if(!dir_entry.is_directory())
        continue;
      if(pu::toLowerCase(dir_entry.path().filename().string()).starts_with("mod"))
      {
        has_mod_folder = true;
        break;
      }
    }
    // TODO(tw3): The fallback below also wraps loose top-level *files* (not just directories) in
    // the synthesized mod folder when no mod* folder is present. The Witcher 3 only loads bundled
    // content from mod* folders, so this is the most reasonable interpretation, but it has not been
    // verified against the game. Mods that legitimately expect loose files at the Mods root (rare)
    // would need a different handling.
    const sfs::path fallback_folder =
      std::format("mod{:0{}}_{}", i, LOAD_ORDER_PREFIX_DIGITS, mod_id);

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
      sfs::path dest_relative_path;
      if(has_mod_folder)
        dest_relative_path = rewriteTopLevelModFolder(relative_path, i);
      else
        dest_relative_path = fallback_folder / relative_path;

      // Insert into both maps in lock-step. If the destination path is already taken by a
      // later-in-loadorder mod, skip this entry entirely so source and destination maps stay
      // consistent.
      const auto [insert_iter, inserted] = source_files.insert({ dest_relative_path, mod_id });
      if(inserted)
        path_map.insert({ dest_relative_path, relative_path });
    }
    mod_sizes[mod_id] = mod_size;
  }
  return { source_files, path_map, mod_sizes };
}

void Tw3Deployer::deployFilesWithPathMap(
  const std::map<sfs::path, int>& source_files,
  const std::map<sfs::path, sfs::path>& path_map,
  std::optional<ProgressNode*> progress_node) const
{
  if(progress_node)
    (*progress_node)->setTotalSteps(source_files.size());

  for(const auto& [dest_relative_path, id] : source_files)
  {
    const sfs::path dest_path = dest_path_ / dest_relative_path;
    if(!checkModPathExistsAndMaybeLogError(id))
    {
      if(progress_node)
        (*progress_node)->advance();
      continue;
    }
    // Look up the source-relative path for this destination. It always exists because both maps
    // are populated in lock-step, but guard defensively.
    const auto path_iter = path_map.find(dest_relative_path);
    if(path_iter == path_map.end())
    {
      if(progress_node)
        (*progress_node)->advance();
      continue;
    }
    const sfs::path source_path = source_path_ / std::to_string(id) / path_iter->second;

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
    if(deploy_mode_ == copy)
      sfs::copy_file(source_path, dest_path);
    else if(deploy_mode_ == sym_link)
      sfs::create_symlink(source_path, dest_path);
    else
      sfs::create_hard_link(source_path, dest_path);

    if(progress_node)
      (*progress_node)->advance();
  }
}
