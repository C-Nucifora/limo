/*!
 * \file savemanager.cpp
 * \brief Implements the SaveManager class.
 *
 * Fork feature #24: save-game manager.
 */

#include "savemanager.h"
#include "log.h"
#include <algorithm>

namespace sfs = std::filesystem;


sfs::path SaveManager::resolveSavesDir(const sfs::path& saves_dir)
{
  if(saves_dir.empty())
    return {};

  std::error_code ec;
  if(!sfs::exists(saves_dir, ec) || ec)
    return {};

  sfs::path dir = saves_dir;
  if(sfs::is_regular_file(dir, ec))
    dir = dir.parent_path();

  if(!sfs::is_directory(dir, ec))
    return {};

  sfs::path canonical = sfs::weakly_canonical(dir, ec);
  if(ec)
    return dir;
  return canonical;
}

std::vector<SaveManager::SaveFile> SaveManager::listSaves(
  const sfs::path& saves_dir,
  const std::vector<std::string>& extensions)
{
  std::vector<SaveFile> saves;
  const sfs::path dir = resolveSavesDir(saves_dir);
  if(dir.empty())
    return saves;

  std::error_code ec;
  for(const auto& entry : sfs::directory_iterator(dir, ec))
  {
    if(ec)
      break;
    if(!entry.is_regular_file(ec) || ec)
      continue;

    if(!extensions.empty())
    {
      std::string ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                     { return std::tolower(c); });
      if(std::find(extensions.begin(), extensions.end(), ext) == extensions.end())
        continue;
    }

    SaveFile save;
    save.path = entry.path();
    save.name = entry.path().filename().string();

    std::error_code size_ec;
    save.size = entry.file_size(size_ec);
    if(size_ec)
      save.size = 0;

    std::error_code time_ec;
    const auto ftime = entry.last_write_time(time_ec);
    if(!time_ec)
    {
      const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - sfs::file_time_type::clock::now() + std::chrono::system_clock::now());
      save.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                         sctp.time_since_epoch())
                         .count();
    }

    saves.push_back(std::move(save));
  }

  std::sort(saves.begin(), saves.end(), [](const SaveFile& a, const SaveFile& b)
            { return a.timestamp > b.timestamp; });
  return saves;
}

bool SaveManager::deleteSave(const sfs::path& save_path)
{
  std::error_code ec;
  if(!sfs::exists(save_path, ec) || ec)
    return false;
  const bool removed = sfs::remove(save_path, ec);
  if(removed && !ec)
    Log::debug("Deleted save file '" + save_path.string() + "'");
  return removed;
}
