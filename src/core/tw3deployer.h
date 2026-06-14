/*!
 * \file tw3deployer.h
 * \brief Header for the Tw3Deployer class.
 */

#pragma once

#include "casematchingdeployer.h"


/*!
 * \brief Deploys mods into The Witcher 3's \p Mods directory while enforcing Limo's load order.
 *
 * \section tw3_ordering The Witcher 3 ordering model
 * The Witcher 3 (next-gen) loads every direct subfolder of its \p Mods directory whose name
 * starts with \p mod. These folders are loaded in alphabetical order of their folder name.
 * When two mods provide conflicting bundled resources, the mod contained in the
 * alphabetically-later folder wins, i.e. the last loaded mod overrides earlier ones. This is
 * why mod managers such as tw3mm and Vortex assign numeric folder-name prefixes to control
 * priority.
 *
 * In Limo, the load order is authoritative: a mod that appears \e later in the load order vector
 * is considered to have a higher priority and therefore must end up in an alphabetically-later
 * folder on disk so the game resolves conflicts in Limo's favor.
 *
 * \section tw3_approach Approach
 * This deployer derives from \ref CaseMatchingDeployer so it keeps the case-adapting behavior the
 * Witcher 3's case-sensitive Proton/Linux file system benefits from. It overrides
 * \ref deploy(const std::vector<int>&, std::optional<ProgressNode*>) "deploy()" to rewrite the
 * top-level \p mod* folder name of every mod such that the on-disk alphabetical order matches the
 * load order.
 *
 * For the mod at index \p i (0-based) in the load order, a top-level folder \p modFoo is deployed
 * as <tt>mod{i:04d}_Foo</tt>: the leading \p mod the game requires is kept, a 4-digit zero-padded
 * index plus an underscore is inserted right after it, and the original leading \p mod is dropped
 * from the remainder. For example, at index 3 the folder \p modFoo becomes \p mod0003_Foo. A mod
 * may contain more than one top-level \p mod* folder; each one is prefixed independently.
 *
 * Files which are \e not located under a top-level \p mod* folder are deployed unchanged. If a
 * staged mod's top level contains no \p mod* folder at all (for example a mod staged directly as
 * \p content/), every such top-level entry is wrapped in a single synthesized folder named
 * <tt>mod{i:04d}_{mod_id}</tt> so the game still recognizes and orders it. See
 * \ref buildLoadOrderPathMap for details.
 *
 * \section tw3_impl Implementation notes
 * Rather than renaming folders on disk \e after a regular deployment (which would desync the
 * \p .lmmfiles manifest from the mods' source directories), this class builds a corrected
 * source-relative to destination-relative path map \e before deployment and mirrors the bookkeeping
 * of \ref Deployer::deploy. Backups, restores and the deployed-files manifest are all keyed on the
 * rewritten destination-relative paths, so they remain consistent while the files are still read
 * from their unmodified source locations.
 */
class Tw3Deployer : public CaseMatchingDeployer
{
public:
  /*!
   * \brief Passes arguments to the base class constructor.
   * \param source_path Path to directory containing mods installed using the Installer class.
   * \param dest_path Path to the target directory, i.e. The Witcher 3's \p Mods directory.
   * \param name A custom name for this instance.
   * \param deploy_mode Determines how files are deployed to the target directory.
   */
  Tw3Deployer(const std::filesystem::path& source_path,
              const std::filesystem::path& dest_path,
              const std::string& name,
              DeployMode deploy_mode = hard_link);
  /*!
   * \brief Deploys all mods to the \p Mods directory, rewriting every top-level \p mod* folder
   * name to include a zero-padded load-order index prefix so that the on-disk alphabetical
   * folder order matches the given load order.
   *
   * Runs the case-matching pass inherited from \ref CaseMatchingDeployer, updates conflict
   * groups, then deploys using a corrected destination-path map. See the class description for
   * the full ordering model and prefixing rules.
   * \param loadorder A vector of mod ids representing the load order. Mods later in this vector
   * have higher priority and are placed in alphabetically-later folders.
   * \param progress_node Used to inform about the current progress of deployment.
   * \return A map from deployed mod ids to their respective mods total size on disk.
   */
  virtual std::map<int, unsigned long> deploy(
    const std::vector<int>& loadorder,
    std::optional<ProgressNode*> progress_node = {}) override;
  /*! \brief Use base class implementation of the overloaded function. */
  using CaseMatchingDeployer::deploy;

private:
  /*! \brief Number of digits used for the zero-padded load-order folder prefix. */
  static constexpr int LOAD_ORDER_PREFIX_DIGITS = 4;

  /*!
   * \brief Rewrites the first component of a destination-relative path so that the top-level
   * \p mod* folder includes the given load-order index prefix.
   *
   * If the first component case insensitively starts with \p mod, the leading \p mod is replaced
   * with <tt>mod{index:04d}_</tt>, e.g. \p modFoo at index 3 becomes \p mod0003_Foo. Components
   * which do not start with \p mod are returned unchanged.
   * \param relative_path Path relative to a mod's root directory.
   * \param index Load-order index (0-based) of the mod containing the file.
   * \return The rewritten destination-relative path.
   */
  std::filesystem::path rewriteTopLevelModFolder(const std::filesystem::path& relative_path,
                                                 int index) const;
  /*!
   * \brief Builds the maps required to deploy the given load order with rewritten folder names.
   *
   * Mirrors \ref Deployer::getDeploymentSourceFilesAndModSizes, but additionally rewrites every
   * top-level \p mod* folder name to contain the load-order index prefix. Mods which contain no
   * top-level \p mod* folder are wrapped in a synthesized <tt>mod{index:04d}_{mod_id}</tt> folder.
   * \param loadorder The load order used for file checks.
   * \return A tuple containing:
   * - A map from \e destination-relative file paths to the mod id from which they are deployed.
   * - A map from \e destination-relative file paths to their \e source-relative file paths.
   * - A map from mod ids to their total file size on disk.
   */
  std::tuple<std::map<std::filesystem::path, int>,
             std::map<std::filesystem::path, std::filesystem::path>,
             std::map<int, unsigned long>>
  buildLoadOrderPathMap(const std::vector<int>& loadorder) const;
  /*!
   * \brief Deploys all given files, reading each file from its source-relative path and writing
   * it to its (potentially rewritten) destination-relative path.
   *
   * Equivalent to \ref Deployer::deployFiles, but decouples the source location of a file from its
   * destination location so folder names can be rewritten on deployment.
   * \param source_files Map of destination-relative file paths to their source mod ids.
   * \param path_map Map of destination-relative file paths to their source-relative file paths.
   * \param progress_node Used to inform about the current progress of deployment.
   */
  void deployFilesWithPathMap(
    const std::map<std::filesystem::path, int>& source_files,
    const std::map<std::filesystem::path, std::filesystem::path>& path_map,
    std::optional<ProgressNode*> progress_node = {}) const;
};
