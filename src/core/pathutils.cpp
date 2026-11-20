#include "pathutils.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <set>
// For FICLONE reflink support (limo-app/limo#232)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sfs = std::filesystem;
namespace pu = path_utils;


namespace path_utils
{
std::optional<sfs::path> pathExists(const sfs::path& path_to_check,
                                    const sfs::path& base_path,
                                    bool case_insensitive)
{
  if(pu::exists(base_path / path_to_check))
    return path_to_check;
  if(!case_insensitive || base_path != "" && !pu::exists(base_path))
    return {};
  const sfs::path target =
    path_to_check.string().ends_with("/") ? path_to_check.parent_path() : path_to_check;

  sfs::path actual_path;
  for(auto iter = target.begin(); iter != target.end(); iter++)
  {
    if(pu::exists(base_path / actual_path / *iter))
    {
      actual_path /= *iter;
      continue;
    }

    std::string lower_part = toLowerCase(*iter);
    bool found = false;
    for(const auto& dir_entry : sfs::directory_iterator(base_path / actual_path))
    {
      const sfs::path path_end = *(std::prev(dir_entry.path().end()));
      std::string lower_case_path_end = toLowerCase(path_end);
      std::string actual_case_path_end = path_end.string();
      if(lower_case_path_end == lower_part)
      {
        actual_path /= actual_case_path_end;
        found = true;
        break;
      }
    }
    if(!found)
      return {};
  }
  return actual_path;
}

std::string toLowerCase(const sfs::path& path)
{
  auto path_string = path.string();
  std::transform(path_string.begin(),
                 path_string.end(),
                 path_string.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return path_string;
}

void moveFilesToDirectory(const sfs::path& source, const sfs::path& destination, bool move)
{
  if(!sfs::exists(destination))
    sfs::create_directories(destination);
  for(const auto& dir_entry : sfs::directory_iterator(source))
  {
    const auto relative_path = getRelativePath(dir_entry.path(), source);
    if(sfs::exists(destination / relative_path))
    {
      if(sfs::is_directory(destination / relative_path))
        moveFilesToDirectory(dir_entry.path(), destination / relative_path, move);
      else
      {
        sfs::remove(destination / relative_path);
        copyOrMoveFiles(dir_entry.path(), destination / relative_path, move);
      }
      continue;
    }
    copyOrMoveFiles(dir_entry.path(), destination / relative_path, move);
  }
  if(sfs::exists(source) && move)
    sfs::remove_all(source);
}

std::string normalizePath(const std::string& path)
{
  // Only swaps separators; does not lexically normalize ('..', '.', duplicate separators are
  // left as-is). This is not a security boundary, see the header documentation.
  return std::regex_replace(path, std::regex(R"(\\)"), "/");
}

std::string getRelativePath(sfs::path target, sfs::path source)
{
  if(source == target)
    return "";
  const auto relative = target.lexically_relative(source);
  return relative == "." ? "" : relative.string();
}

bool directoryIsEmpty(const sfs::path& directory, std::vector<std::string> ignored_files)
{
  if(!sfs::is_directory(directory))
    return false;
  for(const auto& dir_entry : sfs::recursive_directory_iterator(directory))
  {
    if(!dir_entry.is_directory() &&
       !std::ranges::any_of(
         ignored_files, [&dir_entry](auto file) { return file == dir_entry.path().filename(); }))
      return false;
  }
  return true;
}

int getPathLength(const sfs::path& path)
{
  int length = 0;
  for(const auto& e : path)
    length++;
  return length;
}

std::pair<sfs::path, sfs::path> removePathComponents(const sfs::path& path, int depth)
{
  sfs::path short_path;
  sfs::path head;
  int cur_depth = 0;
  for(auto it = path.begin(); it != path.end(); it++, cur_depth++)
  {
    if(cur_depth >= depth)
      short_path /= *it;
    else
      head /= *it;
  }
  return { head, short_path };
}

void renameFiles(const sfs::path& destination,
                 const sfs::path& source,
                 std::function<unsigned char(unsigned char)> converter)
{
  std::vector<sfs::path> old_directories;
  for(const auto& dir_entry : sfs::recursive_directory_iterator(source))
  {
    auto relative_path = getRelativePath(dir_entry.path(), source);
    std::string old_path = relative_path;
    std::transform(relative_path.begin(), relative_path.end(), relative_path.begin(), converter);
    if(dir_entry.is_directory())
    {
      if(old_path != relative_path)
        old_directories.push_back(dir_entry.path());
      continue;
    }
    if(!sfs::exists((destination / relative_path).parent_path()))
      sfs::create_directories((destination / relative_path).parent_path());
    sfs::rename(dir_entry.path(), destination / relative_path);
  }
  if(source == destination)
  {
    for(const auto& dir : old_directories)
      sfs::remove_all(dir);
  }
  else
    sfs::remove_all(source);
}

void moveFilesWithDepth(const sfs::path& source, const sfs::path& destination, int depth)
{
  std::set<std::pair<sfs::path, sfs::path>> files_to_move;
  for(const auto& dir_entry : sfs::recursive_directory_iterator(source))
  {
    const auto [head, short_path] =
      removePathComponents(getRelativePath(dir_entry.path(), source), depth);
    if(short_path != "")
      files_to_move.emplace(dir_entry.path(), destination / short_path);
  }

  for(const auto& [cur_source, cur_dest] : files_to_move)
  {
    if(sfs::is_directory(cur_source))
      sfs::create_directories(cur_dest);
    else
    {
      if(pu::exists(cur_dest))
        throw std::runtime_error("Error: Duplicate file detected: \"" +
                                 getRelativePath(cur_source, source) + "\"!");
      if(cur_dest.has_parent_path())
        sfs::create_directories(cur_dest.parent_path());
      sfs::rename(cur_source, cur_dest);
    }
  }
  sfs::remove_all(source);
}

void copyOrMoveFiles(const sfs::path& source, const sfs::path& destination, bool move)
{
  if(move)
    sfs::rename(source, destination);
  else
    sfs::copy(source, destination, sfs::copy_options(sfs::copy_options::recursive));
}

bool exists(const std::filesystem::path& path)
{
  return sfs::exists(sfs::symlink_status(path));
}

sfs::path createTemporaryDirectory(const sfs::path& parent)
{
  std::string pattern = (parent / ".limo-XXXXXX").string();
  if(!::mkdtemp(pattern.data()))
    throw sfs::filesystem_error("Could not create temporary directory", parent,
                                std::error_code(errno, std::generic_category()));
  return pattern;
}

TemporaryDirectory::TemporaryDirectory(const sfs::path& parent) :
  path(createTemporaryDirectory(parent))
{}

TemporaryDirectory::~TemporaryDirectory()
{
  std::error_code error;
  if(cleanup_)
    sfs::remove_all(path, error);
}

void TemporaryDirectory::retain()
{
  cleanup_ = false;
}

void exchangePaths(const sfs::path& first, const sfs::path& second)
{
  if(::renameat2(AT_FDCWD, first.c_str(), AT_FDCWD, second.c_str(), RENAME_EXCHANGE) == 0)
    return;
  const auto error = std::error_code(errno, std::generic_category());
  if(error != std::errc::invalid_argument && error != std::errc::function_not_supported &&
     error != std::errc::operation_not_supported)
    throw sfs::filesystem_error("Could not exchange paths", first, second, error);

  TemporaryDirectory pending(first.parent_path());
  const auto held = pending.path / "original";
  sfs::rename(first, held);
  bool second_moved = false;
  try
  {
    sfs::rename(second, first);
    second_moved = true;
    sfs::rename(held, second);
  }
  catch(...)
  {
    try
    {
      if(second_moved)
        sfs::rename(first, second);
      sfs::rename(held, first);
    }
    catch(...)
    {
      pending.retain();
      throw std::runtime_error("Could not restore exchanged paths. Recovery data is at " +
                               pending.path.string());
    }
    throw;
  }
}

void reflinkOrCopyFile(const sfs::path& source, const sfs::path& dest)
{
  const int src_fd = ::open(source.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if(src_fd < 0)
    throw sfs::filesystem_error("Could not open copy source", source,
                                std::error_code(errno, std::generic_category()));
  struct stat status{};
  if(::fstat(src_fd, &status) != 0 || !S_ISREG(status.st_mode))
  {
    ::close(src_fd);
    throw sfs::filesystem_error("Copy source is not a regular file", source,
                                std::make_error_code(std::errc::invalid_argument));
  }
  const int dst_fd = ::open(dest.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if(dst_fd < 0)
  {
    const auto error = std::error_code(errno, std::generic_category());
    ::close(src_fd);
    throw sfs::filesystem_error("Could not create copy destination", dest, error);
  }
  const int cloned = ::ioctl(dst_fd, FICLONE, src_fd);
  const int clone_error = errno;
  const int mode_result = cloned == 0 ? ::fchmod(dst_fd, status.st_mode & 0777) : 0;
  const int mode_error = errno;
  ::close(src_fd);
  ::close(dst_fd);
  if(cloned == 0 && mode_result == 0)
    return;
  sfs::remove(dest);
  if(mode_result != 0 ||
     (clone_error != EOPNOTSUPP && clone_error != ENOTTY && clone_error != EXDEV &&
      clone_error != EINVAL && clone_error != ENOSYS))
    throw sfs::filesystem_error("Could not clone file", source, dest,
                                std::error_code(mode_result != 0 ? mode_error : clone_error,
                                                std::generic_category()));
  sfs::copy_file(source, dest);
}

void reflinkOrCopy(const sfs::path& source, const sfs::path& dest)
{
  // Recursively copy source to dest, using reflinks for regular files where supported
  // (limo-app/limo#232). Symlinks are reproduced as symlinks; directories are created
  // normally. Falls back to a standard copy on non-CoW filesystems.
  if(sfs::is_symlink(source))
  {
    sfs::copy_symlink(source, dest);
    return;
  }
  if(sfs::is_directory(source))
  {
    if(!sfs::create_directory(dest))
      throw sfs::filesystem_error("Copy destination already exists", dest,
                                  std::make_error_code(std::errc::file_exists));
    for(const auto& entry : sfs::directory_iterator(source))
      reflinkOrCopy(entry.path(), dest / entry.path().filename());
    sfs::permissions(dest, sfs::status(source).permissions());
    return;
  }
  // Regular file: attempt reflink, fall back to copy.
  reflinkOrCopyFile(source, dest);
}
}
