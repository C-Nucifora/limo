/*!
 * \file cyberpunksetup.cpp
 * \brief Implementation of the cyberpunk_setup namespace.
 */

#include "cyberpunksetup.h"

#include <string>
#include <sys/stat.h>
#include <system_error>
#include <vector>

namespace sfs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

/*!
 * \brief Deploy mode integer values — mirrors Deployer::DeployMode without
 *        including deployer.h.
 *
 * Keep in sync with Deployer::DeployMode in deployer.h.
 *
 * // TODO(cp-setup): If a shared enum is introduced (e.g. in a dedicated
 * //                 types header), replace these literals with that enum.
 */
constexpr int DEPLOY_MODE_HARD_LINK = 0;
constexpr int DEPLOY_MODE_SYM_LINK  = 1;
// constexpr int DEPLOY_MODE_COPY   = 2;  // currently unused; copy is always safe

/*!
 * \brief Returns the device ID for \p path, or 0 on error.
 *
 * Uses std::filesystem::status() which does not follow symlinks for the stat
 * call on the *path itself*, but does follow for directory entries.  This is
 * acceptable here because we only need the mount-point device for the two
 * directory roots.
 *
 * \param path Path to query.
 * \return st_dev value from stat, or 0 if the path does not exist / stat fails.
 */
std::uintmax_t deviceId(const sfs::path& path)
{
  // std::filesystem::space_info does not expose device IDs, so use ::stat via
  // the POSIX API.  ::stat already returns non-zero (and sets errno) when the
  // path does not exist or cannot be queried, so a single call covers both the
  // existence check and the device lookup.
  struct ::stat buf {};
  if(::stat(path.c_str(), &buf) != 0)
    return 0;
  return static_cast<std::uintmax_t>(buf.st_dev);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace cyberpunk_setup
{

std::string protontricksCommand()
{
  return std::string("protontricks ") + std::to_string(CYBERPUNK_STEAM_APP_ID) + " " +
         PROTONTRICKS_DEPENDENCIES;
}

std::vector<std::string> setupChecklist()
{
  return {
    // 1 — launch options
    std::string("Set Steam launch options to: ") + REQUIRED_LAUNCH_OPTIONS,
    // 2 — protontricks
    std::string("Install Proton prefix dependencies via: ") + protontricksCommand(),
    // 3 — deploy mode
    "Use Hard Link (or Copy) deploy mode — avoid Symbolic Link mode for the Cyberpunk "
    "deployer; symlinks break CET / RED4ext DLL proxying.",
    // 4 — filesystem boundary
    "Ensure the Limo staging directory and the Cyberpunk game directory are on the same "
    "filesystem partition so that hard links can be created.",
    // 5 — load order reminder
    "Place CET (Cyber Engine Tweaks) mods before RED4ext mods in the load order so "
    "that CET initialises first.",
  };
}

std::optional<std::string> checkCyberpunkDeployment(int deploy_mode,
                                                    const sfs::path& staging_path,
                                                    const sfs::path& target_path)
{
  // Symlink mode is never safe for Cyberpunk script extenders.
  if(deploy_mode == DEPLOY_MODE_SYM_LINK)
  {
    return "Symbolic link deploy mode is incompatible with Cyber Engine Tweaks (CET) and "
           "RED4ext. These mods rely on DLL proxy loading, which requires real files (or "
           "hard links) in the game directory — symlinks are not reliably followed by "
           "Wine/Proton for this use case. Switch the deployer to Hard Link mode (or Copy "
           "mode as a fallback).";
  }

  // Hard link mode is ideal, but fails if staging and target are on different devices.
  if(deploy_mode == DEPLOY_MODE_HARD_LINK)
  {
    // Only perform the cross-device check when both paths exist; during initial
    // setup the directories may not yet have been created.
    const std::uintmax_t src_dev = deviceId(staging_path);
    const std::uintmax_t dst_dev = deviceId(target_path);

    if(src_dev != 0 && dst_dev != 0 && src_dev != dst_dev)
    {
      return "The staging directory (" + staging_path.string() +
             ") and the game directory (" + target_path.string() +
             ") are on different filesystems. Hard links cannot cross filesystem "
             "boundaries — deployment will fail. Either move the staging directory "
             "to the same partition as the game, or switch the deployer to Copy mode.";
    }
    // Paths don't exist yet — skip cross-device check silently.
    // TODO(cp-setup): Re-run this check at first-deploy time when both paths exist.
  }

  // copy mode (deploy_mode == 2) is always safe — no warning needed.
  return std::nullopt;
}

std::vector<std::string> missingPrefixPrerequisites(const sfs::path& proton_prefix)
{
  const sfs::path system32 = proton_prefix / "drive_c" / "windows" / "system32";

  // Pairs of { dll_filename, human-readable description }
  // TODO(cp-setup): Confirm the canonical vcrun2022 marker file.  msvcp140.dll is
  //                 installed by the VC++ 2015-2022 redistributable and is a reliable
  //                 proxy on all tested prefixes, but vcruntime140.dll would also be
  //                 acceptable as a second opinion.
  const std::vector<std::pair<std::string, std::string>> expected_files = {
    {"d3dcompiler_47.dll",
     "d3dcompiler_47 (required by Cyber Engine Tweaks' embedded Chromium renderer)"},
    {"msvcp140.dll",
     "vcrun2022 / Visual C++ 2022 runtime (required by RED4ext and native DLL mods; "
     "detected via msvcp140.dll)"},
  };

  std::vector<std::string> missing;
  for(const auto& [filename, description] : expected_files)
  {
    std::error_code ec;
    if(!sfs::exists(system32 / filename, ec) || ec)
      missing.push_back(description);
  }
  return missing;
}

} // namespace cyberpunk_setup
