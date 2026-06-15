/*!
 * \file thunderstoreprovider.cpp
 * \brief Implementation of the Thunderstore provider for the remote mod-source
 *        abstraction (limo-app/limo#60).
 *
 * All requests target the public Thunderstore Experimental REST API:
 *   https://thunderstore.io/api/docs/
 *
 * No API key is required for any operation performed here.
 */

#include "thunderstoreprovider.h"
#include "../parseerror.h"
#include <cpr/cpr.h>
#include <format>
#include <json/json.h>
#include <stdexcept>

using namespace remote;


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string ThunderstoreProvider::name() const
{
  return "Thunderstore";
}

std::pair<std::string, std::string> ThunderstoreProvider::splitModId(const std::string& mod_id)
{
  const auto sep = mod_id.find('-');
  if(sep == std::string::npos || sep == 0 || sep == mod_id.size() - 1)
    throw std::runtime_error(
      std::format("Invalid Thunderstore mod id \"{}\": expected \"<namespace>-<name>\".", mod_id));
  return { mod_id.substr(0, sep), mod_id.substr(sep + 1) };
}

RemoteMod ThunderstoreProvider::packageToMod(const Json::Value& pkg)
{
  RemoteMod mod;
  // "full_name" is "<namespace>-<name>-<version>" on list endpoints;
  // on detail endpoints it is the same.  We store the stable "namespace-name"
  // form so it can be round-tripped back into getModInfo / getFiles.
  const std::string ns   = pkg["namespace"].asString();
  const std::string pname = pkg["name"].asString();
  mod.id      = ns + "-" + pname;
  mod.name    = pkg.isMember("full_name") ? pname : pname; // display name
  mod.author  = ns;
  mod.page_url = pkg["package_url"].asString();

  // The "versions" array is present on both list and detail responses.
  // The first entry (index 0) is the most-recent version.
  if(pkg.isMember("versions") && pkg["versions"].isArray() && pkg["versions"].size() > 0)
  {
    const Json::Value& latest = pkg["versions"][0];
    mod.version     = latest["version_number"].asString();
    mod.summary     = latest["description"].asString();
    mod.icon_url    = latest["icon"].asString();
    mod.total_downloads = pkg.isMember("total_downloads")
                            ? pkg["total_downloads"].asInt64()
                            : 0;
  }

  // Preserve raw JSON for callers that need additional fields.
  Json::FastWriter writer;
  mod.extra_json = writer.write(pkg);

  return mod;
}

RemoteFile ThunderstoreProvider::versionToFile(const Json::Value& ver)
{
  RemoteFile file;
  file.id           = ver["uuid4"].asString();
  file.name         = ver["full_name"].asString(); // "ns-pkg-1.2.3"
  file.version      = ver["version_number"].asString();
  file.download_url = ver["download_url"].asString();
  // Thunderstore reports file_size in bytes under "file_size" (integer)
  file.size_bytes   = ver.isMember("file_size") ? ver["file_size"].asInt64() : 0;
  file.description  = ver["description"].asString();
  // "date_created" is an ISO-8601 string
  file.uploaded_at  = ver.isMember("date_created") ? ver["date_created"].asString() : "";
  return file;
}

// ---------------------------------------------------------------------------
// RemoteSource interface
// ---------------------------------------------------------------------------

std::vector<RemoteMod> ThunderstoreProvider::search(const std::string& community,
                                                    const std::string& query,
                                                    int max_results)
{
  // GET /api/experimental/community/{community}/package/?q={query}&page_size={n}
  const std::string url = std::format("{}/api/experimental/community/{}/package/", BASE_URL, community);

  cpr::Response response = cpr::Get(
    cpr::Url(url),
    cpr::Parameters{ { "q", query }, { "page_size", std::to_string(max_results) } },
    cpr::Header{ { "User-Agent", USER_AGENT }, { "Accept", "application/json" } });

  if(response.status_code != 200)
    throw std::runtime_error(
      std::format("Thunderstore search failed for community \"{}\" query \"{}\". "
                  "HTTP {}.",
                  community,
                  query,
                  response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
    throw ParseError("Failed to parse Thunderstore search response.");

  // Response shape: { "count": N, "next": null|url, "previous": null|url, "results": [...] }
  const Json::Value& results = json_body["results"];
  std::vector<RemoteMod> mods;
  mods.reserve(std::min(static_cast<int>(results.size()), max_results));
  for(int i = 0; i < static_cast<int>(results.size()) && i < max_results; ++i)
    mods.push_back(packageToMod(results[i]));
  return mods;
}

RemoteMod ThunderstoreProvider::getModInfo(const std::string& community,
                                           const std::string& mod_id)
{
  const auto [ns, pname] = splitModId(mod_id);
  // GET /api/experimental/community/{community}/package/{namespace}/{name}/
  const std::string url =
    std::format("{}/api/experimental/community/{}/package/{}/{}/", BASE_URL, community, ns, pname);

  cpr::Response response = cpr::Get(
    cpr::Url(url),
    cpr::Header{ { "User-Agent", USER_AGENT }, { "Accept", "application/json" } });

  if(response.status_code == 404)
    throw std::runtime_error(
      std::format("Thunderstore package \"{}/{}\" not found in community \"{}\".",
                  ns,
                  pname,
                  community));
  if(response.status_code != 200)
    throw std::runtime_error(
      std::format("Thunderstore getModInfo failed. HTTP {}.", response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
    throw ParseError("Failed to parse Thunderstore package detail response.");

  return packageToMod(json_body);
}

std::vector<RemoteFile> ThunderstoreProvider::getFiles(const std::string& community,
                                                       const std::string& mod_id)
{
  // Reuse getModInfo; all version entries are embedded in the same response.
  const auto [ns, pname] = splitModId(mod_id);
  const std::string url =
    std::format("{}/api/experimental/community/{}/package/{}/{}/", BASE_URL, community, ns, pname);

  cpr::Response response = cpr::Get(
    cpr::Url(url),
    cpr::Header{ { "User-Agent", USER_AGENT }, { "Accept", "application/json" } });

  if(response.status_code != 200)
    throw std::runtime_error(
      std::format("Thunderstore getFiles failed for \"{}-{}\" in \"{}\". HTTP {}.",
                  ns,
                  pname,
                  community,
                  response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
    throw ParseError("Failed to parse Thunderstore package detail response.");

  const Json::Value& versions = json_body["versions"];
  std::vector<RemoteFile> files;
  files.reserve(versions.size());
  for(int i = 0; i < static_cast<int>(versions.size()); ++i)
    files.push_back(versionToFile(versions[i]));
  return files;
}

std::string ThunderstoreProvider::getDownloadUrl(const std::string& community,
                                                  const std::string& mod_id,
                                                  const std::string& file_id)
{
  // The download_url is already part of the version object; walk getFiles to
  // find the matching UUID rather than making an extra HTTP round-trip.
  const std::vector<RemoteFile> files = getFiles(community, mod_id);
  for(const auto& f : files)
  {
    if(f.id == file_id)
      return f.download_url;
  }
  throw std::runtime_error(
    std::format("Thunderstore: version UUID \"{}\" not found for package \"{}\" in community \"{}\".",
                file_id,
                mod_id,
                community));
}
