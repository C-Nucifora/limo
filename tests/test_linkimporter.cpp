#include "../src/core/remote/linkimporter.h"
#include <catch2/catch_test_macros.hpp>

using remote::LinkImporter;
using remote::ResolvedLink;


TEST_CASE("LinkImporter recognizes supported URLs", "[linkimporter]")
{
  REQUIRE(LinkImporter::isGithubUrl("https://github.com/owner/repo"));
  REQUIRE(LinkImporter::isGithubUrl("http://github.com/owner/repo/releases/latest"));
  REQUIRE_FALSE(LinkImporter::isGithubUrl("https://gitlab.com/owner/repo"));

  REQUIRE(LinkImporter::isModHubUrl(
    "https://www.farming-simulator.com/mod.php?mod_id=312054&title=fs2025"));
  REQUIRE(LinkImporter::isModHubUrl(
    "https://farming-simulator.com/mod.php?lang=en&country=us&mod_id=312054"));
  // A ModHub listing page (mods.php, no mod_id) is not a single-mod URL.
  REQUIRE_FALSE(LinkImporter::isModHubUrl("https://www.farming-simulator.com/mods.php?title=fs2025"));
  REQUIRE_FALSE(LinkImporter::isModHubUrl("https://example.com/mod.php?mod_id=1"));

  REQUIRE(LinkImporter::isSupportedUrl("https://github.com/a/b"));
  REQUIRE_FALSE(LinkImporter::isSupportedUrl("https://nexusmods.com/skyrim/mods/1"));
}

TEST_CASE("ModHub mod-page HTML is scraped to a direct CDN download", "[linkimporter]")
{
  const std::string page_url =
    "https://www.farming-simulator.com/mod.php?mod_id=312054&title=fs2025";
  const std::string html =
    "<html><head><title>FollowMe - Farming Simulator 25 Mod | FS25 Mod</title></head>"
    "<body><img src=\"https://img.giants-software.com/preview.png\">"
    "<a class=\"button\" href=\"https://cdn25.giants-software.com/modHub/storage/00312054/"
    "FS25_FollowMe.zip\">Download</a></body></html>";

  const ResolvedLink result = LinkImporter::parseModHubPage(html, page_url);
  REQUIRE(result.ok);
  REQUIRE(result.download_url ==
          "https://cdn25.giants-software.com/modHub/storage/00312054/FS25_FollowMe.zip");
  REQUIRE(result.file_name == "FS25_FollowMe.zip");
  // The download must carry a browser User-Agent and the mod page as Referer (hotlink gate).
  REQUIRE_FALSE(result.user_agent.empty());
  REQUIRE(result.referer == page_url);
  // The site suffix is trimmed off the <title>.
  REQUIRE(result.mod_name == "FollowMe");
}

TEST_CASE("ModHub page without a CDN link fails gracefully", "[linkimporter]")
{
  const std::string html =
    "<html><body><a href=\"https://img.giants-software.com/preview.png\">img</a></body></html>";
  const ResolvedLink result = LinkImporter::parseModHubPage(html, "https://x/mod.php?mod_id=1");
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("GitHub release JSON resolves to the first archive asset", "[linkimporter]")
{
  const std::string json = R"({
    "tag_name": "v1.4.0",
    "name": "Release 1.4.0",
    "assets": [
      { "name": "notes.txt", "browser_download_url": "https://example/notes.txt" },
      { "name": "CoolMod-1.4.0.zip",
        "browser_download_url": "https://github.com/o/r/releases/download/v1.4.0/CoolMod-1.4.0.zip" }
    ],
    "zipball_url": "https://api.github.com/repos/o/r/zipball/v1.4.0"
  })";
  const ResolvedLink result = LinkImporter::parseGithubReleaseJson(json, "CoolMod");
  REQUIRE(result.ok);
  REQUIRE(result.file_name == "CoolMod-1.4.0.zip");
  REQUIRE(result.download_url ==
          "https://github.com/o/r/releases/download/v1.4.0/CoolMod-1.4.0.zip");
  REQUIRE(result.version == "v1.4.0");
  REQUIRE(result.mod_name == "CoolMod");
}

TEST_CASE("GitHub release with no archive assets falls back to the source zipball",
          "[linkimporter]")
{
  const std::string json = R"({
    "tag_name": "v2.0",
    "assets": [ { "name": "changelog.md", "browser_download_url": "https://example/cl.md" } ],
    "zipball_url": "https://api.github.com/repos/o/r/zipball/v2.0"
  })";
  const ResolvedLink result = LinkImporter::parseGithubReleaseJson(json, "MyRepo");
  REQUIRE(result.ok);
  REQUIRE(result.download_url == "https://api.github.com/repos/o/r/zipball/v2.0");
  REQUIRE(result.file_name == "MyRepo-v2.0.zip");
  REQUIRE(result.version == "v2.0");
}

TEST_CASE("GitHub release with no assets at all fails gracefully", "[linkimporter]")
{
  const std::string json = R"({ "tag_name": "v1", "assets": [] })";
  const ResolvedLink result = LinkImporter::parseGithubReleaseJson(json, "Repo");
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("Disabled ModHub import resolves to an instructive error", "[linkimporter]")
{
  const ResolvedLink result = LinkImporter::resolve(
    "https://www.farming-simulator.com/mod.php?mod_id=1", /*allow_modhub=*/false);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("Unrecognized URLs resolve to an error", "[linkimporter]")
{
  const ResolvedLink result = LinkImporter::resolve("https://example.com/file.zip", true);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
}
