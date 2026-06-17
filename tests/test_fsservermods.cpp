#include "../src/core/remote/fsservermods.h"
#include <catch2/catch_test_macros.hpp>

using remote::FsServerMods;


TEST_CASE("FS server URL is normalized to the /mods index", "[fsserver]")
{
  REQUIRE(FsServerMods::normalizeUrl("myhost:8080") == "http://myhost:8080/mods");
  REQUIRE(FsServerMods::normalizeUrl("http://myhost:8080/") == "http://myhost:8080/mods");
  REQUIRE(FsServerMods::normalizeUrl("http://myhost:8080/mods") == "http://myhost:8080/mods");
  REQUIRE(FsServerMods::normalizeUrl("  http://h/mods  ") == "http://h/mods");
}

TEST_CASE("Relative mod links resolve against the index URL", "[fsserver]")
{
  const std::string html =
    "<html><body><h1>Index of /mods</h1>"
    "<a href=\"../\">Parent</a>"
    "<a href=\"FS25_FollowMe.zip\">FS25_FollowMe.zip</a>"
    "<a href=\"FS25_Big%20Mod.zip\">FS25_Big Mod.zip</a>"
    "</body></html>";
  const auto mods = FsServerMods::parseModIndex(html, "http://server:8080/mods");
  REQUIRE(mods.size() == 2);
  REQUIRE(mods[0].file_name == "FS25_FollowMe.zip");
  REQUIRE(mods[0].download_url == "http://server:8080/FS25_FollowMe.zip");
  // %20 is decoded in the displayed file name.
  REQUIRE(mods[1].file_name == "FS25_Big Mod.zip");
  REQUIRE(mods[1].download_url == "http://server:8080/FS25_Big%20Mod.zip");
}

TEST_CASE("Absolute and root-relative links are honored", "[fsserver]")
{
  const std::string html =
    "<a href=\"/mods/A.zip\">A</a>"
    "<a href=\"http://cdn.example/B.zip\">B</a>";
  const auto mods = FsServerMods::parseModIndex(html, "http://server:8080/mods");
  REQUIRE(mods.size() == 2);
  REQUIRE(mods[0].download_url == "http://server:8080/mods/A.zip");
  REQUIRE(mods[1].download_url == "http://cdn.example/B.zip");
}

TEST_CASE("Non-zip links and duplicates are ignored", "[fsserver]")
{
  const std::string html =
    "<a href=\"readme.txt\">readme</a>"
    "<a href=\"Mod.zip\">Mod</a>"
    "<a href=\"Mod.zip\">Mod again</a>";
  const auto mods = FsServerMods::parseModIndex(html, "http://h/mods");
  REQUIRE(mods.size() == 1);
  REQUIRE(mods[0].file_name == "Mod.zip");
}
