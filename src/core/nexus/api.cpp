#include "api.h"
#include "../consts.h"
#include "../log.h"
#include "../parseerror.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iostream>
#include <json/json.h>
#include <ranges>
#include <regex>

using namespace nexus;
namespace str = std::ranges;

namespace
{
/*!
 * \brief Builds the standard header set for Nexus API requests. A real User-Agent (and the
 * Nexus-recommended Application-Name/Version) is required so requests are not rejected by
 * Cloudflare's bot challenge, which blocks the default libcurl agent (limo-app/limo#220).
 */
cpr::Header authHeader(const std::string& key)
{
  return cpr::Header{
    { "apikey", key },
    { "User-Agent", std::string("Limo/") + APP_VERSION + " (+https://github.com/limo-app/limo)" },
    { "Application-Name", "Limo" },
    { "Application-Version", APP_VERSION },
  };
}
}


void Api::setApiKey(const std::string& api_key)
{
  api_key_ = api_key;
}

bool Api::isInitialized()
{
  return !api_key_.empty();
}

Mod Api::getMod(const std::string& mod_url)
{
  auto domain_and_mod = extractDomainAndModId(mod_url);
  if(!domain_and_mod)
    throw std::runtime_error(std::format("Could not parse mod URL: \"{}\".", mod_url));
  return getMod(domain_and_mod->first, domain_and_mod->second);
}

Mod Api::getMod(const std::string& domain_name, long mod_id)
{
  cpr::Response response =
    cpr::Get(cpr::Url(std::format(
               "https://api.nexusmods.com/v1/games/{}/mods/{}.json", domain_name, mod_id)),
             authHeader(api_key_));
  if(response.status_code != 200)
    throw std::runtime_error(
      std::format("Failed to get data for mod with id {} from NexusMods. Response code was {}",
                  mod_id,
                  response.status_code));
  return { response.text };
}

void Api::trackMod(const std::string& mod_url)
{
  auto domain_and_mod = extractDomainAndModId(mod_url);
  if(!domain_and_mod)
    throw std::runtime_error(std::format("Could not parse mod URL: \"{}\".", mod_url));
  const cpr::Response response =
    cpr::Post(cpr::Url("https://api.nexusmods.com/v1/user/tracked_mods.json"),
              authHeader(api_key_),
              cpr::Parameters{ { "domain_name", domain_and_mod->first },
                               { "mod_id", std::to_string(domain_and_mod->second) } });
}

void Api::untrackMod(const std::string& mod_url)
{
  auto domain_and_mod = extractDomainAndModId(mod_url);
  if(!domain_and_mod)
    throw std::runtime_error(std::format("Could not parse mod URL: \"{}\".", mod_url));
  const cpr::Response response =
    cpr::Delete(cpr::Url("https://api.nexusmods.com/v1/user/tracked_mods.json"),
                authHeader(api_key_),
                cpr::Parameters{ { "domain_name", domain_and_mod->first },
                                 { "mod_id", std::to_string(domain_and_mod->second) } });
}

std::vector<Mod> Api::getTrackedMods()
{
  cpr::Response response = cpr::Get(cpr::Url("https://api.nexusmods.com/v1/user/tracked_mods.json"),
                                    authHeader(api_key_));
  if(response.status_code != 200)
    throw std::runtime_error(std::format(
      "Failed to get tracked mods from NexusMods. Response code was: {}", response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  bool success = reader.parse(response.text.c_str(), json_body);
  if(!success)
    throw ParseError("Failed to parse response from NexusMods.");

  std::vector<Mod> mods;
  for(int i = 0; i < json_body.size(); i++)
    mods.push_back(
      getMod(json_body[i]["domain_name"].asString(), json_body[i]["mod_id"].asInt64()));
  return mods;
}

std::vector<File> Api::getModFiles(const std::string& mod_url)
{
  auto domain_and_mod = extractDomainAndModId(mod_url);
  if(!domain_and_mod)
    throw std::runtime_error(std::format("Could not parse mod URL: \"{}\".", mod_url));

  const auto [domain_name, mod_id] = *domain_and_mod;
  cpr::Response response =
    cpr::Get(cpr::Url(std::format(
               "https://api.nexusmods.com/v1/games/{}/mods/{}/files.json", domain_name, mod_id)),
             authHeader(api_key_));
  if(response.status_code != 200)
    throw std::runtime_error(
      std::format("Failed to get mod files for mod with id {} from NexusMods. Response code was {}",
                  mod_id,
                  response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  bool success = reader.parse(response.text.c_str(), json_body);
  if(!success)
    throw ParseError("Failed to parse response from NexusMods.");

  std::vector<File> files;
  for(int i = 0; i < json_body["files"].size(); i++)
    files.emplace_back(json_body["files"][i]);
  return files;
}

std::string Api::getDownloadUrl(const std::string& mod_url, long file_id)
{
  auto domain_and_mod = extractDomainAndModId(mod_url);
  if(!domain_and_mod)
    throw std::runtime_error(std::format("Could not parse mod URL: \"{}\".", mod_url));

  const auto [domain_name, mod_id] = *domain_and_mod;
  cpr::Response response =
    cpr::Get(cpr::Url(std::format(
               "https://api.nexusmods.com/v1/games/{}/mods/{}/files/{}/download_link.json",
               domain_name,
               mod_id,
               file_id)),
             authHeader(api_key_));
  if(response.status_code == 403)
    throw std::runtime_error(
      "Generation of download links for NexusMods is restricted to premium accounts."
      "You can download the mod on the website here:\n" +
      std::format(
        "https://www.nexusmods.com/{}/mods/{}?tab=files&file_id={}", domain_name, mod_id, file_id));
  else if(response.status_code == 404)
    throw std::runtime_error("The requested file does not exist in NexusMods.");
  else if(response.status_code != 200)
    throw std::runtime_error(std::format("Failed to generate a download link for \"{}\"", mod_url));

  Json::Value json_body;
  Json::Reader reader;
  bool success = reader.parse(response.text.c_str(), json_body);
  if(!success)
    throw ParseError("Failed to parse response from NexusMods.");

  return json_body[0]["URI"].asString();
}

std::string Api::getDownloadUrl(const std::string& nxm_url)
{
  const auto match_opt = nxmUrlIsValid(nxm_url);
  if(!match_opt)
    throw std::runtime_error(std::format("Invalid NXM URL: \"{}\"", nxm_url));
  std::smatch match = *match_opt;
  const std::string domain_name = match[1];
  // Security: domain_name is interpolated into the API URL path; reject anything but a plain
  // game-domain token so a crafted nxm:// link cannot redirect the call to another endpoint.
  if(!std::regex_match(domain_name, std::regex("[a-zA-Z0-9]+")))
    throw std::runtime_error(std::format("Invalid game domain in NXM URL: \"{}\"", nxm_url));
  const std::string mod_id = match[2];
  const std::string file_id = match[3];
  const std::string key = match[4];
  const std::string expires = match[5];

  cpr::Response response =
    cpr::Get(cpr::Url(std::format(
               "https://api.nexusmods.com/v1/games/{}/mods/{}/files/{}/download_link.json",
               domain_name,
               mod_id,
               file_id)),
             authHeader(api_key_),
             cpr::Parameters{ { "game_domain_name", domain_name },
                              { "id", file_id },
                              { "mod_id", mod_id },
                              { "key", key },
                              { "expires", expires } });
  if(response.status_code == 400)
    throw std::runtime_error("Failed to generate download link. Check if the account used on "
                             "NexusMods matches the one for the API key in Limo.");
  else if(response.status_code == 404)
    throw std::runtime_error(std::format("File with id {} for mod with id {} for application"
                                         "\"{}\" not found on NexusMods.",
                                         file_id,
                                         mod_id,
                                         domain_name));
  else if(response.status_code == 410)
    throw std::runtime_error("The NexusMods download link has expired.");
  else if(response.status_code != 200)
    throw std::runtime_error(std::format("Failed to generate download link for file with id {} "
                                         "for mod with id {} for application {}.",
                                         file_id,
                                         mod_id,
                                         domain_name));

  Json::Value json_body;
  Json::Reader reader;
  bool success = reader.parse(response.text.c_str(), json_body);
  if(!success)
    throw ParseError("Failed to parse response from NexusMods.");

  return json_body[0]["URI"].asString();
}

std::vector<std::pair<std::string, std::vector<std::string>>> Api::getChangelogs(
  const std::string& mod_url)
{
  std::vector<std::pair<std::string, std::vector<std::string>>> changelogs;
  auto domain_and_mod = extractDomainAndModId(mod_url);
  if(!domain_and_mod)
    throw std::runtime_error(std::format("Could not parse mod URL: \"{}\".", mod_url));

  const auto [domain_name, mod_id] = *domain_and_mod;
  cpr::Response response = cpr::Get(
    cpr::Url(std::format(
      "https://api.nexusmods.com/v1/games/{}/mods/{}/changelogs.json", domain_name, mod_id)),
    authHeader(api_key_));
  if(response.status_code != 200)
    throw std::runtime_error(std::format(
      "Failed to get changelogs for mod with id {} from NexusMods. Response code was {}",
      mod_id,
      response.status_code));

  Json::Value json_body;
  Json::Reader reader;
  bool success = reader.parse(response.text.c_str(), json_body);
  if(!success)
    throw ParseError("Failed to parse response from NexusMods.");

  std::string text = response.text;
  if(text.starts_with('\"'))
    text.erase(0, 1);
  if(text.ends_with('\"'))
    text.erase(text.size() - 1, 1);

  for(const auto& key : json_body.getMemberNames())
  {
    std::vector<std::string> changes;
    auto log = json_body[key];
    for(int i = 0; i < log.size(); i++)
      changes.push_back(log[i].asString());
    changelogs.emplace_back(key, changes);
  }
  // Jsoncpp uses a std::map to store key, value pairs. This messes up the order of the keys, so
  // they have be re-sorted by version number
  std::sort(changelogs.begin(),
            changelogs.end(),
            [](auto a, auto b)
            {
              std::regex regex(R"(.*?(\d+)\.?(.*))");
              std::smatch match;
              std::vector<int> a_parts;
              std::vector<int> b_parts;
              std::string target = a.first;
              bool found = false;
              while(std::regex_search(target, match, regex))
              {
                found = true;
                a_parts.push_back(std::stoi(match[1]));
                target = match[2];
              }
              if(!found)
                return a > b;

              found = false;
              target = b.first;
              while(std::regex_search(target, match, regex))
              {
                found = true;
                b_parts.push_back(std::stoi(match[1]));
                target = match[2];
              }
              if(!found)
                return a > b;

              for(auto [a_num, b_num] : str::zip_view(a_parts, b_parts))
              {
                if(a_num != b_num)
                  return a_num > b_num;
              }
              return a > b;
            });
  return changelogs;
}

bool Api::modUrlIsValid(const std::string& url)
{
  if(url.empty())
    return false;
  const std::regex regex(R"((?:https:\/\/)?www\.nexusmods\.com\/(.+)\/mods\/(\d+).*)");
  return std::regex_match(url, regex);
}

Page Api::getNexusPage(const std::string& mod_url)
{
  return { mod_url, getMod(mod_url), getChangelogs(mod_url), getModFiles(mod_url) };
}

std::optional<std::pair<std::string, bool>> Api::validateKey(const std::string& api_key)
{
  cpr::Response response = cpr::Get(cpr::Url("https://api.nexusmods.com/v1/users/validate.json"),
                                    authHeader(api_key));
  if(response.status_code != 200)
    return {};

  Json::Value json_body;
  Json::Reader reader;
  bool success = reader.parse(response.text.c_str(), json_body);
  if(!success)
    throw ParseError("Failed to parse response from NexusMods.");

  return { { json_body["name"].asString(), json_body["is_premium"].asBool() } };
}

std::string Api::getNexusPageUrl(const std::string& nxm_url)
{
  std::regex nxm_regex(R"(nxm:\/\/(.*)\/mods\/(\d+)\/files\/\d+\?.*)");
  std::smatch match;
  if(!std::regex_match(nxm_url, match, nxm_regex))
    throw std::runtime_error("Invalid nxm url: \"" + nxm_url + "\".");
  return std::format("https://www.nexusmods.com/{}/mods/{}", match[1].str(), match[2].str());
}

std::string Api::getApiKey()
{
  return api_key_;
}

std::optional<std::pair<std::string, int>> Api::extractDomainAndModId(const std::string& mod_url)
{
  const std::regex regex(R"((?:https:\/\/)?www\.nexusmods\.com\/(.+)\/mods\/(\d+).*)");
  std::smatch match;
  if(std::regex_match(mod_url, match, regex))
    return { { match[1], std::stoi(match[2]) } };
  return {};
}

bool Api::initModInfo(ImportModInfo& info)
{
  std::vector<File> files;

  // The built-in Nexus "Files" tab populates remote_source + remote_file_id directly and never sets
  // an nxm:// request URL. Without this branch initModInfo would bail on the empty request URL below,
  // silently aborting the download (see limo-app/limo#261, #41).
  if(info.remote_request_url.empty() && modUrlIsValid(info.remote_source) && info.remote_file_id >= 0)
  {
    files = getModFiles(info.remote_source);
    auto iter = str::find_if(files, [&info](File& f) { return f.file_id == info.remote_file_id; });
    if(iter == files.end())
      return false;
    auto domain_and_mod = extractDomainAndModId(info.remote_source);
    if(!domain_and_mod)
      return false;
    info.remote_mod_id = (*domain_and_mod).second;
    info.remote_file_name = iter->name;
    info.remote_file_version = iter->version;
    info.remote_type = ImportModInfo::RemoteType::nexus;
    return true;
  }

  auto match = nxmUrlIsValid(info.remote_request_url);
  if(!match)
    return false;

  if(modUrlIsValid(info.remote_source))
    files = getModFiles(info.remote_source);
  else
  {
    info.remote_source = getNexusPageUrl(info.remote_request_url);
    files = getModFiles(info.remote_source);
  }

  const std::string file_id_str = (*match)[3];
  if(file_id_str.find_first_not_of("0123456789") != std::string::npos)
    return false;

  const std::string mod_id_str = (*match)[2];
  if(mod_id_str.find_first_not_of("0123456789") != std::string::npos)
    return false;

  long file_id = std::stol(file_id_str);
  long mod_id = std::stol(mod_id_str);
  auto iter = str::find_if(files, [file_id](File& f){return f.file_id == file_id;});
  if(iter == files.end())
    return false;

  info.remote_mod_id = mod_id;
  info.remote_file_id = iter->file_id;
  info.remote_file_name = iter->name;
  info.remote_file_version = iter->version;
  info.remote_type = ImportModInfo::RemoteType::nexus;

  return true;
}

std::vector<SearchResult> Api::fetchModListing(const std::string& domain_name,
                                               const std::string& endpoint)
{
  std::vector<SearchResult> results;
  cpr::Response response = cpr::Get(
    cpr::Url(std::format(
      "https://api.nexusmods.com/v1/games/{}/mods/{}.json", domain_name, endpoint)),
    cpr::Header{ { "apikey", api_key_ },
                 { "User-Agent", "Limo" },
                 { "Accept", "application/json" } });

  if(response.status_code == 429)
  {
    Log::warning("NexusMods search: rate limit reached (429). Try again later.");
    return results;
  }
  if(response.status_code == 403)
  {
    Log::warning("NexusMods search: access forbidden (403). This endpoint may be "
                 "restricted to premium accounts.");
    return results;
  }
  if(response.status_code != 200)
  {
    Log::warning(std::format("NexusMods search: request to \"{}\" failed with code {}.",
                             endpoint,
                             response.status_code));
    return results;
  }

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
  {
    Log::error("NexusMods search: failed to parse response from NexusMods.");
    return results;
  }

  // The listing endpoints return a plain JSON array of mods.
  for(int i = 0; i < json_body.size(); i++)
  {
    try
    {
      Mod mod(json_body[i]);
      std::string mod_domain = mod.domain_name.empty() ? domain_name : mod.domain_name;
      results.push_back({ mod_domain, mod });
    }
    catch(const std::exception& e)
    {
      Log::warning(std::format("NexusMods search: skipping malformed entry: {}", e.what()));
    }
  }
  return results;
}

std::vector<SearchResult> Api::searchMods(const std::string& domain_name,
                                          const std::string& query,
                                          SortOrder sort_order,
                                          int category_id)
{
  if(!isInitialized())
  {
    Log::warning("NexusMods search: no API key configured.");
    return {};
  }
  if(domain_name.empty())
  {
    Log::warning("NexusMods search: no NexusMods domain given.");
    return {};
  }

  // The public v1 API has no free text search endpoint. We fetch the listing matching the
  // requested sort order and filter/sort client-side.
  const std::string endpoint =
    sort_order == SortOrder::recent ? "latest_updated" : "trending";
  std::vector<SearchResult> results = fetchModListing(domain_name, endpoint);

  // Filter by query (case-insensitive substring on name and summary).
  if(!query.empty())
  {
    std::string needle = query;
    std::ranges::transform(needle, needle.begin(), [](unsigned char c) { return std::tolower(c); });
    auto contains = [&needle](const std::string& haystack)
    {
      std::string lower = haystack;
      std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return std::tolower(c); });
      return lower.find(needle) != std::string::npos;
    };
    std::erase_if(results,
                  [&](const SearchResult& r)
                  { return !contains(r.mod.name) && !contains(r.mod.summary); });
  }

  // Optional category filter.
  if(category_id >= 0)
    std::erase_if(results,
                  [&](const SearchResult& r) { return r.mod.category_id != category_id; });

  // Sort client-side according to the requested order.
  switch(sort_order)
  {
    case SortOrder::endorsements:
      std::ranges::sort(results,
                        [](const SearchResult& a, const SearchResult& b)
                        { return a.mod.endorsement_count > b.mod.endorsement_count; });
      break;
    case SortOrder::downloads:
      std::ranges::sort(results,
                        [](const SearchResult& a, const SearchResult& b)
                        { return a.mod.mod_downloads > b.mod.mod_downloads; });
      break;
    case SortOrder::recent:
      std::ranges::sort(results,
                        [](const SearchResult& a, const SearchResult& b)
                        { return a.mod.updated_time > b.mod.updated_time; });
      break;
  }

  return results;
}

std::optional<std::smatch> Api::nxmUrlIsValid(const std::string& nxm_url)
{
  const std::regex regex(
    R"(nxm:\/\/(.+)\/mods\/(\d+)\/files\/(\d+)\?key=(.+)&expires=(\d+)&user_id=(\d+))");
  std::smatch match;
  std::regex_match(nxm_url, match, regex);
  if(match.empty())
    return {};
  return match;
}

std::optional<std::pair<GitPlatform, std::pair<std::string, std::string>>> Api::gitRepoFromUrl(
  const std::string& url)
{
  if(url.empty())
    return {};
  // Matches https://github.com/<owner>/<repo> or https://gitlab.com/<owner>/<repo>.
  // The repo name stops at the first '/', '?', '#' or end of string and an optional trailing
  // ".git" is stripped.
  const std::regex regex(
    R"((?:https:\/\/)?(?:www\.)?(github|gitlab)\.com\/([^\/\s]+)\/([^\/\s?#]+?)(?:\.git)?(?:[\/?#].*)?)");
  std::smatch match;
  if(!std::regex_match(url, match, regex))
    return {};
  const GitPlatform platform = match[1] == "github" ? GitPlatform::github : GitPlatform::gitlab;
  return { { platform, { match[2], match[3] } } };
}

cpr::Header Api::gitAuthHeader()
{
  // GitHub rejects requests without a User-Agent header; GitLab tolerates it.
  return cpr::Header{ { "User-Agent", "Limo" } };
}

std::time_t Api::parseIso8601(const std::string& timestamp)
{
  if(timestamp.empty())
    return 0;
  std::tm tm{};
  // git platforms return UTC timestamps such as 2023-01-02T03:04:05Z or with a fractional part.
  if(std::sscanf(timestamp.c_str(),
                 "%4d-%2d-%2dT%2d:%2d:%2d",
                 &tm.tm_year,
                 &tm.tm_mon,
                 &tm.tm_mday,
                 &tm.tm_hour,
                 &tm.tm_min,
                 &tm.tm_sec) != 6)
    return 0;
  tm.tm_year -= 1900;
  tm.tm_mon -= 1;
#ifdef _WIN32
  return _mkgmtime(&tm);
#else
  return timegm(&tm);
#endif
}

std::optional<GitRelease> Api::getLatestGitRelease(const std::string& repo_url)
{
  const auto repo = gitRepoFromUrl(repo_url);
  if(!repo)
    return {};

  const GitPlatform platform = repo->first;
  const std::string& owner = repo->second.first;
  const std::string& name = repo->second.second;

  std::string request_url;
  if(platform == GitPlatform::github)
    request_url =
      std::format("https://api.github.com/repos/{}/{}/releases/latest", owner, name);
  else
    request_url = std::format(
      "https://gitlab.com/api/v4/projects/{}%2F{}/releases", owner, name);

  cpr::Response response = cpr::Get(cpr::Url(request_url), gitAuthHeader());
  if(response.status_code != 200)
  {
    std::cerr << std::format("Failed to get latest release for \"{}\". Response code was {}.",
                             repo_url,
                             response.status_code)
              << std::endl;
    return {};
  }

  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(response.text.c_str(), json_body))
  {
    std::cerr << std::format("Failed to parse release response for \"{}\".", repo_url) << std::endl;
    return {};
  }

  GitRelease release;
  release.platform = platform;
  if(platform == GitPlatform::github)
  {
    release.version = json_body["tag_name"].asString();
    release.published_time = parseIso8601(json_body["published_at"].asString());
  }
  else
  {
    // GitLab returns an array of releases, newest first.
    if(!json_body.isArray() || json_body.empty())
      return {};
    release.version = json_body[0]["tag_name"].asString();
    release.published_time = parseIso8601(json_body[0]["released_at"].asString());
  }

  if(release.version.empty())
    return {};
  return release;
}
