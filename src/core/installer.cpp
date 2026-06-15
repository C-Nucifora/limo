#include "installer.h"
#include <algorithm>
#include <limits>
#include "compressionerror.h"
#include "pathutils.h"
#include <archive.h>
#include <archive_entry.h>
#include <cstdint>
#include <set>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <regex>
#include <string>
#include <system_error>
#include <zlib.h>
#ifdef LIMO_WITH_UNRAR
#define _UNIX
#include <dll.hpp>
#endif

namespace sfs = std::filesystem;
namespace pu = path_utils;

bool Installer::sourceIsArchive(const sfs::path& source_path)
{
    struct archive* source;
    source = archive_read_new();
    archive_read_support_filter_all(source);
    archive_read_support_format_all(source);

    if(archive_read_open_filename(source, source_path.c_str(), 10240) != ARCHIVE_OK){
        return false;
    }
    if(archive_read_free(source) != ARCHIVE_OK){
        return false;
    }

    return true;
}

void Installer::extract(const sfs::path& source_path,
                        const sfs::path& dest_path,
                        std::optional<ProgressNode*> progress_node)
{
  log(Log::LOG_DEBUG, "Beginning extraction");

  if(sfs::is_directory(source_path))
  {
    sfs::create_directories(dest_path);
    if(source_path.parent_path() == dest_path.parent_path())
      sfs::rename(source_path, dest_path);
    else
      sfs::copy(source_path, dest_path, sfs::copy_options::recursive);
    return;
  }

  if(sourceIsOmod(source_path))
  {
    extractOmodArchive(source_path, dest_path);
    for(const auto& dir_entry : sfs::recursive_directory_iterator(dest_path))
    {
      auto permissions = sfs::perms::owner_read | sfs::perms::owner_write | sfs::perms::group_read |
                         sfs::perms::group_write | sfs::perms::others_read;
      if(dir_entry.is_directory())
        permissions |= sfs::perms::owner_exec | sfs::perms::group_exec | sfs::perms::others_exec;
      sfs::permissions(dir_entry.path(), permissions);
    }
    return;
  }

  // for singular, non-archive files, just create the temp directory and copy the file to it
  if(!sourceIsArchive(source_path)){
    const std::string base_file_name = source_path.filename();
    const std::filesystem::path new_dest = dest_path / base_file_name;

    sfs::create_directories(dest_path);
    sfs::copy(source_path, new_dest);
    return;
  }

  try
  {
    extractWithProgress(source_path, dest_path, progress_node);
  }
  catch(CompressionError& error)
  {
    std::string extension = source_path.extension().string();
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
#ifdef LIMO_WITH_UNRAR
    // A .fomod file is a FOMOD-packaged archive that may use any container
    // format (zip/7z/rar). libarchive content-sniffs and usually handles it,
    // but old Fallout 3 / New Vegas .fomod files are frequently RAR archives.
    // Since the .fomod extension hides the real container type, fall back to
    // the libunrar based extraction (as is done for .rar) when libarchive
    // fails on a .fomod file.
    if(extension == ".rar" || extension == ".fomod")
    {
      sfs::remove_all(dest_path);
      try
      {
        extractRarArchive(source_path, dest_path);
      }
      catch(CompressionError& rar_error)
      {
        // Not a RAR either: re-raise the original libarchive error so the
        // caller sees the more relevant message.
        if(extension == ".fomod")
          throw error;
        throw rar_error;
      }
    }
    else
      throw error;
#else
    throw error;
#endif
  }
  for(const auto& dir_entry :
      sfs::recursive_directory_iterator(dest_path, sfs::directory_options::none))
  {
    auto permissions = sfs::perms::owner_read | sfs::perms::owner_write | sfs::perms::group_read |
                       sfs::perms::group_write | sfs::perms::others_read;
    if(dir_entry.is_directory())
      permissions |= sfs::perms::owner_exec | sfs::perms::group_exec | sfs::perms::others_exec;
    sfs::permissions(dir_entry.path(), permissions);
  }
}

unsigned long Installer::install(const sfs::path& source,
                                 const sfs::path& destination,
                                 int options,
                                 const std::string& type,
                                 int root_level,
                                 const std::vector<std::pair<sfs::path, sfs::path>> fomod_files,
                                 const std::set<sfs::path>& selected_files)
{
  log(Log::LOG_DEBUG, "Beginning mod installation");

  if(type != SIMPLEINSTALLER && type != FOMODINSTALLER)
    throw std::runtime_error("Error: Unknown Installer type \"" + type + "\"!");

  // Some games load mods as archive files directly (e.g. Doom source ports load
  // .pk3/.pk4 zip archives as-is). When the no_extract flag is set, deploy the
  // source archive itself as a single opaque file instead of extracting it.
  if(options & no_extract)
  {
    if(sfs::is_directory(source))
      throw std::runtime_error("Cannot install a directory without extraction.");
    sfs::create_directories(destination);
    const auto target = destination / source.filename();
    sfs::copy_file(source, target, sfs::copy_options::overwrite_existing);
    auto permissions = sfs::perms::owner_read | sfs::perms::owner_write | sfs::perms::group_read |
                       sfs::perms::group_write | sfs::perms::others_read;
    sfs::permissions(target, permissions);
    return sfs::is_regular_file(target) ? sfs::file_size(target) : 0;
  }

  unsigned tmp_id = 0;
  sfs::path tmp_dir;
  do
    tmp_dir = destination.parent_path() / (EXTRACT_TMP_DIR + std::to_string(tmp_id));
  while(pu::exists(tmp_dir) && tmp_id++ < std::numeric_limits<unsigned>::max());
  if(tmp_id == std::numeric_limits<unsigned>::max())
    throw std::runtime_error("Could not create directory!");

  // Try to populate the temporary directory from a previously cached extraction
  // of this exact archive. This avoids re-running libarchive on reinstall. On
  // any miss or failure, fall back to a normal extraction and refresh the cache.
  bool populated_from_cache = false;
  if(!sfs::is_directory(source))
  {
    try
    {
      populated_from_cache = populateFromCache(source, tmp_dir);
    }
    catch(...)
    {
      sfs::remove_all(tmp_dir);
      sfs::create_directories(tmp_dir);
      populated_from_cache = false;
    }
  }

  if(!populated_from_cache)
  {
    try
    {
      extract(source, tmp_dir, {});
    }
    catch(CompressionError& error)
    {
      sfs::remove_all(tmp_dir);
      throw error;
    }
    // Store a verbatim copy of the freshly extracted archive in the cache so the
    // next reinstall can reuse it. Only meaningful for actual archives, not
    // directory sources (which extract() handles via rename/copy).
    if(!sfs::is_directory(source))
      storeInCache(source, tmp_dir);
  }

  // Restrict the extracted tree to the user-selected subset (if any) before the
  // installer-specific move pipeline runs. The selection is matched against the
  // archive layout (== the just-extracted tmp_dir), i.e. before root_level
  // stripping, exactly as returned by getArchiveFileNames. An empty selection
  // leaves the tree untouched (unchanged default behavior).
  if(!selected_files.empty())
  {
    try
    {
      pruneToSelection(tmp_dir, selected_files);
    }
    catch(...)
    {
      sfs::remove_all(tmp_dir);
      throw;
    }
  }

  if(type == FOMODINSTALLER)
  {
    if(fomod_files.empty())
    {
      sfs::remove_all(tmp_dir);
      throw std::runtime_error("No files to install.");
    }
    if(root_level > 0)
    {
      auto tmp_move_dir = tmp_dir.string() + "." + MOVE_EXTENSION;
      pu::moveFilesWithDepth(tmp_dir, tmp_move_dir, root_level);
      sfs::rename(tmp_move_dir, tmp_dir);
    }

    for(auto iter = fomod_files.begin(); iter != fomod_files.end(); iter++)
    {
      const auto& [source_file, dest_file] = *iter;
      sfs::create_directories(destination / dest_file.parent_path());
      sfs::path actual_source = source_file;
      if(!sfs::exists(tmp_dir / actual_source))
      {
        // The ModuleConfig may reference a source whose casing differs from the extracted files
        // (Windows paths are case-insensitive), e.g. a "Required" folder; resolve it
        // case-insensitively before giving up (limo-app/limo#46, #253).
        auto resolved = pu::pathExists(source_file, tmp_dir, true);
        if(resolved)
          actual_source = *resolved;
        else
        {
          sfs::remove_all(destination);
          sfs::remove_all(tmp_dir);
          throw std::runtime_error("Could not find '" + source_file.string() + "'");
        }
      }
      bool contains_no_duplicates = std::find_if(std::next(iter),
                                                 fomod_files.end(),
                                                 [source_file](auto pair) {
                                                   return pair.first == source_file;
                                                 }) == fomod_files.end();
      if(sfs::is_directory(tmp_dir / actual_source))
      {
        contains_no_duplicates &= std::find_if(std::next(iter),
                                               fomod_files.end(),
                                               [source_file](auto pair)
                                               {
                                                 sfs::path parent = pair.first.parent_path();
                                                 bool is_duplicate = false;
                                                 while(parent != "" && parent != "/")
                                                 {
                                                   if(parent == source_file)
                                                   {
                                                     is_duplicate = true;
                                                     break;
                                                   }
                                                   parent = parent.parent_path();
                                                 }
                                                 return is_duplicate;
                                               }) == fomod_files.end();

        if(sfs::exists(destination / dest_file))
          pu::moveFilesToDirectory(
            tmp_dir / actual_source, destination / dest_file, contains_no_duplicates);
        else
          pu::copyOrMoveFiles(
            tmp_dir / actual_source, destination / dest_file, contains_no_duplicates);
      }
      else
      {
        if(pu::exists(destination / dest_file) && !sfs::is_directory(destination / dest_file))
          sfs::remove(destination / dest_file);
        if(!dest_file.has_filename())
          pu::copyOrMoveFiles(
            tmp_dir / actual_source, destination / source_file.filename(), contains_no_duplicates);
        else
          pu::copyOrMoveFiles(
            tmp_dir / actual_source, destination / dest_file, contains_no_duplicates);
      }
    }
    sfs::remove_all(tmp_dir);
  }
  else
  {
    if(options & lower_case)
      pu::renameFiles(tmp_dir, tmp_dir, [](unsigned char c) { return std::tolower(c); });
    else if(options & upper_case)
      pu::renameFiles(tmp_dir, tmp_dir, [](unsigned char c) { return std::toupper(c); });
    if(options & single_directory)
    {
      // Collect all entries before mutating the tree. Renaming files into tmp_dir while a
      // recursive_directory_iterator is still walking tmp_dir is undefined behaviour and can
      // cause the iterator to re-encounter the moved files, resulting in an infinite loop for
      // archives with a top-level folder plus a sub directory (e.g. F4SE/SKSE: extender exe at
      // the root and a Data subtree). Do not follow symlinks to avoid cyclic traversal.
      std::vector<sfs::path> files;
      std::vector<sfs::path> directories;
      for(const auto& dir_entry :
          sfs::recursive_directory_iterator(tmp_dir, sfs::directory_options::none))
      {
        if(dir_entry.is_directory() && !dir_entry.is_symlink())
          directories.push_back(dir_entry.path());
        else
          files.push_back(dir_entry.path());
      }
      for(const auto& file : files)
      {
        const auto target = tmp_dir / file.filename();
        if(file != target)
          sfs::rename(file, target);
      }
      for(const auto& dir : directories)
        sfs::remove_all(dir);
    }

    sfs::create_directories(destination);
    try
    {
      if(root_level == 0)
        sfs::rename(tmp_dir, destination);
      else
        pu::moveFilesWithDepth(tmp_dir, destination, root_level);
    }
    catch(sfs::filesystem_error& error)
    {
      sfs::remove_all(tmp_dir);
      sfs::remove_all(destination);
      throw error;
    }
    catch(std::runtime_error& error)
    {
      sfs::remove_all(tmp_dir);
      sfs::remove_all(destination);
      throw error;
    }
  }
  unsigned long size = 0;
  for(const auto& dir_entry :
      sfs::recursive_directory_iterator(destination, sfs::directory_options::none))
    if(dir_entry.is_regular_file())
      size += dir_entry.file_size();
  return size;
}

unsigned long Installer::installPatch(const sfs::path& source,
                                      const sfs::path& existing_mod_dir,
                                      int options,
                                      int root_level)
{
  log(Log::LOG_DEBUG, "Beginning mod patch installation");

  if(!pu::exists(existing_mod_dir) || !sfs::is_directory(existing_mod_dir))
    throw std::runtime_error("Error: Existing mod directory \"" + existing_mod_dir.string() +
                             "\" does not exist!");

  unsigned tmp_id = 0;
  sfs::path tmp_dir;
  do
    tmp_dir = existing_mod_dir.parent_path() / (EXTRACT_TMP_DIR + std::to_string(tmp_id));
  while(pu::exists(tmp_dir) && tmp_id++ < std::numeric_limits<unsigned>::max());
  if(tmp_id == std::numeric_limits<unsigned>::max())
    throw std::runtime_error("Could not create directory!");

  try
  {
    extract(source, tmp_dir, {});
  }
  catch(CompressionError& error)
  {
    sfs::remove_all(tmp_dir);
    throw error;
  }

  // Apply the same name/structure transformations as the simple installer so the
  // overlaid files match the conventions used by the existing mod.
  try
  {
    if(options & lower_case)
      pu::renameFiles(tmp_dir, tmp_dir, [](unsigned char c) { return std::tolower(c); });
    else if(options & upper_case)
      pu::renameFiles(tmp_dir, tmp_dir, [](unsigned char c) { return std::toupper(c); });
    if(options & single_directory)
    {
      std::vector<sfs::path> directories;
      for(const auto& dir_entry : sfs::recursive_directory_iterator(tmp_dir))
      {
        if(!dir_entry.is_directory())
          sfs::rename(dir_entry.path(), tmp_dir / dir_entry.path().filename());
        else
          directories.push_back(dir_entry.path());
      }
      for(const auto& dir : directories)
        sfs::remove_all(dir);
    }

    if(root_level > 0)
    {
      auto tmp_move_dir = tmp_dir.string() + "." + MOVE_EXTENSION;
      pu::moveFilesWithDepth(tmp_dir, tmp_move_dir, root_level);
      sfs::rename(tmp_move_dir, tmp_dir);
    }

    // Overlay the extracted files onto the existing mod. moveFilesToDirectory merges
    // recursively, overwriting files of the same relative path while leaving any files
    // in existing_mod_dir that are not part of the patch untouched.
    pu::moveFilesToDirectory(tmp_dir, existing_mod_dir, true);
  }
  catch(sfs::filesystem_error& error)
  {
    sfs::remove_all(tmp_dir);
    throw error;
  }
  catch(std::runtime_error& error)
  {
    sfs::remove_all(tmp_dir);
    throw error;
  }
  sfs::remove_all(tmp_dir);

  unsigned long size = 0;
  for(const auto& dir_entry : sfs::recursive_directory_iterator(existing_mod_dir))
    if(dir_entry.is_regular_file())
      size += dir_entry.file_size();
  return size;
}

void Installer::uninstall(const sfs::path& mod_path, const std::string& type)
{
  sfs::remove_all(mod_path);
}

std::vector<std::pair<sfs::path, bool>> Installer::getArchiveFileNames(const sfs::path& path)
{

  std::vector<std::pair<sfs::path, bool>> file_names;
  if(sfs::is_directory(path))
  {
    for(const auto& dir_entry :
        sfs::recursive_directory_iterator(path, sfs::directory_options::none))
      file_names.emplace_back(pu::getRelativePath(dir_entry.path(), path), dir_entry.is_directory());
    return file_names;
  }

  // for singular, non-archive files, return data without trying to extract it
  if(!sourceIsArchive(path)){
    file_names.emplace_back(pu::getRelativePath(path, path.parent_path()), sfs::is_directory(path));
    return file_names;
  }

  struct archive* source;
  struct archive_entry* entry;
  source = archive_read_new();
  archive_read_support_filter_all(source);
  archive_read_support_format_all(source);
  if(archive_read_open_filename(source, path.string().c_str(), 10240) != ARCHIVE_OK)
    throw CompressionError("Could not open archive file.");
  while(archive_read_next_header(source, &entry) == ARCHIVE_OK)
    file_names.emplace_back(archive_entry_pathname(entry), archive_entry_filetype(entry) == AE_IFDIR);
  if(archive_read_free(source) != ARCHIVE_OK)
    throw CompressionError("Parsing of archive failed.");
  return file_names;
}

void Installer::pruneToSelection(const sfs::path& extract_dir,
                                 const std::set<sfs::path>& selected_files)
{
  log(Log::LOG_DEBUG, "Pruning extracted files to selection");

  if(selected_files.empty() || !sfs::is_directory(extract_dir))
    return;

  // Normalize the selection. Archive directory entries may carry a trailing
  // slash (e.g. "foo/"), which std::filesystem treats as distinct from "foo";
  // lexically_normal() collapses these so comparisons against the walked tree
  // are consistent. Empty/"." entries (e.g. the archive root) are dropped, as
  // selecting the root means "keep everything".
  std::set<sfs::path> selection;
  for(const auto& raw : selected_files)
  {
    const auto normalized = raw.lexically_normal();
    if(normalized.empty() || normalized == ".")
      return; // root selected -> keep everything, nothing to prune
    selection.insert(normalized);
  }

  // Returns true if the given archive-relative path must be kept: it is selected
  // itself, or it is a descendant of a selected directory.
  auto is_selected_or_under_selection = [&selection](const sfs::path& rel) -> bool
  {
    if(selection.contains(rel))
      return true;
    sfs::path ancestor = rel.parent_path();
    while(!ancestor.empty() && ancestor != ".")
    {
      if(selection.contains(ancestor))
        return true;
      ancestor = ancestor.parent_path();
    }
    return false;
  };

  // First pass: delete every regular file (and symlink) that is not selected and
  // not contained in a selected directory. Collect candidate directories for the
  // second pass. Do not mutate the tree while iterating: gather first, act after.
  std::vector<sfs::path> files_to_remove;
  std::vector<sfs::path> directories;
  for(const auto& dir_entry :
      sfs::recursive_directory_iterator(extract_dir, sfs::directory_options::none))
  {
    const auto rel = sfs::path(pu::getRelativePath(dir_entry.path(), extract_dir)).lexically_normal();
    if(dir_entry.is_directory() && !dir_entry.is_symlink())
    {
      directories.push_back(dir_entry.path());
      continue;
    }
    if(!is_selected_or_under_selection(rel))
      files_to_remove.push_back(dir_entry.path());
  }
  for(const auto& file : files_to_remove)
    sfs::remove(file);

  // Second pass: remove directories that are neither selected, under a selected
  // directory, nor an ancestor of a kept file. A directory is an ancestor of a
  // kept file iff it still contains files after the first pass, so simply drop
  // any directory that has become empty and is not itself part of the selection.
  // Process deepest first so parents see their emptied children.
  std::sort(directories.begin(),
            directories.end(),
            [](const sfs::path& a, const sfs::path& b)
            { return pu::getPathLength(a) > pu::getPathLength(b); });
  for(const auto& dir : directories)
  {
    if(!sfs::exists(dir))
      continue;
    const auto rel = sfs::path(pu::getRelativePath(dir, extract_dir)).lexically_normal();
    if(is_selected_or_under_selection(rel))
      continue; // explicitly selected directory subtree: keep even if empty
    if(pu::directoryIsEmpty(dir))
      sfs::remove_all(dir);
  }
}

std::tuple<int, std::string, std::string> Installer::detectInstallerSignature(
  const sfs::path& source)
{
  const auto path = (sfs::path("fomod") / "ModuleConfig.xml");
  auto str_equals = [](const std::string& a, const std::string& b)
  {
    return std::equal(a.begin(),
                      a.end(),
                      b.begin(),
                      b.end(),
                      [](char c1, char c2) { return tolower(c1) == tolower(c2); });
  };
  const auto files = getArchiveFileNames(source);
  int max_length = 0;
  for(const auto& [file, _] : files)
    max_length = std::max(max_length, pu::getPathLength(file));
  for(int root_level = 0; root_level < max_length; root_level++)
  {
    for(const auto& [file, _] : files)
    {
      const auto [head, tail] = pu::removePathComponents(file, root_level);
      if(str_equals(path, tail))
        return { root_level, head.string(), FOMODINSTALLER };
    }
  }

  // No fomod/ModuleConfig.xml was found. Some old Fallout 3 / New Vegas
  // .fomod files do not ship a ModuleConfig.xml and instead use a simple
  // structure where the mod payload lives inside (or under a wrapper of) a
  // "Data" directory and/or loose plugin files. In that case fall back to a
  // simple install, choosing the root level so the wrapping directory (if any)
  // is stripped and the "Data" folder / plugins end up at the mod root.
  static const std::regex plugin_regex(R"(.*\.(esp|esm|esl|bsa|ba2)$)",
                                       std::regex::icase);
  for(int root_level = 0; root_level < std::max(max_length, 1); root_level++)
  {
    for(const auto& [file, is_dir] : files)
    {
      const auto [head, tail] = pu::removePathComponents(file, root_level);
      // A "Data" directory at this level, or a plugin/archive file directly at
      // this level, marks a valid simple-install root.
      const std::string first = tail.begin() == tail.end()
                                  ? std::string{}
                                  : tail.begin()->string();
      const bool data_dir_here = pu::getPathLength(tail) == 1 && str_equals("Data", first);
      const bool plugin_here =
        !is_dir && pu::getPathLength(tail) == 1 && std::regex_match(first, plugin_regex);
      if(data_dir_here || plugin_here)
        return { root_level, head.string(), SIMPLEINSTALLER };
    }
  }

  return { 0, {}, SIMPLEINSTALLER };
}

void Installer::cleanupFailedInstallation(const sfs::path& staging_dir, int mod_id)
{
  if(mod_id >= 0)
      sfs::remove_all(staging_dir / std::to_string(mod_id));
  for(const auto& dir_entry : sfs::directory_iterator(staging_dir))
  {
    if(!dir_entry.is_directory())
      continue;
    if(dir_entry.path().extension() == MOVE_EXTENSION)
      sfs::remove_all(dir_entry.path());
    std::regex tmp_dir_regex(EXTRACT_TMP_DIR + R"(\d+)");
    if(std::regex_search(dir_entry.path().filename().string(), tmp_dir_regex))
      sfs::remove_all(dir_entry.path());
  }
}

void Installer::setIsAFlatpak(bool is_a_flatpak)
{
  is_a_flatpak_ = is_a_flatpak;
}

void Installer::throwCompressionError(struct archive* source)
{
  throw CompressionError("Error during archive extraction.");

  // The following code sometimes crashes during execution of archive_error_string:

  // throw CompressionError(
  // ("Error during archive extraction: " + std::string(archive_error_string(source))).c_str());
}

void Installer::copyArchive(struct archive* source, struct archive* dest)
{
  int return_code;
  const void* buffer;
  size_t size;
  la_int64_t offset;

  while(true)
  {
    return_code = archive_read_data_block(source, &buffer, &size, &offset);
    if(return_code == ARCHIVE_EOF)
      return;
    if(return_code < ARCHIVE_OK)
      throwCompressionError(source);
    if(archive_write_data_block(dest, buffer, size, offset) < ARCHIVE_OK)
      throwCompressionError(dest);
  }
}

void Installer::extractWithProgress(const sfs::path& source_path,
                                    const sfs::path& dest_path,
                                    std::optional<ProgressNode*> progress_node)
{
  log(Log::LOG_DEBUG, "Beginning extraction with progress");

  constexpr int buffer_size = 10240;
  struct archive* source;
  struct archive* dest;
  struct archive_entry* entry;
  int return_code;
  const char* file_name = source_path.c_str();
  // Security: reject archive entries that would escape the destination directory (absolute
  // paths or ".." traversal) or be written through a symlink (zip-slip / symlink attacks).
  int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_SECURE_NODOTDOT |
              ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS;
  sfs::path working_dir = "/tmp";
  try
  {
    working_dir = sfs::current_path();
  }
  catch(std::filesystem::filesystem_error& error)
  {}
  // Restore the working directory on every exit path (including exceptions): the extraction loop
  // chdir's into dest_path, and a leaked cwd would make later relative-path operations act on the
  // wrong directory.
  struct CwdGuard
  {
    sfs::path dir;
    ~CwdGuard()
    {
      std::error_code ec;
      sfs::current_path(dir, ec);
    }
  } cwd_guard{ working_dir };
  if(!sfs::exists(dest_path))
    sfs::create_directories(dest_path);
  sfs::current_path(dest_path);
  source = archive_read_new();
  archive_read_support_format_all(source);
  archive_read_support_filter_all(source);
  dest = archive_write_disk_new();
  archive_write_disk_set_options(dest, flags);
  archive_write_disk_set_standard_lookup(dest);
  if(archive_read_open_filename(source, file_name, buffer_size))
  {
    sfs::current_path(working_dir);
    throw CompressionError("Could not open archive file.");
  }
  uint64_t total_size = 0;
  while(true)
  {
    return_code = archive_read_next_header(source, &entry);
    if(return_code == ARCHIVE_EOF)
      break;
    if(return_code < ARCHIVE_OK)
    {
      sfs::current_path(working_dir);
      throwCompressionError(source);
    }
    total_size += archive_entry_size(entry);
  }
  if(progress_node)
    (*progress_node)->setTotalSteps(total_size);
  archive_read_close(source);
  archive_read_free(source);
  source = archive_read_new();
  archive_read_support_format_all(source);
  archive_read_support_filter_all(source);
  if(archive_read_open_filename(source, file_name, buffer_size))
  {
    sfs::current_path(working_dir);
    throw CompressionError("Could not open archive file.");
  }

  while(true)
  {
    return_code = archive_read_next_header(source, &entry);
    if(return_code == ARCHIVE_EOF)
      break;
    if(return_code < ARCHIVE_OK)
    {
      sfs::current_path(working_dir);
      throwCompressionError(source);
    }
    archive_entry_set_pathname(entry, archive_entry_pathname(entry));
    if(archive_write_header(dest, entry) < ARCHIVE_OK)
    {
      sfs::current_path(working_dir);
      throwCompressionError(dest);
    }
    const void* buff;
    size_t size;
    int64_t offset;

    while(true)
    {
      return_code = archive_read_data_block(source, &buff, &size, &offset);
      if(return_code == ARCHIVE_EOF)
        break;
      if(return_code < ARCHIVE_OK)
      {
        sfs::current_path(working_dir);
        throwCompressionError(source);
      }
      if(archive_write_data_block(dest, buff, size, offset) != ARCHIVE_OK)
      {
        sfs::current_path(working_dir);
        throwCompressionError(dest);
      }
      if(progress_node)
        (*progress_node)->advance(size);
    }
    if(archive_write_finish_entry(dest) < ARCHIVE_OK)
    {
      sfs::current_path(working_dir);
      throwCompressionError(dest);
    }
  }
  archive_read_close(source);
  archive_read_free(source);
  archive_write_close(dest);
  archive_write_free(dest);
  sfs::current_path(working_dir);
}

#ifdef LIMO_WITH_UNRAR
void Installer::extractRarArchive(const sfs::path& source_path, const sfs::path& dest_path)
{
  log(Log::LOG_DEBUG, "Using fallback rar extraction");

  const auto source_str = source_path.string();
  const auto dest_str = dest_path.string();
  char input_path[source_str.size() + 1];
  for(int i = 0; i < source_str.size(); i++)
    input_path[i] = source_str[i];
  input_path[source_str.size()] = '\0';
  char output_path[dest_str.size() + 1];
  for(int i = 0; i < dest_str.size(); i++)
    output_path[i] = dest_str[i];
  output_path[dest_str.size()] = '\0';

  RAROpenArchiveDataEx archive{ input_path, nullptr, RAR_OM_EXTRACT, 0, nullptr, 0, 0, 0, 0 };
  HANDLE hArcData = RAROpenArchiveEx(&archive);
  if(archive.OpenResult != 0)
    throw CompressionError("Failed to open RAR archive.");
  auto header_data = std::make_unique<RARHeaderDataEx>();
  int header_state = RARReadHeaderEx(hArcData, header_data.get());
  while(header_state == 0)
  {
    if(RARProcessFile(hArcData, RAR_EXTRACT, output_path, nullptr) != 0)
      throw CompressionError("Failed to extract RAR archive.");
    header_state = RARReadHeaderEx(hArcData, header_data.get());
  }
  if(header_state != ERAR_END_ARCHIVE)
    throw CompressionError("Failed to extract RAR archive.");
  RARCloseArchive(hArcData);
}
#endif

std::optional<std::string> Installer::computeCacheKey(const sfs::path& source)
{
  std::error_code ec;
  if(sfs::is_directory(source, ec) || ec)
    return {};
  const auto size = sfs::file_size(source, ec);
  if(ec)
    return {};
  const auto mtime = sfs::last_write_time(source, ec);
  if(ec)
    return {};
  auto canonical = sfs::weakly_canonical(source, ec);
  const std::string path_str = (ec ? source.string() : canonical.string());

  // Derive a hash from the canonical path; combine with size and mtime so a
  // changed archive (even at the same path) never matches a stale cache entry.
  const auto path_hash = std::hash<std::string>{}(path_str);
  const auto mtime_count =
    static_cast<long long>(mtime.time_since_epoch().count());
  return std::to_string(path_hash) + "_" + std::to_string(size) + "_" +
         std::to_string(mtime_count);
}

sfs::path Installer::cacheEntryPath(const std::string& key)
{
  std::error_code ec;
  sfs::path base = sfs::temp_directory_path(ec);
  if(ec)
    base = "/tmp";
  return base / EXTRACT_CACHE_DIR / key;
}

void Installer::hardLinkOrCopyTree(const sfs::path& src, const sfs::path& dst)
{
  sfs::create_directories(dst);
  for(const auto& dir_entry : sfs::recursive_directory_iterator(src))
  {
    const auto relative = pu::getRelativePath(dir_entry.path(), src);
    const auto target = dst / relative;
    if(dir_entry.is_directory())
    {
      sfs::create_directories(target);
    }
    else if(dir_entry.is_regular_file())
    {
      sfs::create_directories(target.parent_path());
      std::error_code ec;
      sfs::create_hard_link(dir_entry.path(), target, ec);
      if(ec)
      {
        // Hard links can not cross file system boundaries (and fail for some
        // file systems); fall back to a plain copy in that case.
        sfs::copy_file(dir_entry.path(), target, sfs::copy_options::overwrite_existing);
      }
    }
    else
    {
      // Symlinks and other special entries: copy verbatim where possible.
      std::error_code ec;
      sfs::copy(dir_entry.path(),
                target,
                sfs::copy_options::copy_symlinks | sfs::copy_options::overwrite_existing,
                ec);
    }
  }
}

bool Installer::populateFromCache(const sfs::path& source, const sfs::path& dest_path)
{
  const auto key = computeCacheKey(source);
  if(!key)
    return false;
  const auto entry = cacheEntryPath(*key);
  const auto marker = entry / CACHE_MARKER_FILE;
  const auto payload = entry / CACHE_PAYLOAD_DIR;
  std::error_code ec;
  if(!sfs::exists(marker, ec) || !sfs::is_directory(payload, ec))
    return false;

  // Verify the marker matches the current archive identity before reusing it.
  std::string stored_key;
  {
    std::ifstream marker_stream(marker);
    if(!marker_stream)
      return false;
    std::getline(marker_stream, stored_key);
  }
  if(stored_key != *key)
    return false;

  log(Log::LOG_DEBUG, "Populating installation from extraction cache");
  if(!sfs::exists(dest_path))
    sfs::create_directories(dest_path);
  hardLinkOrCopyTree(payload, dest_path);
  return true;
}

void Installer::storeInCache(const sfs::path& source, const sfs::path& extracted_path)
{
  try
  {
    const auto key = computeCacheKey(source);
    if(!key)
      return;
    const auto entry = cacheEntryPath(*key);
    const auto marker = entry / CACHE_MARKER_FILE;
    const auto payload = entry / CACHE_PAYLOAD_DIR;

    // Rebuild the entry from scratch so a partial/stale cache can not be reused.
    sfs::remove_all(entry);
    sfs::create_directories(payload);
    hardLinkOrCopyTree(extracted_path, payload);

    std::ofstream marker_stream(marker, std::ios::trunc);
    if(marker_stream)
      marker_stream << *key << "\n";
    log(Log::LOG_DEBUG, "Stored extraction in cache for future reinstalls");
  }
  catch(...)
  {
    // Caching is a best-effort optimization; never let it break installation.
    log(Log::LOG_DEBUG, "Failed to store extraction cache (ignored)");
  }
}

bool Installer::sourceIsOmod(const sfs::path& source_path)
{
  std::string extension = source_path.extension().string();
  std::transform(extension.begin(),
                 extension.end(),
                 extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return extension == ".omod";
}

bool Installer::readOmodMember(const sfs::path& archive_path,
                               const std::string& member_name,
                               std::vector<unsigned char>& out_data)
{
  auto to_lower = [](std::string s)
  {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
  };
  const std::string wanted = to_lower(member_name);

  struct archive* source = archive_read_new();
  archive_read_support_format_all(source);
  archive_read_support_filter_all(source);
  if(archive_read_open_filename(source, archive_path.string().c_str(), 10240) != ARCHIVE_OK)
  {
    archive_read_free(source);
    return false;
  }

  bool found = false;
  struct archive_entry* entry;
  while(archive_read_next_header(source, &entry) == ARCHIVE_OK)
  {
    const char* name = archive_entry_pathname(entry);
    if(name == nullptr)
      continue;
    if(to_lower(name) != wanted)
      continue;
    found = true;
    out_data.clear();
    const void* buff;
    size_t size;
    int64_t offset;
    int return_code;
    while((return_code = archive_read_data_block(source, &buff, &size, &offset)) == ARCHIVE_OK)
    {
      const auto* bytes = static_cast<const unsigned char*>(buff);
      out_data.insert(out_data.end(), bytes, bytes + size);
    }
    if(return_code != ARCHIVE_EOF)
      found = false;
    break;
  }

  archive_read_free(source);
  return found;
}

bool Installer::inflateZlibBuffer(const std::vector<unsigned char>& input,
                                  std::vector<unsigned char>& output)
{
  output.clear();
  if(input.empty())
    return true;

  z_stream stream{};
  // 47 = automatic header detection (zlib or gzip) with a 32K window.
  if(inflateInit2(&stream, 47) != Z_OK)
    return false;

  stream.next_in = const_cast<unsigned char*>(input.data());
  stream.avail_in = static_cast<uInt>(input.size());

  std::vector<unsigned char> chunk(262144);
  int return_code = Z_OK;
  do
  {
    stream.next_out = chunk.data();
    stream.avail_out = static_cast<uInt>(chunk.size());
    return_code = inflate(&stream, Z_NO_FLUSH);
    if(return_code != Z_OK && return_code != Z_STREAM_END)
    {
      inflateEnd(&stream);
      return false;
    }
    output.insert(output.end(), chunk.data(), chunk.data() + (chunk.size() - stream.avail_out));
  } while(return_code != Z_STREAM_END && stream.avail_in > 0);

  inflateEnd(&stream);
  return return_code == Z_STREAM_END;
}

bool Installer::decompressBufferWithLibarchive(const std::vector<unsigned char>& input,
                                               std::vector<unsigned char>& output)
{
  output.clear();
  if(input.empty())
    return true;

  struct archive* source = archive_read_new();
  // The OMOD data blob is a raw compressed stream (no archive wrapper), so
  // enable the raw format together with all available filters (lzma/xz/7z).
  archive_read_support_filter_all(source);
  archive_read_support_format_raw(source);
  if(archive_read_open_memory(source, input.data(), input.size()) != ARCHIVE_OK)
  {
    archive_read_free(source);
    return false;
  }

  struct archive_entry* entry;
  if(archive_read_next_header(source, &entry) != ARCHIVE_OK)
  {
    archive_read_free(source);
    return false;
  }

  const void* buff;
  size_t size;
  int64_t offset;
  int return_code;
  while((return_code = archive_read_data_block(source, &buff, &size, &offset)) == ARCHIVE_OK)
  {
    const auto* bytes = static_cast<const unsigned char*>(buff);
    output.insert(output.end(), bytes, bytes + size);
  }

  bool success = return_code == ARCHIVE_EOF;
  archive_read_free(source);
  return success;
}

void Installer::extractOmodArchive(const sfs::path& source_path, const sfs::path& dest_path)
{
  log(Log::LOG_DEBUG, "Beginning OMOD extraction");
  log(Log::LOG_WARNING,
      "OMOD install scripts are not executed; only the contained mod files are extracted.");

  // Helpers for reading the little-endian, .NET-BinaryReader style 'config' and
  // '*.crc' member streams.
  struct Reader
  {
    const std::vector<unsigned char>& data;
    size_t pos = 0;
    bool ok = true;

    explicit Reader(const std::vector<unsigned char>& d) : data(d) {}

    bool remaining(size_t n) const { return pos + n <= data.size(); }

    unsigned char readByte()
    {
      if(!remaining(1))
      {
        ok = false;
        return 0;
      }
      return data[pos++];
    }

    uint32_t readUInt32()
    {
      if(!remaining(4))
      {
        ok = false;
        return 0;
      }
      uint32_t value = static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
                       (static_cast<uint32_t>(data[pos + 2]) << 16) |
                       (static_cast<uint32_t>(data[pos + 3]) << 24);
      pos += 4;
      return value;
    }

    int64_t readInt64()
    {
      if(!remaining(8))
      {
        ok = false;
        return 0;
      }
      uint64_t value = 0;
      for(int i = 0; i < 8; i++)
        value |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
      pos += 8;
      return static_cast<int64_t>(value);
    }

    // .NET BinaryReader 7-bit-encoded length prefixed UTF-8 string.
    std::string readString()
    {
      uint32_t length = 0;
      int shift = 0;
      while(true)
      {
        if(!remaining(1) || shift > 35)
        {
          ok = false;
          return {};
        }
        unsigned char b = data[pos++];
        length |= static_cast<uint32_t>(b & 0x7F) << shift;
        if((b & 0x80) == 0)
          break;
        shift += 7;
      }
      if(!remaining(length))
      {
        ok = false;
        return {};
      }
      std::string result(reinterpret_cast<const char*>(data.data() + pos), length);
      pos += length;
      return result;
    }
  };

  // Compression type as stored in the OMOD config (0 = SevenZip, 1 = Zip).
  enum class OmodCompression
  {
    seven_zip,
    zip
  };

  // Parse the 'config' member to determine which compression the data blobs use.
  OmodCompression compression = OmodCompression::seven_zip;
  std::vector<unsigned char> config_data;
  if(readOmodMember(source_path, "config", config_data))
  {
    Reader reader(config_data);
    const unsigned char file_version = reader.readByte();
    reader.readString();    // ModName
    reader.readUInt32();    // MajorVersion
    reader.readUInt32();    // MinorVersion
    reader.readString();    // Author
    reader.readString();    // Email
    reader.readString();    // Website
    reader.readString();    // Description
    if(file_version >= 2)
      reader.readInt64();    // CreationTime (.NET DateTime ticks)
    const unsigned char compression_byte = reader.readByte();
    if(reader.ok)
      compression = compression_byte == 1 ? OmodCompression::zip : OmodCompression::seven_zip;
    else
      log(Log::LOG_WARNING, "Could not parse OMOD config; assuming 7-zip compression.");
  }
  else
    log(Log::LOG_WARNING, "OMOD has no readable config; assuming 7-zip compression.");

  // Each pair maps a CRC member (the file list) to its data blob member.
  const std::vector<std::pair<std::string, std::string>> payloads{ { "data.crc", "data" },
                                                                   { "plugins.crc", "plugins" } };

  bool extracted_any = false;
  for(const auto& [crc_name, data_name] : payloads)
  {
    std::vector<unsigned char> crc_data;
    if(!readOmodMember(source_path, crc_name, crc_data))
      continue;    // Optional member (e.g. an OMOD with no loose plugins).

    std::vector<unsigned char> blob;
    if(!readOmodMember(source_path, data_name, blob))
    {
      log(Log::LOG_WARNING, "OMOD lists '" + crc_name + "' but has no '" + data_name + "' member.");
      continue;
    }

    // Decompress the concatenated data blob.
    std::vector<unsigned char> payload;
    bool decompressed = false;
    if(compression == OmodCompression::zip)
      decompressed = inflateZlibBuffer(blob, payload);
    else
      decompressed = decompressBufferWithLibarchive(blob, payload);
    // Fall back to the other method in case the config lied about its type.
    if(!decompressed)
    {
      if(compression == OmodCompression::zip)
        decompressed = decompressBufferWithLibarchive(blob, payload);
      else
        decompressed = inflateZlibBuffer(blob, payload);
    }
    if(!decompressed)
    {
      log(Log::LOG_WARNING,
          "Could not decompress OMOD '" + data_name +
            "' blob. The OMOD may use an unsupported compression; skipping it.");
      continue;
    }

    // Parse the file list and slice the decompressed payload accordingly.
    // CRC entry layout per file: string path, uint32 crc, int64 length.
    Reader reader(crc_data);
    size_t payload_offset = 0;
    while(reader.pos < crc_data.size())
    {
      const std::string entry_path = reader.readString();
      reader.readUInt32();    // crc (unused)
      const int64_t length = reader.readInt64();
      if(!reader.ok || length < 0)
      {
        log(Log::LOG_WARNING, "Malformed OMOD file list in '" + crc_name + "'; stopping early.");
        break;
      }
      if(payload_offset + static_cast<size_t>(length) > payload.size())
      {
        log(Log::LOG_WARNING,
            "OMOD payload smaller than declared in '" + crc_name + "'; stopping early.");
        break;
      }

      // OMOD paths use backslashes; normalise and guard against traversal.
      std::string normalised = entry_path;
      std::replace(normalised.begin(), normalised.end(), '\\', '/');
      sfs::path rel_path = sfs::path(normalised).lexically_normal();
      bool is_safe = !rel_path.empty() && !rel_path.is_absolute();
      for(const auto& part : rel_path)
        if(part == "..")
          is_safe = false;
      if(!is_safe)
      {
        log(Log::LOG_WARNING, "Skipping unsafe OMOD entry path: " + entry_path);
        payload_offset += static_cast<size_t>(length);
        continue;
      }

      const sfs::path out_path = dest_path / rel_path;
      std::error_code ec;
      sfs::create_directories(out_path.parent_path(), ec);
      std::FILE* file = std::fopen(out_path.string().c_str(), "wb");
      if(file == nullptr)
      {
        log(Log::LOG_WARNING, "Could not write OMOD file: " + out_path.string());
        payload_offset += static_cast<size_t>(length);
        continue;
      }
      if(length > 0)
        std::fwrite(payload.data() + payload_offset, 1, static_cast<size_t>(length), file);
      std::fclose(file);
      payload_offset += static_cast<size_t>(length);
      extracted_any = true;
    }
  }

  if(!extracted_any)
    throw CompressionError("Could not extract any files from OMOD archive.");
}
