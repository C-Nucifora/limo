/*!
 * \file overlaydeployer.cpp
 * \brief Implementation of the OverlayDeployer class.
 *
 * Relates to limo fork issue #110 / upstream limo-app/limo#158 and #77.
 *
 * Linux-only.  Shells out to fuse-overlayfs (rootless, user-space overlay FS)
 * which must be installed and either setuid or listed in /etc/fuse.conf with
 * "user_allow_other".  Typical package name: fuse-overlayfs.
 *
 * Mount layout (all dirs under source_path_/.overlay/):
 *   orig/   — read-only snapshot of the original game directory (bottom lowerdir)
 *   upper/  — fuse-overlayfs upper layer; catches runtime writes by the game
 *   work/   — fuse-overlayfs work dir (kernel requirement)
 *
 * The fuse-overlayfs lowerdir list is built as:
 *   <mod N-1 staging> : <mod N-2 staging> : … : <mod 0 staging> : orig/
 *   (highest-priority mod is listed first so it wins conflicts)
 *
 * dest_path_ is used as the mount point.
 */

#include "overlaydeployer.h"

#include "log.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <json/json.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sfs = std::filesystem;


// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

std::pair<int, std::string> OverlayDeployer::runCommand(const std::string& cmd)
{
  std::string full_cmd = cmd + " 2>&1";
  std::array<char, 512> buf{};
  std::string output;
  int exit_code = 0;

  FILE* pipe = popen(full_cmd.c_str(), "r");
  if(!pipe)
    return { -1, "popen() failed" };

  while(fgets(buf.data(), buf.size(), pipe) != nullptr)
    output += buf.data();

  exit_code = pclose(pipe);
  // WEXITSTATUS for POSIX
  if(WIFEXITED(exit_code))
    exit_code = WEXITSTATUS(exit_code);

  return { exit_code, output };
}

std::string OverlayDeployer::findFuseOverlayfs()
{
  // Check common install locations and PATH via "which"
  auto [code, path] = runCommand("which fuse-overlayfs");
  if(code == 0 && !path.empty())
  {
    // strip trailing newline
    while(!path.empty() && (path.back() == '\n' || path.back() == '\r'))
      path.pop_back();
    return path;
  }
  // Fallback: check fixed paths
  for(const char* p : { "/usr/bin/fuse-overlayfs", "/usr/local/bin/fuse-overlayfs",
                         "/bin/fuse-overlayfs" })
  {
    if(sfs::exists(p))
      return p;
  }
  return {};
}

// ---------------------------------------------------------------------------
// path helpers
// ---------------------------------------------------------------------------

sfs::path OverlayDeployer::overlayDir() const
{
  return source_path_ / overlay_dir_name_;
}

sfs::path OverlayDeployer::upperDir() const
{
  return overlayDir() / upper_dir_name_;
}

sfs::path OverlayDeployer::workDir() const
{
  return overlayDir() / work_dir_name_;
}

sfs::path OverlayDeployer::origDir() const
{
  return overlayDir() / orig_dir_name_;
}

sfs::path OverlayDeployer::stateFile() const
{
  return overlayDir() / state_file_name_;
}

// ---------------------------------------------------------------------------
// state persistence
// ---------------------------------------------------------------------------

void OverlayDeployer::saveState(bool mounted) const
{
  sfs::create_directories(overlayDir());
  Json::Value root;
  root["mounted"]    = mounted;
  root["mountpoint"] = dest_path_.string();

  std::ofstream f(stateFile(), std::ios::binary);
  if(!f.is_open())
    log_(Log::LOG_ERROR,
         std::format("OverlayDeployer '{}': Could not write state file '{}'",
                     name_, stateFile().string()));
  else
    f << root;
}

bool OverlayDeployer::loadState() const
{
  if(!sfs::exists(stateFile()))
    return false;

  std::ifstream f(stateFile(), std::ios::binary);
  if(!f.is_open())
    return false;

  Json::Value root;
  try
  {
    f >> root;
  }
  catch(...)
  {
    return false;
  }
  return root.get("mounted", false).asBool();
}

// ---------------------------------------------------------------------------
// mount / unmount helpers
// ---------------------------------------------------------------------------

bool OverlayDeployer::isMounted() const
{
  // Parse /proc/mounts looking for our mount point
  std::ifstream mounts("/proc/mounts");
  if(!mounts.is_open())
    return false;

  const std::string target = dest_path_.string();
  std::string line;
  while(std::getline(mounts, line))
  {
    // format: device mountpoint fstype options dump pass
    std::istringstream ss(line);
    std::string device, mountpoint;
    ss >> device >> mountpoint;
    if(mountpoint == target)
      return true;
  }
  return false;
}

void OverlayDeployer::doUnmount()
{
  if(!isMounted())
  {
    log_(Log::LOG_DEBUG,
         std::format("OverlayDeployer '{}': mount point '{}' is not mounted, nothing to unmount",
                     name_, dest_path_.string()));
    saveState(false);
    return;
  }

  log_(Log::LOG_INFO,
       std::format("OverlayDeployer '{}': Unmounting overlay at '{}'",
                   name_, dest_path_.string()));

  // Prefer fusermount3 (user-space unmount, no root needed)
  auto [code, output] =
    runCommand(std::format("fusermount3 -u -- '{}'", dest_path_.string()));
  if(code != 0)
  {
    // Fallback: lazy unmount via umount
    log_(Log::LOG_DEBUG,
         std::format("OverlayDeployer '{}': fusermount3 failed ({}), trying umount -l",
                     name_, output));
    auto [code2, out2] =
      runCommand(std::format("umount -l -- '{}'", dest_path_.string()));
    if(code2 != 0)
    {
      log_(Log::LOG_ERROR,
           std::format("OverlayDeployer '{}': Failed to unmount '{}': {}",
                       name_, dest_path_.string(), out2));
      // Do not throw — we still want to save state and continue
    }
  }

  saveState(false);
  log_(Log::LOG_INFO,
       std::format("OverlayDeployer '{}': Overlay unmounted successfully", name_));
}

void OverlayDeployer::ensureOverlayDirs()
{
  sfs::create_directories(upperDir());
  sfs::create_directories(workDir());
  sfs::create_directories(origDir());
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

OverlayDeployer::OverlayDeployer(const sfs::path& source_path,
                                 const sfs::path& dest_path,
                                 const std::string& name,
                                 DeployMode deploy_mode) :
  Deployer(source_path, dest_path, name, deploy_mode)
{
  type_ = "Overlay Deployer";

  // If the state file says we were mounted (e.g. after a crash), log a warning.
  // We do NOT auto-unmount here because dest_path_ might still be in use and
  // the mount might actually be healthy.  deploy()/unDeploy() will handle it.
  if(loadState())
  {
    log_(Log::LOG_DEBUG,
         std::format("OverlayDeployer '{}': State file indicates a previous overlay mount at '{}' "
                     "may still be active.",
                     name_, dest_path_.string()));
  }
}

// ---------------------------------------------------------------------------
// deploy
// ---------------------------------------------------------------------------

std::map<int, unsigned long> OverlayDeployer::deploy(
  const std::vector<int>& loadorder,
  std::optional<ProgressNode*> progress_node)
{
  log_(Log::LOG_INFO,
       std::format("OverlayDeployer '{}': Deploying {} mods via overlay mount...",
                   name_, loadorder.size()));

  // ---- 1. Locate fuse-overlayfs ------------------------------------------
  const std::string fuse_overlayfs = findFuseOverlayfs();
  if(fuse_overlayfs.empty())
    throw std::runtime_error(
      std::format("OverlayDeployer '{}': fuse-overlayfs not found in PATH.  "
                  "Please install it (e.g. 'sudo pacman -S fuse-overlayfs' or "
                  "'sudo apt install fuse-overlayfs') and ensure it is executable. "
                  "See limo fork issue #110.",
                  name_));

  // ---- 2. Tear down any previous mount ------------------------------------
  if(isMounted() || loadState())
    doUnmount();

  // ---- 3. Ensure bookkeeping dirs exist -----------------------------------
  ensureOverlayDirs();

  // ---- 4. Snapshot orig/ from dest_path_ (first-time or after cleanup) ----
  // orig/ is the bottom-most lowerdir — it captures the game's own files.
  // We only (re)create the snapshot when the directory is empty to avoid
  // redundant copies on every deploy.
  if(sfs::is_empty(origDir()))
  {
    log_(Log::LOG_INFO,
         std::format("OverlayDeployer '{}': Creating original-game snapshot in '{}'",
                     name_, origDir().string()));
    try
    {
      sfs::copy(dest_path_, origDir(),
                sfs::copy_options::recursive |
                sfs::copy_options::copy_symlinks |
                sfs::copy_options::skip_existing);
    }
    catch(const sfs::filesystem_error& e)
    {
      throw std::runtime_error(
        std::format("OverlayDeployer '{}': Failed to snapshot game directory '{}' into '{}': {}",
                    name_, dest_path_.string(), origDir().string(), e.what()));
    }
  }

  if(progress_node)
    (*progress_node)->setTotalSteps(static_cast<int>(loadorder.size()) + 2);

  // ---- 5. Build lowerdir list --------------------------------------------
  // fuse-overlayfs resolves conflicts by taking the first hit in the lowerdir
  // chain.  We list highest-priority mods first (last entry in loadorder wins
  // conflicts, so we reverse).
  std::vector<std::string> lowerdirs;
  lowerdirs.reserve(loadorder.size() + 1);

  for(int i = static_cast<int>(loadorder.size()) - 1; i >= 0; i--)
  {
    const int mod_id = loadorder[i];
    if(!checkModPathExistsAndMaybeLogError(mod_id))
      continue;
    sfs::path mod_dir = source_path_ / std::to_string(mod_id);
    lowerdirs.push_back(mod_dir.string());

    if(progress_node)
      (*progress_node)->advance();
  }

  // The original game content is the bottom-most lowerdir
  lowerdirs.push_back(origDir().string());

  // Build the colon-separated lowerdir option string
  std::string lowerdir_opt;
  for(std::size_t i = 0; i < lowerdirs.size(); i++)
  {
    if(i > 0)
      lowerdir_opt += ':';
    lowerdir_opt += lowerdirs[i];
  }

  // ---- 6. Compose and run the mount command --------------------------------
  // Upper/work dirs allow the game to write files at runtime; those writes land
  // in upper/ and do not touch the staging dirs or orig/.
  const std::string mount_cmd =
    std::format("'{}' -o lowerdir='{}',upperdir='{}',workdir='{}' -- '{}'",
                fuse_overlayfs,
                lowerdir_opt,
                upperDir().string(),
                workDir().string(),
                dest_path_.string());

  log_(Log::LOG_DEBUG,
       std::format("OverlayDeployer '{}': Running: {}", name_, mount_cmd));

  auto [exit_code, output] = runCommand(mount_cmd);
  if(exit_code != 0)
    throw std::runtime_error(
      std::format("OverlayDeployer '{}': fuse-overlayfs mount failed (exit {}): {}",
                  name_, exit_code, output));

  // ---- 7. Persist state --------------------------------------------------
  saveState(true);

  if(progress_node)
    (*progress_node)->advance();

  log_(Log::LOG_INFO,
       std::format("OverlayDeployer '{}': Overlay mount active at '{}'",
                   name_, dest_path_.string()));

  // Overlay deployers do not compute per-mod disk sizes (no files are copied)
  return {};
}

// ---------------------------------------------------------------------------
// unDeploy
// ---------------------------------------------------------------------------

void OverlayDeployer::unDeploy(std::optional<ProgressNode*> progress_node)
{
  log_(Log::LOG_INFO,
       std::format("OverlayDeployer '{}': Undeploying overlay...", name_));
  doUnmount();
  if(progress_node)
    (*progress_node)->setTotalSteps(1);
  if(progress_node)
    (*progress_node)->advance();
}

// ---------------------------------------------------------------------------
// cleanup
// ---------------------------------------------------------------------------

void OverlayDeployer::cleanup()
{
  doUnmount();
  // Remove the overlay bookkeeping directory so orig/ is also cleared
  std::error_code ec;
  sfs::remove_all(overlayDir(), ec);
  if(ec)
    log_(Log::LOG_ERROR,
         std::format("OverlayDeployer '{}': Failed to remove overlay dir '{}': {}",
                     name_, overlayDir().string(), ec.message()));
}

// ---------------------------------------------------------------------------
// capability flags
// ---------------------------------------------------------------------------

bool OverlayDeployer::supportsSorting() const        { return true;  }
bool OverlayDeployer::supportsReordering() const     { return true;  }
bool OverlayDeployer::supportsModConflicts() const   { return true;  }
bool OverlayDeployer::supportsFileConflicts() const  { return true;  }
bool OverlayDeployer::supportsFileBrowsing() const   { return true;  }
