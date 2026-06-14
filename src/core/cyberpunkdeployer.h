/*!
 * \file cyberpunkdeployer.h
 * \brief Header for the CyberpunkDeployer class.
 */

#pragma once

#include "casematchingdeployer.h"


/*!
 * \brief Deployer for Cyberpunk 2077 mods which deploys into the game's root directory and
 * enforces a user controlled load order for \c .archive files.
 *
 * \section cp2077_model Cyberpunk 2077 deployment model
 * Most Cyberpunk 2077 mods downloaded from sites such as Nexus are packaged with their full
 * relative path from the game root already baked in, e.g. \c archive/pc/mod/foo.archive
 * (REDmod/archive mods), \c bin/x64/plugins/... (CET, ASI loader), \c red4ext/plugins/...,
 * \c r6/scripts/... and \c r6/tweaks/... (redscript / TweakXL) and \c engine/.... Because each
 * mod carries its own directory structure, a deployer that simply targets the game root routes
 * every file to the correct location without any per file path rewriting. This deployer therefore
 * performs no custom path routing for the general case.
 *
 * \section cp2077_archive_order The .archive load order (first wins)
 * The single ordering sensitive part of Cyberpunk modding is the set of \c .archive files placed
 * in \c archive/pc/mod/. The game loads these in <b>alphabetical filename order</b> and, for
 * conflicting resources, the <b>alphabetically first archive wins</b> (i.e. a lower position in
 * the on disk ordering corresponds to a higher priority). This is the opposite of The Witcher 3,
 * where the last loaded archive wins.
 *
 * To make Limo's load order authoritative with the usual "top of the list = highest priority =
 * wins" semantics, this deployer rewrites the deployed file name of every \c .archive under
 * \c archive/pc/mod/ to carry a zero padded numeric prefix derived from its position in the load
 * order. The mod at load order index \c i (0 based, where 0 is the top of the user's list) has its
 * archives deployed as <tt>{i:04d}_&lt;original filename&gt;</tt>. Alphabetical order then equals
 * load order, so the archive of the top mod (prefix \c 0000_ ) sorts first and therefore wins,
 * matching "top of list wins". The prefix is applied only to the final path component (the file
 * name); the containing directory (\c archive/pc/mod/ ) is preserved. Files anywhere else, and non
 * \c .archive files inside \c archive/pc/mod/, are deployed unchanged.
 *
 * \section cp2077_companion Companion files
 * Companion files such as \c foo.archive.xl (ArchiveXL) are loaded independently of the archive
 * load order and are matched to their archive by content, not by the \c .archive file name.
 * Prefixing only the \c .archive (and leaving any \c .xl untouched) is therefore the correct and
 * standard behaviour. See the implementation in \ref deploy for the precise rules applied.
 *
 * \section cp2077_impl Implementation
 * Because the load order index must be encoded into the deployed file name, the source relative
 * path and the destination relative path can differ. The base \ref Deployer.deploy
 * "Deployer::deploy" keys its entire pipeline on a single relative path used for both the source
 * and the destination, so it cannot by itself express this remapping. This class instead overrides
 * \ref deploy to build a destination keyed file map (with archive prefixes applied) together with a
 * destination to source path map, then drives the same backup/restore, deployment and
 * \c .lmmfiles manifest primitives used by the base class. This keeps the manifest consistent with
 * what is actually on disk; deployed files are never renamed after the fact. The case matching pass
 * of \ref CaseMatchingDeployer.deploy "CaseMatchingDeployer::deploy" is still run first.
 */
class CyberpunkDeployer : public CaseMatchingDeployer
{
public:
  /*!
   * \brief Passes arguments to base class constructor.
   * \param source_path Path to directory containing mods installed using the Installer class.
   * \param dest_path Path to the Cyberpunk 2077 root directory for mod deployment.
   * \param name A custom name for this instance.
   * \param deploy_mode Determines how files are deployed to the target directory.
   */
  CyberpunkDeployer(const std::filesystem::path& source_path,
                    const std::filesystem::path& dest_path,
                    const std::string& name,
                    DeployMode deploy_mode = hard_link);
  /*!
   * \brief Deploys all mods to the Cyberpunk 2077 root directory.
   *
   * Runs the case matching pass, then deploys every file. For any file whose relative path lies
   * under \c archive/pc/mod/ and ends in \c .archive, the deployed file name is rewritten to
   * <tt>{i:04d}_&lt;original filename&gt;</tt> where \c i is the mod's index in \p loadorder
   * (0 = top = highest priority = alphabetically first = wins). All other files are deployed with
   * their original relative path. Backups, restores and the \c .lmmfiles manifest are kept
   * consistent by mirroring the structure of \ref Deployer.deploy "Deployer::deploy".
   * \param loadorder A vector of mod ids representing the load order.
   * \param progress_node Used to inform about the current progress of deployment.
   * \return A map from deployed mod ids to their respective mods total size on disk.
   */
  virtual std::map<int, unsigned long> deploy(
    const std::vector<int>& loadorder,
    std::optional<ProgressNode*> progress_node = {}) override;
  /*! \brief Use base class implementation of overloaded function. */
  using CaseMatchingDeployer::deploy;

private:
  /*! \brief Relative directory under which ordering sensitive \c .archive files are placed. */
  static inline const std::filesystem::path ARCHIVE_MOD_DIR = "archive/pc/mod";
  /*! \brief File extension (lower case) of ordering sensitive archive files. */
  static inline const std::string ARCHIVE_EXTENSION = ".archive";

  /*!
   * \brief Builds the destination keyed deployment maps for the given load order, applying the
   * archive load order prefix to every \c .archive file under \c archive/pc/mod/.
   * \param loadorder The load order used for file checks.
   * \return A tuple of: a map from destination relative path to source mod id, a map from
   * destination relative path to the source relative path within that mod, and a map from mod ids
   * to their total file size on disk.
   */
  std::tuple<std::map<std::filesystem::path, int>,
             std::map<std::filesystem::path, std::filesystem::path>,
             std::map<int, unsigned long>>
  getDeploymentMaps(const std::vector<int>& loadorder) const;
  /*!
   * \brief Checks whether the given mod relative path refers to an ordering sensitive
   * \c .archive file, i.e. a file directly inside \c archive/pc/mod/ whose extension is
   * \c .archive (case insensitively).
   * \param relative_path Path relative to the mod's root directory.
   * \return True iff the path is an ordering sensitive archive.
   */
  bool isOrderedArchive(const std::filesystem::path& relative_path) const;
  /*!
   * \brief Returns the destination relative path for a source file, applying the archive load
   * order prefix if the file is an ordering sensitive \c .archive.
   * \param relative_path Path relative to the mod's root directory.
   * \param loadorder_index Index of the mod in the load order (0 = top = highest priority).
   * \return The destination relative path.
   */
  std::filesystem::path destinationPath(const std::filesystem::path& relative_path,
                                        int loadorder_index) const;
  /*!
   * \brief Deploys all given files. Mirrors \ref Deployer.deployFiles "Deployer::deployFiles" but
   * resolves the source path through \p source_paths instead of assuming source and destination
   * share a relative path.
   * \param dest_files A map from destination relative path to the source mod id.
   * \param source_paths A map from destination relative path to the source relative path.
   * \param progress_node Used to inform about the current progress of deployment.
   */
  void deployFilesWithRemap(
    const std::map<std::filesystem::path, int>& dest_files,
    const std::map<std::filesystem::path, std::filesystem::path>& source_paths,
    std::optional<ProgressNode*> progress_node = {}) const;
};
