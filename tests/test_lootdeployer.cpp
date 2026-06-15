#include "../src/core/lootdeployer.h"
#include "matcher.h"
#include "test_utils.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>


void resetFiles()
{
  const sfs::path plugin_target = DATA_DIR / "target" / "loot" / "target" / "plugins.txt";
  const sfs::path plugin_source = DATA_DIR / "source" / "loot" / "plugins.txt";
  const sfs::path load_order_target = DATA_DIR / "target" / "loot" / "target" / "loadorder.txt";
  const sfs::path load_order_source = DATA_DIR / "source" / "loot" / "loadorder.txt";
  for(const auto& dir_entry : sfs::directory_iterator(DATA_DIR / "target" / "loot" / "target"))
    sfs::remove(dir_entry.path());
  sfs::copy(plugin_source, plugin_target);
  sfs::copy(load_order_source, load_order_target);
}


TEST_CASE("State is read", "[loot]")
{
  // Arrange
  auto root = std::make_shared<DeployerEntry>(true, "Root", -2);
  auto expectedEntry0 = std::make_shared<DeployerModInfo>(false, "a.esp", "", -1, true);
  auto expectedEntry1 = std::make_shared<DeployerModInfo>(false, "c.esp", "", -1, false);
  auto expectedEntry2 = std::make_shared<DeployerModInfo>(false, "d.esp", "", -1, true);
  auto expectedEntry3 = std::make_shared<DeployerModInfo>(false, "Morrowind.esm", "", -1, true);
  std::vector<std::weak_ptr<DeployerEntry>> expectedEntries = {
    root,
    expectedEntry0,
    expectedEntry1,
    expectedEntry2,
    expectedEntry3
  };

  resetFiles();
  LootDeployer depl(
    DATA_DIR / "target" / "loot" / "source", DATA_DIR / "target" / "loot" / "target", "", false);
  REQUIRE(depl.getNumMods() == 4);
  REQUIRE_THAT(depl.getModNames(),
               Catch::Matchers::Equals(std::vector<std::string>{ "a.esp", "c.esp", "d.esp", "Morrowind.esm" }));
  REQUIRE_THAT(depl.getLoadorder()->getTraversalItems(),
               EqualsDeployerEntryVector(expectedEntries));
}

TEST_CASE("Load order can be edited", "[loot]")
{
  auto root = std::make_shared<DeployerEntry>(true, "Root", -2);
  auto expectedEntry0 = std::make_shared<DeployerModInfo>(false, "d.esp", "", -1, false);
  auto expectedEntry1 = std::make_shared<DeployerModInfo>(false, "a.esp", "", -1, true);
  auto expectedEntry2 = std::make_shared<DeployerModInfo>(false, "c.esp", "", -1, true);
  auto expectedEntry3 = std::make_shared<DeployerModInfo>(false, "Morrowind.esm", "", -1, true);
  std::vector<std::weak_ptr<DeployerEntry>> expectedEntries = {
    root,
    expectedEntry0,
    expectedEntry1,
    expectedEntry2,
    expectedEntry3
  };

  resetFiles();
  LootDeployer depl(
    DATA_DIR / "target" / "loot" / "source", DATA_DIR / "target" / "loot" / "target", "", false);
  depl.swapChild(0, 2);
  depl.setModStatus(1, true);
  depl.setModStatus(0, false);
  depl.swapChild(2, 1);
  REQUIRE_THAT(depl.getModNames(),
               Catch::Matchers::Equals(std::vector<std::string>{ "d.esp", "a.esp", "c.esp", "Morrowind.esm" }));
  REQUIRE_THAT(depl.getLoadorder()->getTraversalItems(),
               EqualsDeployerEntryVector(expectedEntries));
  LootDeployer depl2(
    DATA_DIR / "target" / "loot" / "source", DATA_DIR / "target" / "loot" / "target", "", false);
  REQUIRE_THAT(depl.getModNames(), Catch::Matchers::Equals(depl2.getModNames()));
  REQUIRE_THAT(depl.getLoadorder()->getTraversalItems(), EqualsDeployerEntryVector(depl2.getLoadorder()->getTraversalItems()));
}

TEST_CASE("Profiles are managed", "[loot]")
{
  auto root = std::make_shared<DeployerEntry>(true, "Root", -2);
  // a.esp starts enabled, c.esp starts disabled, d.esp enabled, Morrowind.esm enabled
  auto expectedAespDisabled = std::make_shared<DeployerModInfo>(false, "a.esp", "", -1, false);
  auto expectedAespEnabled  = std::make_shared<DeployerModInfo>(false, "a.esp", "", -1, true);
  auto expectedCesp = std::make_shared<DeployerModInfo>(false, "c.esp", "", -1, false);
  auto expectedDesp = std::make_shared<DeployerModInfo>(false, "d.esp", "", -1, true);
  auto expectedMorrowind = std::make_shared<DeployerModInfo>(false, "Morrowind.esm", "", -1, true);
  // Profile 0 after setModStatus(0, false): a.esp disabled
  std::vector<std::weak_ptr<DeployerEntry>> expectedEntries0 = {
    root,
    expectedAespDisabled,
    expectedCesp,
    expectedDesp,
    expectedMorrowind
  };
  // Profile 1 (copy of original before modification): a.esp enabled
  std::vector<std::weak_ptr<DeployerEntry>> expectedEntries1 = {
    root,
    expectedAespEnabled,
    expectedCesp,
    expectedDesp,
    expectedMorrowind
  };
  // Profile 2: LootDeployer::addProfile has condition (source <= num_profiles_ && num_profiles_ > 1),
  // without the (source != current_profile) check from the base class.
  // When addProfile(0) is called in profile 0 with num_profiles_=1, condition fails (1 > 1 is false),
  // so profile 1 is a copy of the current live files.
  // After setProfile(1), profile 0's hidden file has a.esp disabled.
  // The second addProfile(0) (called in profile 1) passes the condition and copies profile 0's
  // hidden file (a.esp disabled) as profile 2. So profile 2 also has a.esp disabled.
  std::vector<std::weak_ptr<DeployerEntry>> expectedEntries2 = {
    root,
    expectedAespDisabled,
    expectedCesp,
    expectedDesp,
    expectedMorrowind
  };

  resetFiles();
  LootDeployer depl(
    DATA_DIR / "target" / "loot" / "source", DATA_DIR / "target" / "loot" / "target", "", false);
  depl.addProfile(5);
  depl.addProfile(0);
  depl.setModStatus(0, false);
  REQUIRE_THAT(depl.getLoadorder()->getTraversalItems(),
               EqualsDeployerEntryVector(expectedEntries0));
  depl.setProfile(1);
  REQUIRE_THAT(depl.getLoadorder()->getTraversalItems(),
               EqualsDeployerEntryVector(expectedEntries1));
  depl.addProfile(0);
  depl.setProfile(2);
  REQUIRE_THAT(depl.getLoadorder()->getTraversalItems(),
              EqualsDeployerEntryVector(expectedEntries2));
  verifyDirsAreEqual(
    DATA_DIR / "target" / "loot" / "target", DATA_DIR / "target" / "loot" / "profiles", true);
}
