/*!
 * \file test_remoteproviders.cpp
 * \brief Network-free smoke tests for the experimental remote-source provider parsers.
 *
 * The Thunderstore and mod.io providers (src/core/remote/) keep their JSON->struct
 * parsing in private static helpers. These tests exercise those parsers against
 * literal JSON so the parse paths stay regression-safe even while the providers
 * remain experimental and unwired from the UI (limo-app/limo#60, finding F087).
 *
 * GameBanana is intentionally not covered: its provider parses responses inline
 * inside its network methods (no separable pure parser exists to call without
 * issuing an HTTP request), so a smoke test would require a non-trivial refactor.
 */

#include "../src/core/remote/modio_provider.h"
#include "../src/core/remote/thunderstoreprovider.h"

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>
#include <sstream>
#include <string>
#include <utility>

namespace remote
{
//! Test-only shim that forwards to the providers' private static parsers (declared
//! a friend in each provider header).
struct RemoteProviderTestAccess
{
  static RemoteMod tsPackageToMod(const Json::Value& v)
  {
    return ThunderstoreProvider::packageToMod(v);
  }
  static RemoteFile tsVersionToFile(const Json::Value& v)
  {
    return ThunderstoreProvider::versionToFile(v);
  }
  static std::pair<std::string, std::string> tsSplitModId(const std::string& s)
  {
    return ThunderstoreProvider::splitModId(s);
  }
  static RemoteMod modioModToMod(const Json::Value& v)
  {
    return ModioProvider::modObjectToRemoteMod(v);
  }
  static RemoteFile modioFileToFile(const Json::Value& v)
  {
    return ModioProvider::modfileToRemoteFile(v);
  }
};
} // namespace remote

namespace
{
Json::Value parseJson(const std::string& text)
{
  Json::Value value;
  Json::CharReaderBuilder builder;
  std::string errors;
  std::istringstream stream(text);
  REQUIRE(Json::parseFromStream(builder, stream, &value, &errors));
  return value;
}
} // namespace

using Access = remote::RemoteProviderTestAccess;

TEST_CASE("Thunderstore packageToMod maps a package object", "[remoteproviders]")
{
  const Json::Value pkg = parseJson(R"({
    "namespace": "ns",
    "name": "name",
    "package_url": "https://thunderstore.io/c/ror2/p/ns/name/",
    "total_downloads": 12345,
    "versions": [
      { "version_number": "1.2.3", "description": "a summary", "icon": "https://img/icon.png" }
    ]
  })");
  const remote::RemoteMod mod = Access::tsPackageToMod(pkg);
  REQUIRE(mod.id == "ns-name");
  REQUIRE(mod.name == "name");
  REQUIRE(mod.author == "ns");
  REQUIRE(mod.version == "1.2.3");
  REQUIRE(mod.summary == "a summary");
  REQUIRE(mod.icon_url == "https://img/icon.png");
  REQUIRE(mod.total_downloads == 12345);
  REQUIRE(mod.page_url == "https://thunderstore.io/c/ror2/p/ns/name/");
}

TEST_CASE("Thunderstore versionToFile maps a version object", "[remoteproviders]")
{
  const Json::Value ver = parseJson(R"({
    "uuid4": "abcd-uuid",
    "full_name": "ns-name-1.2.3",
    "version_number": "1.2.3",
    "download_url": "https://cdn/ns-name-1.2.3.zip",
    "file_size": 4096,
    "description": "changelog",
    "date_created": "2024-01-01T00:00:00Z"
  })");
  const remote::RemoteFile file = Access::tsVersionToFile(ver);
  REQUIRE(file.id == "abcd-uuid");
  REQUIRE(file.name == "ns-name-1.2.3");
  REQUIRE(file.version == "1.2.3");
  REQUIRE(file.download_url == "https://cdn/ns-name-1.2.3.zip");
  REQUIRE(file.size_bytes == 4096);
  REQUIRE(file.uploaded_at == "2024-01-01T00:00:00Z");
}

TEST_CASE("Thunderstore splitModId splits on the first hyphen", "[remoteproviders]")
{
  auto [ns, name] = Access::tsSplitModId("BepInEx-BepInExPack");
  REQUIRE(ns == "BepInEx");
  REQUIRE(name == "BepInExPack");

  // The package name itself may contain hyphens; only the first one delimits.
  auto [ns2, name2] = Access::tsSplitModId("ns-some-long-name");
  REQUIRE(ns2 == "ns");
  REQUIRE(name2 == "some-long-name");

  REQUIRE_THROWS(Access::tsSplitModId("noseparator"));
  REQUIRE_THROWS(Access::tsSplitModId("-leading"));
  REQUIRE_THROWS(Access::tsSplitModId("trailing-"));
}

TEST_CASE("mod.io modObjectToRemoteMod maps a mod object", "[remoteproviders]")
{
  const Json::Value obj = parseJson(R"({
    "id": 987,
    "name": "My Mod",
    "summary": "summary text",
    "submitted_by": { "username": "alice" },
    "logo": { "original": "https://img/original.png", "thumb_320x180": "https://img/thumb.png" },
    "stats": { "downloads_total": 5000 },
    "modfile": { "version": "2.0" },
    "profile_url": "https://mod.io/g/x/m/my-mod"
  })");
  const remote::RemoteMod mod = Access::modioModToMod(obj);
  REQUIRE(mod.id == "987");
  REQUIRE(mod.name == "My Mod");
  REQUIRE(mod.summary == "summary text");
  REQUIRE(mod.author == "alice");
  REQUIRE(mod.icon_url == "https://img/original.png");
  REQUIRE(mod.total_downloads == 5000);
  REQUIRE(mod.version == "2.0");
  REQUIRE(mod.page_url == "https://mod.io/g/x/m/my-mod");
}

TEST_CASE("mod.io modfileToRemoteFile handles public, auth-gated and malformed files",
          "[remoteproviders]")
{
  SECTION("public file exposes a direct binary_url")
  {
    const Json::Value obj = parseJson(R"({
      "id": 111,
      "version": "1.0",
      "filename": "mod.zip",
      "filesize": 2048,
      "changelog": "notes",
      "date_added": 1700000000,
      "download": { "binary_url": "https://cdn/mod.zip" }
    })");
    const remote::RemoteFile file = Access::modioFileToFile(obj);
    REQUIRE(file.id == "111");
    REQUIRE(file.version == "1.0");
    REQUIRE(file.name == "mod.zip");
    REQUIRE(file.size_bytes == 2048);
    REQUIRE(file.download_url == "https://cdn/mod.zip");
    REQUIRE(file.uploaded_at == "1700000000");
  }

  SECTION("auth-gated file has no binary_url, so download_url is empty")
  {
    // Mirrors the F086 public-vs-subscriber branch: subscriber-only files omit
    // the "download" object until an authenticated request is made.
    const Json::Value obj = parseJson(R"({
      "id": 112, "version": "1.0", "filename": "mod.zip", "filesize": 2048
    })");
    const remote::RemoteFile file = Access::modioFileToFile(obj);
    REQUIRE(file.download_url.empty());
  }

  SECTION("malformed field types fall back to defaults instead of throwing")
  {
    // Regression guard for F162: the isString()/isIntegral() checks must absorb
    // unexpected JSON types rather than letting jsoncpp throw a LogicError.
    const Json::Value obj = parseJson(R"({
      "id": "not-an-int", "filesize": "huge", "filename": 12345
    })");
    remote::RemoteFile file;
    REQUIRE_NOTHROW(file = Access::modioFileToFile(obj));
    REQUIRE(file.id.empty());      // id not integral -> ""
    REQUIRE(file.size_bytes == 0); // filesize not integral -> 0
    REQUIRE(file.name == file.id); // filename not a string -> falls back to file.id
  }
}
