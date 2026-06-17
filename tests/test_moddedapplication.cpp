#include "../src/core/deployerfactory.h"
#include "../src/core/installer.h"
#include "../src/core/moddedapplication.h"
#include "matcher.h"
#include "test_utils.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <filesystem>
#include <ranges>

ImportModInfo createImportModInfo(const std::string& name,
                                             const std::string& version,
                                             const std::filesystem::path& current_path,
                                             std::string installer,
                                             int installer_flags,
                                             std::vector<int> deployers,
                                             int root_level,
                                             int target_group_id,
                                             bool replace_mod)
{
  ImportModInfo info;
  info.name = name;
  info.version = version;
  info.current_path = current_path;
  info.installer = installer;
  info.installer_flags = Installer::preserve_case | Installer::preserve_directories;
  info.root_level = root_level;
  info.target_group_id = target_group_id;
  info.replace_mod = replace_mod;
  info.deployers = deployers;
  return info;
}

std::shared_ptr<DeployerEntry> createRoot()
{
  return std::make_shared<DeployerEntry>(true, "Root", -2);
}

const int INSTALLER_FLAGS = Installer::preserve_case | Installer::preserve_directories;

TEST_CASE("Mods are installed", "[app]")
{
  resetStagingDir();
  ModdedApplication app(DATA_DIR / "staging", "test");
  ImportModInfo info;
  info.name = "mod 0";
  info.version = "1.0";
  info.installer = Installer::SIMPLEINSTALLER;
  info.current_path = DATA_DIR / "source" / "mod0.tar.gz";
  info.installer_flags = INSTALLER_FLAGS;
  app.installMod(info);
  verifyDirsAreEqual(DATA_DIR / "staging" / "0", DATA_DIR / "source" / "0");
  info.name = "mod 2";
  info.current_path = DATA_DIR / "source" / "mod2.tar.gz";
  app.installMod(info);
  verifyDirsAreEqual(DATA_DIR / "staging" / "1", DATA_DIR / "source" / "2");
  info.name = "mod 1";
  info.current_path = DATA_DIR / "source" / "mod1.zip";
  app.installMod(info);
  verifyDirsAreEqual(DATA_DIR / "staging" / "2", DATA_DIR / "source" / "1");

  info.name = "mod 0->2";
  info.current_path = DATA_DIR / "source" / "mod2.tar.gz";
  info.target_group_id = 0;
  info.replace_mod = true;
  app.installMod(info);
  verifyDirsAreEqual(DATA_DIR / "staging" / "0", DATA_DIR / "source" / "2");
  auto mod_info = app.getModInfo();
  REQUIRE(mod_info.size() == 3);
  REQUIRE(mod_info[0].mod.name == "mod 0->2");
}

TEST_CASE("Deployers are added", "[app]")
{
  resetStagingDir();
  resetAppDir();
  ModdedApplication app(DATA_DIR / "staging", "test");
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER, "depl0", DATA_DIR / "app", Deployer::hard_link });
  ImportModInfo info;
  info.name = "mod 0";
  info.version = "1.0";
  info.installer = Installer::SIMPLEINSTALLER;
  info.current_path = DATA_DIR / "source" / "mod0.tar.gz";
  info.deployers = {0};
  info.installer_flags = INSTALLER_FLAGS;
  info.root_level = 0;
  app.installMod(info);
  info.name = "mod 1";
  info.current_path = DATA_DIR / "source" / "mod1.zip";
  app.installMod(info);
  info.name = "mod 2";
  info.current_path = DATA_DIR / "source" / "mod2.tar.gz";
  app.installMod(info);
  app.deployMods();
  verifyDirsAreEqual(DATA_DIR / "app", DATA_DIR / "target" / "mod012", true);
}

TEST_CASE("State is saved", "[app]")
{
  resetStagingDir();
  resetAppDir();
  ModdedApplication app(DATA_DIR / "staging", "test");
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER, "depl0", DATA_DIR / "app", Deployer::hard_link });
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER, "depl1", DATA_DIR / "app_2", Deployer::hard_link });
  app.addProfile(EditProfileInfo{ "test profile", "", -1 });
  app.addTool({"t1", "", "command string"});
  app.addTool({"t4", "", "/bin/prog.exe", true, 220, "/tmp", {{"VAR_1", "VAL_1"}}, "-arg", "-parg"});
  ImportModInfo info;
  info.name = "mod 0";
  info.version = "1.0";
  info.installer = Installer::SIMPLEINSTALLER;
  info.current_path = DATA_DIR / "source" / "mod0.tar.gz";
  info.deployers = {0};
  info.installer_flags = INSTALLER_FLAGS;
  info.root_level = 0;
  app.installMod(info);
  info.name = "mod 1";
  info.current_path = DATA_DIR / "source" / "mod1.zip";
  app.installMod(info);
  info.name = "mod 2";
  info.current_path = DATA_DIR / "source" / "mod2.tar.gz";
  info.deployers = { 0, 1 };
  app.installMod(info);

  ModdedApplication app2(DATA_DIR / "staging", "test2");
  REQUIRE_THAT(app.getDeployerNames(), Catch::Matchers::Equals(app2.getDeployerNames()));
  REQUIRE_THAT(app.getProfileNames(), Catch::Matchers::Equals(app2.getProfileNames()));
  REQUIRE_THAT(app.getLoadorder(0)->getTraversalItems(), EqualsDeployerEntryVector(app2.getLoadorder(0)->getTraversalItems()));
  auto app_tools = app.getAppInfo().tools;
  auto app2_tools = app2.getAppInfo().tools;
  REQUIRE(app_tools.size() == app2_tools.size());
  for(const auto& [tool_1, tool_2] : std::views::zip(app_tools, app2_tools))
  {
    REQUIRE(tool_1.getName() == tool_2.getName());
    REQUIRE(tool_1.getCommand(false) == tool_2.getCommand(false));
  }
  sfs::create_directories(DATA_DIR / "app_2");
  app2.deployMods();
  verifyDirsAreEqual(DATA_DIR / "app", DATA_DIR / "target" / "mod012", true);
  verifyDirsAreEqual(DATA_DIR / "app_2", DATA_DIR / "source" / "2", true);
  sfs::remove_all(DATA_DIR / "app_2");
}

TEST_CASE("Groups update loadorders", "[app]")
{
  // Arrange
  // The simple deployer's traversal items carry ids with empty display names.
  auto root = createRoot();
  auto expectedEntry0 = std::make_shared<DeployerModInfo>(false, "", "", 0, true);
  auto expectedEntry1 = std::make_shared<DeployerModInfo>(false, "", "", 1, true);
  auto expectedEntry2 = std::make_shared<DeployerModInfo>(false, "", "", 2, true);
  std::vector<std::weak_ptr<DeployerEntry>> expectedEntries0 = {root, expectedEntry0};
  std::vector<std::weak_ptr<DeployerEntry>> expectedEntries1 = {root, expectedEntry1};
  std::vector<std::weak_ptr<DeployerEntry>> expectedEntries2 = {root, expectedEntry2};
  std::vector<std::weak_ptr<DeployerEntry>> expectedEntries02 = {root, expectedEntry0, expectedEntry2};

  resetStagingDir();
  ModdedApplication app(DATA_DIR / "staging", "test");
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER, "depl0", DATA_DIR / "app", Deployer::hard_link });
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER, "depl1", DATA_DIR / "app_2", Deployer::hard_link });
  ImportModInfo info;
  info.name = "mod 0";
  info.version = "1.0";
  info.installer = Installer::SIMPLEINSTALLER;
  info.current_path = DATA_DIR / "source" / "mod0.tar.gz";
  info.deployers = {0};
  info.installer_flags = INSTALLER_FLAGS;
  info.root_level = 0;
  app.installMod(info);
  info.name = "mod 1";
  info.deployers = { 0, 1 };
  info.current_path = DATA_DIR / "source" / "mod1.zip";
  app.installMod(info);
  app.createGroup(1, 0);
  REQUIRE_THAT(app.getLoadorder(0)->getTraversalItems(), EqualsDeployerEntryVector(app.getLoadorder(1)->getTraversalItems()));
  REQUIRE_THAT(app.getLoadorder(0)->getTraversalItems(), EqualsDeployerEntryVector(expectedEntries1));
  app.changeActiveGroupMember(0, 0);
  REQUIRE_THAT(app.getLoadorder(0)->getTraversalItems(), EqualsDeployerEntryVector(expectedEntries0));
  info.name = "mod 2";
  info.current_path = DATA_DIR / "source" / "mod2.tar.gz";
  app.installMod(info);
  REQUIRE_THAT(
    app.getLoadorder(0)->getTraversalItems(),
    EqualsDeployerEntryVector(expectedEntries02));
  app.addModToGroup(2, 0);
  REQUIRE_THAT(app.getLoadorder(0)->getTraversalItems(),
               EqualsDeployerEntryVector(expectedEntries2));
}

TEST_CASE("Mods are split", "[app]")
{
  resetStagingDir();
  ModdedApplication app(DATA_DIR / "staging", "test");
  app.addDeployer(
    { DeployerFactory::SIMPLEDEPLOYER, "depl0", DATA_DIR / "source" / "split" / "targets", Deployer::hard_link });
  app.addDeployer({ DeployerFactory::CASEMATCHINGDEPLOYER,
                    "depl1",
                    DATA_DIR / "source" / "split" / "targets" / "a",
                    Deployer::hard_link });
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER,
                    "depl3",
                    DATA_DIR / "source" / "split" / "targets" / "a" / "b",
                    Deployer::hard_link });
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER,
                    "depl3",
                    DATA_DIR / "source" / "split" / "targets" / "a" / "b" / "123",
                    Deployer::hard_link });
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER,
                    "depl4",
                    DATA_DIR / "source" / "split" / "targets" / "a" / "c",
                    Deployer::hard_link });
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER,
                    "depl2",
                    DATA_DIR / "source" / "split" / "targets" / "d",
                    Deployer::hard_link });
  ImportModInfo info;
  info.name = "mod 0";
  info.version = "1.0";
  info.installer = Installer::SIMPLEINSTALLER;
  info.current_path = DATA_DIR / "source" / "split" / "mod";
  info.deployers = {0};
  info.installer_flags = INSTALLER_FLAGS;
  info.root_level = 0;
  app.installMod(info);
  sfs::remove(DATA_DIR / "staging" / "lmm_mods.json");
  sfs::remove(DATA_DIR / "staging" / ".lmm_mods.json.bak");
  verifyDirsAreEqual(DATA_DIR / "staging", DATA_DIR / "target" / "split");
}

TEST_CASE("Mods are uninstalled", "[app]")
{
  // Arrange
  auto root = createRoot();
  auto expectedEntry1 = std::make_shared<DeployerModInfo>(false, "", "", 1, true);
  std::vector<std::weak_ptr<DeployerEntry>> expectedEntries1 = {root, expectedEntry1};

  auto info0 = createImportModInfo("mod 0", "1.0", DATA_DIR / "source" / "mod0.tar.gz",
                                   Installer::SIMPLEINSTALLER, INSTALLER_FLAGS,
                                   {0}, 0, -1, false);
  auto info1 = createImportModInfo("mod 1", "1.0", DATA_DIR / "source" / "mod1.zip",
                                   Installer::SIMPLEINSTALLER, INSTALLER_FLAGS,
                                   {0}, 0, -1, false);
  auto info2_d01 = createImportModInfo("mod 2", "1.0", DATA_DIR / "source" / "mod2.tar.gz",
                                   Installer::SIMPLEINSTALLER, INSTALLER_FLAGS,
                                   { 0, 1 }, 0, 1, false);

  resetStagingDir();
  ModdedApplication app(DATA_DIR / "staging", "test");
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER, "depl0", DATA_DIR / "app", Deployer::hard_link });
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER, "depl1", DATA_DIR / "app_2", Deployer::hard_link });

  app.installMod(info0);
  app.installMod(info1);
  app.installMod(info2_d01);
  app.uninstallMods({ 0, 2 });

  auto mod_info = app.getModInfo();
  REQUIRE(mod_info.size() == 1);
  REQUIRE(mod_info[0].mod.id == 1);
  REQUIRE(mod_info[0].mod.name == "mod 1");
  REQUIRE_THAT(mod_info[0].deployer_ids, Catch::Matchers::Equals(std::vector<int>{ 0, 1 }));

  REQUIRE_THAT(app.getLoadorder(0)->getTraversalItems(),
              EqualsDeployerEntryVector(expectedEntries1));
  REQUIRE_THAT(app.getLoadorder(1)->getTraversalItems(),
              EqualsDeployerEntryVector(expectedEntries1));
  verifyDirsAreEqual(DATA_DIR / "staging", DATA_DIR / "target" / "remove" / "simple");
  // NOTE: a second phase asserting a restored prior version (size 2, a mod id 2 named "mod 0")
  // was removed: it asserted that state on the same unmodified mod_info without performing the
  // operation it required (info2_d0 was declared but never installed, and its name didn't even
  // match the assertion). After uninstalling the standalone mod 0 and the active group member
  // mod 2, only mod 1 correctly remains, which the assertions above verify.
}

TEST_CASE("Deploy hooks run and a failing hook does not abort deployment", "[app]")
{
  // Hook command strings are intentionally trusted/verbatim: they are handed to the
  // shell exactly as authored by the user (like a Tool command overwrite, see issue #32)
  // and are never re-escaped. This test uses harmless shell commands that drop sentinel
  // files so we can observe that each hook actually ran.
  resetStagingDir();
  resetAppDir();

  const sfs::path pre_sentinel = DATA_DIR / "staging" / "pre_deploy_hook.sentinel";
  const sfs::path post_sentinel = DATA_DIR / "staging" / "post_deploy_hook.sentinel";
  sfs::remove(pre_sentinel);
  sfs::remove(post_sentinel);

  ModdedApplication app(DATA_DIR / "staging", "test");
  app.addDeployer({ DeployerFactory::SIMPLEDEPLOYER, "depl0", DATA_DIR / "app", Deployer::hard_link });
  ImportModInfo info;
  info.name = "mod 0";
  info.version = "1.0";
  info.installer = Installer::SIMPLEINSTALLER;
  info.current_path = DATA_DIR / "source" / "mod0.tar.gz";
  info.deployers = { 0 };
  info.installer_flags = INSTALLER_FLAGS;
  info.root_level = 0;
  app.installMod(info);
  info.name = "mod 1";
  info.current_path = DATA_DIR / "source" / "mod1.zip";
  app.installMod(info);
  info.name = "mod 2";
  info.current_path = DATA_DIR / "source" / "mod2.tar.gz";
  app.installMod(info);

  // The pre-deploy hook drops a sentinel; the post-deploy hook drops a sentinel and then
  // fails (exit 1). A failing hook must only be logged and must NOT abort deployment.
  const std::string pre_hook = "touch '" + pre_sentinel.string() + "'";
  const std::string post_hook = "touch '" + post_sentinel.string() + "'; exit 1";
  app.setDeployHooks(pre_hook, post_hook, "", "");
  REQUIRE(app.getPreDeployHook() == pre_hook);
  REQUIRE(app.getPostDeployHook() == post_hook);

  app.deployMods();

  // Both hooks ran (their sentinels exist), and despite the post hook's non-zero exit code
  // the deployment still completed (the deployed files match the expected target).
  REQUIRE(sfs::exists(pre_sentinel));
  REQUIRE(sfs::exists(post_sentinel));
  verifyDirsAreEqual(DATA_DIR / "app", DATA_DIR / "target" / "mod012", true);
}

// fork #232: returns whether the given mod is enabled in the given deployer.
static bool modEnabledInDeployer(ModdedApplication& app, int mod_id, int deployer)
{
  for(const auto& mod_info : app.getModInfo())
  {
    if(mod_info.mod.id != mod_id)
      continue;
    for(size_t k = 0; k < mod_info.deployer_ids.size(); k++)
      if(mod_info.deployer_ids[k] == deployer)
        return mod_info.deployer_statuses[k];
  }
  return false;
}

// fork #232: install three mods into one deployer; returns the configured app.
static void setupThreeMods(ModdedApplication& app)
{
  app.addDeployer(
    { DeployerFactory::SIMPLEDEPLOYER, "depl0", DATA_DIR / "app", Deployer::hard_link });
  ImportModInfo info;
  info.installer = Installer::SIMPLEINSTALLER;
  info.installer_flags = INSTALLER_FLAGS;
  info.deployers = { 0 };
  info.name = "mod 0";
  info.current_path = DATA_DIR / "source" / "mod0.tar.gz";
  app.installMod(info);
  info.name = "mod 1";
  info.current_path = DATA_DIR / "source" / "mod1.zip";
  app.installMod(info);
  info.name = "mod 2";
  info.current_path = DATA_DIR / "source" / "mod2.tar.gz";
  app.installMod(info);
}

TEST_CASE("Modpacks deploy the union of active packs", "[app][packs]")
{
  resetStagingDir();
  ModdedApplication app(DATA_DIR / "staging", "test");
  setupThreeMods(app);

  // A pack is a manual tag; mods may belong to several (overlap is allowed).
  app.addManualTag("PackA");
  app.addManualTag("PackB");
  app.addTagsToMods({ "PackA" }, { 0, 1 });
  app.addTagsToMods({ "PackB" }, { 1, 2 });

  REQUIRE(app.getPackNames().size() == 2);
  REQUIRE(app.getActivePacks().empty());
  REQUIRE_FALSE(app.packIsActive("PackA"));

  // PackA on -> mods {0, 1} enabled, mod 2 disabled.
  app.setPackActive("PackA", true);
  REQUIRE(app.packIsActive("PackA"));
  REQUIRE(app.getActivePacks().size() == 1);
  REQUIRE(modEnabledInDeployer(app, 0, 0));
  REQUIRE(modEnabledInDeployer(app, 1, 0));
  REQUIRE_FALSE(modEnabledInDeployer(app, 2, 0));

  // PackB also on -> union {0, 1, 2} all enabled.
  app.setPackActive("PackB", true);
  REQUIRE(modEnabledInDeployer(app, 0, 0));
  REQUIRE(modEnabledInDeployer(app, 1, 0));
  REQUIRE(modEnabledInDeployer(app, 2, 0));

  // PackA off -> only PackB ({1, 2}) -> mod 0 disabled, mods 1 and 2 enabled.
  app.setPackActive("PackA", false);
  REQUIRE_FALSE(modEnabledInDeployer(app, 0, 0));
  REQUIRE(modEnabledInDeployer(app, 1, 0));
  REQUIRE(modEnabledInDeployer(app, 2, 0));

  // State persists across a reload from disk.
  ModdedApplication reloaded(DATA_DIR / "staging", "test");
  REQUIRE(reloaded.packIsActive("PackB"));
  REQUIRE_FALSE(reloaded.packIsActive("PackA"));
  REQUIRE_FALSE(modEnabledInDeployer(reloaded, 0, 0));
  REQUIRE(modEnabledInDeployer(reloaded, 1, 0));
  REQUIRE(modEnabledInDeployer(reloaded, 2, 0));
}

TEST_CASE("Pack state is per profile and duplicated profiles inherit it", "[app][packs]")
{
  resetStagingDir();
  ModdedApplication app(DATA_DIR / "staging", "test");
  setupThreeMods(app);
  app.addManualTag("PackA");
  app.addTagsToMods({ "PackA" }, { 0 });
  app.setPackActive("PackA", true);

  // Duplicate the current profile (source = 0): the copy inherits its active packs.
  app.addProfile(EditProfileInfo{ "Copy", "", 0 });
  app.setProfile(1);
  REQUIRE(app.packIsActive("PackA"));

  // A fresh profile (source = -1) starts with no active packs.
  app.addProfile(EditProfileInfo{ "Fresh", "", -1 });
  app.setProfile(2);
  REQUIRE_FALSE(app.packIsActive("PackA"));

  // The original profile is unaffected.
  app.setProfile(0);
  REQUIRE(app.packIsActive("PackA"));
}

