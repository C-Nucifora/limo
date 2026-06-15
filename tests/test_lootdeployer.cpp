#include "../src/core/lootdeployer.h"
#include "test_utils.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>


void resetFiles()
{
  const sfs::path target_dir = DATA_DIR / "target" / "loot" / "target";
  const sfs::path plugin_source = DATA_DIR / "source" / "loot" / "plugins.txt";
  const sfs::path load_order_source = DATA_DIR / "source" / "loot" / "loadorder.txt";
  const sfs::path initial_source = DATA_DIR / "source" / "loot" / "initial_target";
  // Remove all files in the target directory.
  for(const auto& dir_entry : sfs::directory_iterator(target_dir))
    sfs::remove(dir_entry.path());
  // Restore base plugin/loadorder files.
  sfs::copy(plugin_source, target_dir / "plugins.txt");
  sfs::copy(load_order_source, target_dir / "loadorder.txt");
  // Restore initial profile state (hidden files: .lmmconfig, .lmmprof* etc.).
  for(const auto& dir_entry : sfs::directory_iterator(initial_source))
    sfs::copy(dir_entry.path(), target_dir / dir_entry.path().filename());
}


TEST_CASE("State is read", "[loot]")
{
  resetFiles();
  LootDeployer depl(
    DATA_DIR / "target" / "loot" / "source", DATA_DIR / "target" / "loot" / "target", "", false);
  REQUIRE(depl.getNumMods() == 4);
  REQUIRE_THAT(depl.getModNames(),
               Catch::Matchers::Equals(std::vector<std::string>{ "a.esp", "c.esp", "d.esp", "Morrowind.esm" }));
  REQUIRE_THAT(depl.getLoadorder(),
               Catch::Matchers::Equals(
                 std::vector<std::tuple<int, bool>>{ { -1, true }, { -1, false }, { -1, true }, { -1, true } }));
}

TEST_CASE("Load order can be edited", "[loot]")
{
  resetFiles();
  LootDeployer depl(
    DATA_DIR / "target" / "loot" / "source", DATA_DIR / "target" / "loot" / "target", "", false);
  depl.changeLoadorder(0, 2);
  depl.setModStatus(1, true);
  depl.setModStatus(0, false);
  depl.changeLoadorder(2, 1);
  REQUIRE_THAT(depl.getModNames(),
               Catch::Matchers::Equals(std::vector<std::string>{ "c.esp", "a.esp", "d.esp", "Morrowind.esm" }));
  REQUIRE_THAT(depl.getLoadorder(),
               Catch::Matchers::Equals(
                 std::vector<std::tuple<int, bool>>{ { -1, false }, { -1, true }, { -1, true }, { -1, true } }));
  LootDeployer depl2(
    DATA_DIR / "target" / "loot" / "source", DATA_DIR / "target" / "loot" / "target", "", false);
  REQUIRE_THAT(depl.getModNames(), Catch::Matchers::Equals(depl2.getModNames()));
  REQUIRE_THAT(depl.getLoadorder(), Catch::Matchers::Equals(depl2.getLoadorder()));
}

TEST_CASE("Profiles are managed", "[loot]")
{
  resetFiles();
  LootDeployer depl(
    DATA_DIR / "target" / "loot" / "source", DATA_DIR / "target" / "loot" / "target", "", false);
  depl.addProfile(5);
  depl.addProfile(0);
  depl.setModStatus(0, false);
  REQUIRE_THAT(depl.getLoadorder(),
               Catch::Matchers::Equals(
                 std::vector<std::tuple<int, bool>>{ { -1, false }, { -1, false }, { -1, true }, { -1, true } }));
  depl.setProfile(1);
  REQUIRE_THAT(depl.getLoadorder(),
               Catch::Matchers::Equals(
                 std::vector<std::tuple<int, bool>>{ { -1, true }, { -1, false }, { -1, true }, { -1, true } }));
  depl.addProfile(0);
  depl.setProfile(2);
  REQUIRE_THAT(depl.getLoadorder(),
               Catch::Matchers::Equals(
                 std::vector<std::tuple<int, bool>>{ { -1, false }, { -1, false }, { -1, true }, { -1, true } }));
  verifyDirsAreEqual(
    DATA_DIR / "target" / "loot" / "target", DATA_DIR / "target" / "loot" / "profiles", true);
}
