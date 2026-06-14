#include "cyberpunkdeployer.h"
#include "pathutils.h"
#include <algorithm>
#include <format>
#include <ranges>

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
  return mod_sizes;
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
      continue;
    const auto source_iter = source_paths.find(dest_relative_path);
    const sfs::path source_relative_path =
      source_iter != source_paths.end() ? source_iter->second : dest_relative_path;
    const sfs::path source_path = source_path_ / std::to_string(id) / source_relative_path;
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
