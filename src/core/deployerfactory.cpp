#include "deployerfactory.h"
#include "bg3deployer.h"
#include "casematchingdeployer.h"
#include "reversedeployer.h"
#ifdef LIMO_WITH_LOOT
#include "lootdeployer.h"
#include "openmwarchivedeployer.h"
#include "openmwplugindeployer.h"
#endif


std::unique_ptr<Deployer> DeployerFactory::makeDeployer(const std::string& type,
                                                        const std::filesystem::path& source_path,
                                                        const std::filesystem::path& dest_path,
                                                        const std::string& name,
                                                        Deployer::DeployMode deploy_mode,
                                                        bool separate_profile_dirs,
                                                        bool update_ignore_list)
{
  if(type == SIMPLEDEPLOYER)
    return std::make_unique<Deployer>(source_path, dest_path, name, deploy_mode);
  else if(type == CASEMATCHINGDEPLOYER)
    return std::make_unique<CaseMatchingDeployer>(source_path, dest_path, name, deploy_mode);
#ifdef LIMO_WITH_LOOT
  else if(type == LOOTDEPLOYER)
    return std::make_unique<LootDeployer>(source_path, dest_path, name);
#endif
  else if(type == REVERSEDEPLOYER)
    return std::make_unique<ReverseDeployer>(
      source_path, dest_path, name, deploy_mode, separate_profile_dirs, update_ignore_list);
#ifdef LIMO_WITH_LOOT
  else if(type == OPENMWARCHIVEDEPLOYER)
    return std::make_unique<OpenMwArchiveDeployer>(source_path, dest_path, name);
  else if(type == OPENMWPLUGINDEPLOYER)
    return std::make_unique<OpenMwPluginDeployer>(source_path, dest_path, name);
#endif
  else if(type == BG3DEPLOYER)
    return std::make_unique<Bg3Deployer>(source_path, dest_path, name);
  else
    throw std::runtime_error("Unknown deployer type \"" + type + "\"!");
}
