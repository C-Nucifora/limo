/*!
 * \file gamebanana_provider.cpp
 * \brief Scaffold implementation of the GameBanana provider
 *        (limo-app/limo#60, limo-app/limo#4).
 *
 * Status: SCAFFOLDED — compilable, HTTP/parse structure is in place.
 *   search()      — implemented using /Core/List/New
 *   getModInfo()  — implemented using /Core/Item/Data
 *   getFiles()    — implemented; parses Files().aFiles() field
 *   getDownloadUrl() — implemented; returns URL from file metadata
 *
 * GameBanana API docs: https://api.gamebanana.com/
 */

#include "gamebanana_provider.h"
#include "../parseerror.h"
#include <cpr/cpr.h>
#include <format>
#include <json/json.h>
#include <stdexcept>

using namespace remote;


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string GamebananaProvider::name() const
{
  return "GameBanana";
}

Json::Value GamebananaProvider::fetchItemData(const std::string& mod_id,
                                              const std::string& fields)
{
  // GET https://api.gamebanana.com/Core/Item/Data?
  //       itemtype=Mod&itemid={mod_id}&fields={fields}
  const std::string url = std::string(BASE_URL) + "/Core/Item/Data";
  cpr::Response response = cpr::Get(
    cpr::Url(url),
    cpr::Parameters{ { "itemtype", ITEM_TYPE },
                     { "itemid", mod_id },
                     { "fields", fields } },
    cpr::Header{ { "User-Agent", USER_AGENT }, { "Accept", "application/json" } });

  if(response.status_code != 200)
    throw std::runtime_error(
      std::format("GameBanana fetchItemData failed for mod id \"{}\". HTTP {}.",
                  mod_id,
                  response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
    throw ParseError("Failed to parse GameBanana item data response.");
  // The response is a JSON array with one element per requested field.
  return json_body;
}

// ---------------------------------------------------------------------------
// RemoteSource interface
// ---------------------------------------------------------------------------

std::vector<RemoteMod> GamebananaProvider::search(const std::string& community,
                                                  const std::string& query,
                                                  int max_results)
{
  // GET https://api.gamebanana.com/Core/List/New?
  //       itemtype=Mod&gameid={community}&page=1&perpage={n}&search={query}
  // TODO(limo-app/limo#60): switch to /apiv2/Util/Game/Submissions/List once
  //   that endpoint stabilises; /Core/List/New does not officially support
  //   a search keyword but the undocumented `search` param is widely used.
  const std::string url = std::string(BASE_URL) + "/Core/List/New";
  cpr::Response response = cpr::Get(
    cpr::Url(url),
    cpr::Parameters{ { "itemtype", ITEM_TYPE },
                     { "gameid", community },
                     { "page", "1" },
                     { "perpage", std::to_string(max_results) },
                     { "search", query } },
    cpr::Header{ { "User-Agent", USER_AGENT }, { "Accept", "application/json" } });

  if(response.status_code != 200)
    throw std::runtime_error(
      std::format("GameBanana search failed for game \"{}\", query \"{}\". HTTP {}.",
                  community,
                  query,
                  response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
    throw ParseError("Failed to parse GameBanana search response.");

  // Response: array of { _idRow, _sName, _sProfileUrl, _aPreviewMedia, _aSubmitter, ... }
  std::vector<RemoteMod> mods;
  for(int i = 0; i < static_cast<int>(json_body.size()) && i < max_results; ++i)
  {
    const Json::Value& item = json_body[i];
    RemoteMod mod;
    mod.id       = std::to_string(item["_idRow"].asInt64());
    mod.name     = item["_sName"].asString();
    mod.page_url = item["_sProfileUrl"].asString();
    if(item.isMember("_aSubmitter") && item["_aSubmitter"].isMember("_sName"))
      mod.author = item["_aSubmitter"]["_sName"].asString();
    // Summary / version are not available on the list endpoint without extra fields;
    // a full getModInfo() call is required to populate those.
    Json::FastWriter writer;
    mod.extra_json = writer.write(item);
    mods.push_back(std::move(mod));
  }
  return mods;
}

RemoteMod GamebananaProvider::getModInfo(const std::string& /*community*/,
                                         const std::string& mod_id)
{
  // Fields: name, description, Owner().name, Preview().sSubFeedImageUrl(),
  //         Updates().aSubmissions(), Version
  // TODO(limo-app/limo#60): consider requesting screenshot URLs for icon_url
  const std::string fields =
    "name,description,Owner().name,Preview().sSubFeedImageUrl(),Version()";
  const Json::Value data = fetchItemData(mod_id, fields);

  // data is an array; indices match the comma-separated fields list above.
  if(!data.isArray())
    throw ParseError(
      std::format("GameBanana item data for mod \"{}\" is not a JSON array.", mod_id));

  // Tolerate missing/short fields: index only when present and of string type.
  const auto field = [&data](Json::ArrayIndex i) -> std::string
  {
    if(i >= data.size() || data[i].isNull())
      return "";
    return data[i].asString();
  };

  RemoteMod mod;
  mod.id      = mod_id;
  mod.name    = field(0);
  mod.summary = field(1);
  mod.author  = field(2);
  mod.icon_url = field(3);
  // Version() returns a string like "1.2.3" or may be null
  mod.version  = field(4);
  mod.page_url = std::format("https://gamebanana.com/mods/{}", mod_id);

  Json::FastWriter writer;
  mod.extra_json = writer.write(data);
  return mod;
}

std::vector<RemoteFile> GamebananaProvider::getFiles(const std::string& /*community*/,
                                                     const std::string& mod_id)
{
  // Files().aFiles() returns an object keyed by file ID, each value containing
  // _sFile (filename), _nFilesize, _sDownloadUrl, _sDescription, _tsDateAdded
  const std::string fields = "Files().aFiles()";
  const Json::Value data = fetchItemData(mod_id, fields);

  // data[0] is the Files().aFiles() result (object or null)
  const Json::Value& files_obj = data[0];
  std::vector<RemoteFile> files;
  if(files_obj.isNull() || !files_obj.isObject())
    return files;

  for(const auto& key : files_obj.getMemberNames())
  {
    const Json::Value& f = files_obj[key];
    RemoteFile file;
    file.id           = key; // GameBanana file ID
    file.name         = f.isMember("_sFile") ? f["_sFile"].asString() : key;
    file.size_bytes   = f.isMember("_nFilesize") ? f["_nFilesize"].asInt64() : 0;
    file.download_url = f.isMember("_sDownloadUrl") ? f["_sDownloadUrl"].asString() : "";
    file.description  = f.isMember("_sDescription") ? f["_sDescription"].asString() : "";
    file.uploaded_at  = f.isMember("_tsDateAdded")
                          ? std::to_string(f["_tsDateAdded"].asInt64())
                          : "";
    // GameBanana does not expose a per-file version; inherit from mod version if needed.
    files.push_back(std::move(file));
  }
  return files;
}

std::string GamebananaProvider::getDownloadUrl(const std::string& community,
                                               const std::string& mod_id,
                                               const std::string& file_id)
{
  const std::vector<RemoteFile> files = getFiles(community, mod_id);
  for(const auto& f : files)
  {
    if(f.id == file_id)
    {
      if(f.download_url.empty())
        throw std::runtime_error(
          std::format("GameBanana: no download URL for file \"{}\" of mod \"{}\".",
                      file_id,
                      mod_id));
      return f.download_url;
    }
  }
  throw std::runtime_error(
    std::format("GameBanana: file \"{}\" not found for mod \"{}\".", file_id, mod_id));
}
