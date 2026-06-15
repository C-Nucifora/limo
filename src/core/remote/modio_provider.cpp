/*!
 * \file modio_provider.cpp
 * \brief Scaffold implementation of the mod.io provider
 *        (limo-app/limo#60, limo-app/limo#209).
 *
 * Status: SCAFFOLDED — compilable, full HTTP/parse structure in place.
 *   search()         — implemented (/v1/games/{id}/mods?_q={query})
 *   getModInfo()     — implemented (/v1/games/{id}/mods/{mod_id})
 *   getFiles()       — implemented (/v1/games/{id}/mods/{mod_id}/files)
 *   getDownloadUrl() — returns public binary_url; OAuth flow is TODO.
 *
 * mod.io API docs: https://docs.mod.io/restapiref
 */

#include "modio_provider.h"
#include "../parseerror.h"
#include <cpr/cpr.h>
#include <format>
#include <json/json.h>
#include <stdexcept>

using namespace remote;


// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ModioProvider::ModioProvider(const std::string& api_key)
  : api_key_(api_key)
{}

std::string ModioProvider::name() const
{
  return "mod.io";
}

void ModioProvider::setApiKey(const std::string& key)
{
  api_key_ = key;
}

void ModioProvider::requireKey() const
{
  if(api_key_.empty())
    throw std::runtime_error(
      "mod.io provider: no API key configured. "
      "Obtain a key at https://mod.io/apikey/widget and call setApiKey().");
}

// ---------------------------------------------------------------------------
// Conversion helpers
// ---------------------------------------------------------------------------

RemoteMod ModioProvider::modObjectToRemoteMod(const Json::Value& obj)
{
  RemoteMod mod;
  mod.id      = std::to_string(obj["id"].asInt64());
  mod.name    = obj["name"].asString();
  mod.summary = obj["summary"].asString();
  if(obj.isMember("submitted_by") && obj["submitted_by"].isMember("username"))
    mod.author = obj["submitted_by"]["username"].asString();

  // Logo URL — use "original" if available, else first size
  if(obj.isMember("logo"))
  {
    const Json::Value& logo = obj["logo"];
    mod.icon_url = logo.isMember("original") ? logo["original"].asString()
                   : logo.isMember("thumb_320x180") ? logo["thumb_320x180"].asString()
                   : "";
  }

  // mod.io exposes stats.downloads_total
  if(obj.isMember("stats") && obj["stats"].isMember("downloads_total"))
    mod.total_downloads = obj["stats"]["downloads_total"].asInt64();

  // Latest release version from modfile subobject (if present)
  if(obj.isMember("modfile") && !obj["modfile"].isNull())
    mod.version = obj["modfile"]["version"].asString();

  mod.page_url = obj.isMember("profile_url") ? obj["profile_url"].asString() : "";

  Json::FastWriter writer;
  mod.extra_json = writer.write(obj);
  return mod;
}

RemoteFile ModioProvider::modfileToRemoteFile(const Json::Value& obj)
{
  RemoteFile file;
  file.id      = std::to_string(obj["id"].asInt64());
  file.version = obj.isMember("version") ? obj["version"].asString() : "";
  // "filename" is the original archive name
  file.name    = obj.isMember("filename") ? obj["filename"].asString() : file.id;
  // mod.io exposes file size in bytes under "filesize"
  file.size_bytes  = obj.isMember("filesize") ? obj["filesize"].asInt64() : 0;
  file.description = obj.isMember("changelog") ? obj["changelog"].asString() : "";
  // date_added is a Unix timestamp
  file.uploaded_at = obj.isMember("date_added")
                       ? std::to_string(obj["date_added"].asInt64())
                       : "";
  // binary_url is the direct CDN link; present for public mods without auth
  if(obj.isMember("download") && !obj["download"].isNull())
    file.download_url = obj["download"]["binary_url"].asString();
  return file;
}

// ---------------------------------------------------------------------------
// RemoteSource interface
// ---------------------------------------------------------------------------

std::vector<RemoteMod> ModioProvider::search(const std::string& community,
                                              const std::string& query,
                                              int max_results)
{
  requireKey();
  // GET /v1/games/{game_id}/mods?api_key={key}&_q={query}&_limit={n}
  const std::string url = std::format("{}/games/{}/mods", BASE_URL, community);
  cpr::Response response = cpr::Get(
    cpr::Url(url),
    cpr::Parameters{ { "api_key", api_key_ },
                     { "_q", query },
                     { "_limit", std::to_string(max_results) } },
    cpr::Header{ { "User-Agent", USER_AGENT }, { "Accept", "application/json" } });

  if(response.status_code != 200)
    throw std::runtime_error(
      std::format("mod.io search failed for game \"{}\", query \"{}\". HTTP {}.",
                  community,
                  query,
                  response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
    throw ParseError("Failed to parse mod.io search response.");

  // Response: { "data": [...], "result_count": N, ... }
  const Json::Value& data = json_body["data"];
  std::vector<RemoteMod> mods;
  mods.reserve(data.size());
  for(int i = 0; i < static_cast<int>(data.size()); ++i)
    mods.push_back(modObjectToRemoteMod(data[i]));
  return mods;
}

RemoteMod ModioProvider::getModInfo(const std::string& community,
                                    const std::string& mod_id)
{
  requireKey();
  const std::string url = std::format("{}/games/{}/mods/{}", BASE_URL, community, mod_id);
  cpr::Response response = cpr::Get(
    cpr::Url(url),
    cpr::Parameters{ { "api_key", api_key_ } },
    cpr::Header{ { "User-Agent", USER_AGENT }, { "Accept", "application/json" } });

  if(response.status_code == 404)
    throw std::runtime_error(
      std::format("mod.io: mod \"{}\" not found in game \"{}\".", mod_id, community));
  if(response.status_code != 200)
    throw std::runtime_error(
      std::format("mod.io getModInfo failed. HTTP {}.", response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
    throw ParseError("Failed to parse mod.io mod detail response.");

  return modObjectToRemoteMod(json_body);
}

std::vector<RemoteFile> ModioProvider::getFiles(const std::string& community,
                                                 const std::string& mod_id)
{
  requireKey();
  const std::string url =
    std::format("{}/games/{}/mods/{}/files", BASE_URL, community, mod_id);
  cpr::Response response = cpr::Get(
    cpr::Url(url),
    cpr::Parameters{ { "api_key", api_key_ } },
    cpr::Header{ { "User-Agent", USER_AGENT }, { "Accept", "application/json" } });

  if(response.status_code != 200)
    throw std::runtime_error(
      std::format("mod.io getFiles failed for mod \"{}\" in game \"{}\". HTTP {}.",
                  mod_id,
                  community,
                  response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
    throw ParseError("Failed to parse mod.io files response.");

  const Json::Value& data = json_body["data"];
  std::vector<RemoteFile> files;
  files.reserve(data.size());
  for(int i = 0; i < static_cast<int>(data.size()); ++i)
    files.push_back(modfileToRemoteFile(data[i]));
  return files;
}

std::string ModioProvider::getDownloadUrl(const std::string& community,
                                           const std::string& mod_id,
                                           const std::string& file_id)
{
  // For public mods the binary_url is embedded in the modfile object; use
  // getFiles to find the matching entry rather than an extra request.
  // TODO(limo-app/limo#60): for subscriber-only mods, implement the OAuth
  //   token flow: POST /v1/oauth/emailrequest then POST /v1/oauth/emailexchange
  //   to obtain a bearer token, then GET with Authorization: Bearer {token}.
  const std::vector<RemoteFile> files = getFiles(community, mod_id);
  for(const auto& f : files)
  {
    if(f.id == file_id)
    {
      if(f.download_url.empty())
        throw std::runtime_error(
          std::format("mod.io: no public download URL for file \"{}\" of mod \"{}\". "
                      "The mod may require authentication (OAuth token flow — "
                      "see limo-app/limo#60).",
                      file_id,
                      mod_id));
      return f.download_url;
    }
  }
  throw std::runtime_error(
    std::format("mod.io: file \"{}\" not found for mod \"{}\".", file_id, mod_id));
}
