/*!
 * \file overlaydeployer.h
 * \brief Header for the OverlayDeployer class.
 *
 * Implements a VFS/overlay-based deployer using fuse-overlayfs (rootless) so the game
 * directory is never physically modified.  Relates to limo fork issue #110 /
 * upstream limo-app/limo#158 and #77.
 *
 * On deploy  : builds a fuse-overlayfs mount at dest_path_ with the enabled mods'
 *              staging directories as lowerdirs (highest-priority mod first) and the
 *              game's original files as the bottom-most lowerdir.  A work/upper dir
 *              pair stored under source_path_/.overlay/ handles writes made by the
 *              game at runtime so they end up in the upper layer, not the lowerdirs.
 *
 * On undeploy: unmounts the overlay (fusermount3 -u / umount -l) and the game dir
 *              is automatically restored to its original state.
 *
 * Availability: fuse-overlayfs is detected at runtime via PATH lookup.  If absent
 * the deploy() call throws a descriptive std::runtime_error.
 *
 * State file  : source_path_/.overlay/state.json — records whether a mount is live
 *               so that cleanup/restart can unmount correctly.
 */

#pragma once

#include "deployer.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>


/*!
 * \brief Deployer that presents the merged mod tree to the game via a fuse-overlayfs
 * union mount, leaving the actual game directory untouched on disk.
 *
 * The deployer is \e not autonomous: it participates in the standard load-order
 * workflow managed by ModdedApplication / the UI, just like SimpleDeployer and
 * CaseMatchingDeployer.
 */
class OverlayDeployer : public Deployer
{
public:
  /*!
   * \brief Constructor — mirrors the Deployer base signature.
   * \param source_path  Directory that holds per-mod staging subdirectories
   *                     (same convention as SimpleDeployer: source_path/\<mod_id\>/).
   * \param dest_path    The game directory that will be used as the mount point.
   * \param name         User-visible name for this deployer instance.
   * \param deploy_mode  Accepted for interface compatibility; overlay mounts are
   *                     always used regardless of this value.
   */
  OverlayDeployer(const std::filesystem::path& source_path,
                  const std::filesystem::path& dest_path,
                  const std::string& name,
                  DeployMode deploy_mode = hard_link);

  /*!
   * \brief Mounts a fuse-overlayfs overlay at dest_path_ merging the enabled mods.
   *
   * Steps:
   *  1. If a previous overlay is still mounted, unmount it first.
   *  2. Build the lowerdir list: enabled mods in reverse load-order (highest
   *     priority last in fuse-overlayfs convention) followed by the original
   *     game content snapshot dir.
   *  3. Run: fuse-overlayfs -o lowerdir=…,upperdir=…,workdir=… \<dest_path_\>
   *  4. Persist mount state to the state file.
   *
   * \param loadorder    Mod IDs to deploy, in load order (index 0 = lowest priority).
   * \param progress_node Used to report progress.
   * \return Empty map — overlay deployers do not report per-mod sizes.
   * \throws std::runtime_error if fuse-overlayfs is not found in PATH or the mount fails.
   */
  std::map<int, unsigned long> deploy(
    const std::vector<int>& loadorder,
    std::optional<ProgressNode*> progress_node = {}) override;

  /*! \brief Calls deploy() with the internally stored load order. */
  using Deployer::deploy;

  /*!
   * \brief Unmounts the overlay at dest_path_ restoring the game directory.
   * \param progress_node Used to report progress.
   */
  void unDeploy(std::optional<ProgressNode*> progress_node = {}) override;

  /*!
   * \brief Unmounts any live overlay and removes persistent state, then calls
   *        the base cleanup().
   */
  void cleanup() override;

  // ---- capability flags -------------------------------------------------------

  /*! \return True — load ordering affects which mod wins file conflicts. */
  bool supportsReordering() const override;
  /*! \return True — conflict detection is done on the source staging dirs. */
  bool supportsModConflicts() const override;
  /*! \return True. */
  bool supportsFileConflicts() const override;
  /*! \return True. */
  bool supportsFileBrowsing() const override;
  /*! \return True. */
  bool supportsSorting() const override;

private:
  /*! \brief Sub-directory inside source_path_ used for overlay bookkeeping. */
  const std::string overlay_dir_name_ = ".overlay";
  /*! \brief fuse-overlayfs upper layer (writable runtime writes from the game). */
  const std::string upper_dir_name_   = "upper";
  /*! \brief fuse-overlayfs work directory (required by the kernel/fuse protocol). */
  const std::string work_dir_name_    = "work";
  /*!
   * \brief Snapshot of the original game directory, used as the bottom-most
   * lowerdir so the game still sees its own files through the overlay.
   */
  const std::string orig_dir_name_    = "orig";
  /*! \brief Name of the JSON file that records mount state. */
  const std::string state_file_name_  = "state.json";

  /*! \brief Full path to the overlay bookkeeping directory. */
  std::filesystem::path overlayDir() const;
  /*! \brief Full path to the upper layer directory. */
  std::filesystem::path upperDir()   const;
  /*! \brief Full path to the work directory. */
  std::filesystem::path workDir()    const;
  /*! \brief Full path to the original-game-content snapshot directory. */
  std::filesystem::path origDir()    const;
  /*! \brief Full path to the state JSON file. */
  std::filesystem::path stateFile()  const;

  /*!
   * \brief Locates fuse-overlayfs in PATH.
   * \return Absolute path to the binary, or empty string if not found.
   */
  static std::string findFuseOverlayfs();

  /*!
   * \brief Returns true if dest_path_ is currently a FUSE/overlay mount point.
   *
   * Reads /proc/mounts and checks for an entry whose mount point matches dest_path_.
   */
  bool isMounted() const;

  /*!
   * \brief Unmounts dest_path_ using fusermount3 or umount -l.
   * Does nothing (logs a warning) if not currently mounted.
   */
  void doUnmount();

  /*!
   * \brief Ensures the overlay bookkeeping directories exist and, on first use,
   * snapshots the original game directory into origDir().
   */
  void ensureOverlayDirs();

  /*!
   * \brief Writes the current mount state to the state JSON file.
   * \param mounted Whether an overlay is currently active.
   */
  void saveState(bool mounted) const;

  /*!
   * \brief Reads the state JSON and returns whether an overlay was last known
   * to be mounted (used across restarts to clean up stale mounts).
   * \return True if the state file records a live mount.
   */
  bool loadState() const;

  /*!
   * \brief Runs an external command and returns {exit_code, combined_output}.
   * \param cmd  Full command string passed to /bin/sh -c.
   * \return Pair of (exit code, stdout+stderr output).
   */
  static std::pair<int, std::string> runCommand(const std::string& cmd);

  /*!
   * \brief POSIX-escapes an arbitrary string for safe use as a single shell word.
   *
   * Wraps the value in single quotes and turns every embedded ' into '\\'' so that
   * paths containing quotes, spaces or shell metacharacters cannot break out of the
   * argument and inject commands when passed to runCommand()/popen().
   * \param value The raw string (e.g. a filesystem path).
   * \return A shell-safe, single-quoted token.
   */
  static std::string shellEscape(const std::string& value);
};
