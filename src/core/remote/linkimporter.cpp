#include "linkimporter.h"
#include "../consts.h"
#include "../log.h"
#include <algorithm>
#include <cpr/cpr.h>
#include <filesystem>
#include <format>
#include <json/json.h>
#include <regex>
#include <string>

namespace sfs = std::filesystem;


// A current desktop-Firefox User-Agent. ModHub's CDN rejects a bare/library User-Agent
// (403); a real browser UA + Referer is the documented hotlink gate (fork #233).
const std::string remote::LinkImporter::browser_user_agent =
  "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0";

const std::string remote::LinkImporter::limo_user_agent =
  std::string("Limo/") + APP_VERSION + " (+https://github.com/limo-app/limo)";

namespace
{
constexpr int kRequestTimeoutMs = 20000;

// Lower-cases an ASCII string in place-copy.
std::string toLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

// Whether a file name ends in a recognized mod-archive extension.
bool isArchiveName(const std::string& name)
{
  const std::string lower = toLower(name);
  for(const char* ext : { ".zip", ".7z", ".rar", ".scs", ".tar.gz", ".tgz" })
  {
    const std::string e = ext;
    if(lower.size() >= e.size() && lower.compare(lower.size() - e.size(), e.size(), e) == 0)
      return true;
  }
  return false;
}

remote::ResolvedLink makeError(const std::string& message)
{
  remote::ResolvedLink result;
  result.ok = false;
  result.error = message;
  return result;
}
} // namespace


bool remote::LinkImporter::isModHubUrl(const std::string& url)
{
  // The FS ModHub mod page is farming-simulator.com/mod.php?mod_id=...
  const std::string lower = toLower(url);
  return lower.find("farming-simulator.com/mod.php") != std::string::npos &&
         lower.find("mod_id=") != std::string::npos;
}

bool remote::LinkImporter::isGithubUrl(const std::string& url)
{
  const std::string lower = toLower(url);
  return lower.find("github.com/") != std::string::npos;
}

bool remote::LinkImporter::isSupportedUrl(const std::string& url)
{
  return isGithubUrl(url) || isModHubUrl(url);
}

remote::ResolvedLink remote::LinkImporter::resolve(const std::string& url,
                                                   bool allow_modhub,
                                                   const std::string& github_token)
{
  if(isGithubUrl(url))
    return resolveGithub(url, github_token);
  if(isModHubUrl(url))
  {
    if(!allow_modhub)
      return makeError(
        "ModHub link import is experimental and currently disabled. Enable it under "
        "Settings → NexusMods, or download the .zip in your browser and import the file "
        "directly (the robust, always-supported path).");
    return resolveModHub(url);
  }
  return makeError("Unrecognized URL. Paste a GitHub release/repository URL, or a Farming "
                   "Simulator ModHub mod-page URL (farming-simulator.com/mod.php?mod_id=...).");
}


// ----------------------------------------------------------------------------------------
//  ModHub
// ----------------------------------------------------------------------------------------

remote::ResolvedLink remote::LinkImporter::parseModHubPage(const std::string& html,
                                                           const std::string& mod_page_url)
{
  // The mod page links the exact CDN archive: cdn<N>.giants-software.com/modHub/storage/<id>/<file>.zip
  // Match precisely on that documented path so we never pick up image/asset CDNs.
  static const std::regex cdn_regex(
    R"(https?://cdn\d+\.giants-software\.com/modHub/storage/\d+/[^"'\s<>\\]+\.zip)",
    std::regex::icase);
  std::smatch match;
  if(!std::regex_search(html, match, cdn_regex))
    return makeError("Could not find a ModHub download link on that page. The page layout or "
                     "hotlink gate may have changed — download the .zip manually and import "
                     "the file instead.");

  ResolvedLink result;
  result.ok = true;
  result.download_url = match.str();
  result.file_name = sfs::path(result.download_url).filename().string();
  result.user_agent = browser_user_agent;
  result.referer = mod_page_url;

  // Best-effort mod name from the page <title>; fall back to the file stem.
  static const std::regex title_regex(R"(<title>([^<]*)</title>)", std::regex::icase);
  std::smatch title_match;
  if(std::regex_search(html, title_match, title_regex))
  {
    std::string title = title_match[1].str();
    // Trim a trailing " - Farming Simulator ..." / " | ..." site suffix.
    for(const std::string& sep : { " - ", " | " })
    {
      const auto pos = title.find(sep);
      if(pos != std::string::npos)
        title = title.substr(0, pos);
    }
    // Trim surrounding whitespace.
    const auto first = title.find_first_not_of(" \t\r\n");
    const auto last = title.find_last_not_of(" \t\r\n");
    if(first != std::string::npos)
      title = title.substr(first, last - first + 1);
    if(!title.empty())
      result.mod_name = title;
  }
  if(result.mod_name.empty())
    result.mod_name = sfs::path(result.file_name).stem().string();
  return result;
}

remote::ResolvedLink remote::LinkImporter::resolveModHub(const std::string& mod_page_url)
{
  cpr::Response response = cpr::Get(cpr::Url(mod_page_url),
                                    cpr::Header{ { "User-Agent", browser_user_agent } },
                                    cpr::Timeout{ kRequestTimeoutMs });
  if(response.status_code != 200)
    return makeError(std::format(
      "Could not load the ModHub mod page (HTTP {}). Download the .zip manually and import it.",
      response.status_code));
  return parseModHubPage(response.text, mod_page_url);
}


// ----------------------------------------------------------------------------------------
//  GitHub Releases
// ----------------------------------------------------------------------------------------

remote::ResolvedLink remote::LinkImporter::parseGithubReleaseJson(const std::string& json_text,
                                                                  const std::string& repo)
{
  Json::Value root;
  Json::CharReaderBuilder builder;
  std::string errors;
  std::istringstream stream(json_text);
  if(!Json::parseFromStream(builder, stream, &root, &errors))
    return makeError("Could not read the GitHub release data. Download the asset manually and "
                     "import the file instead.");

  const std::string tag = root.get("tag_name", "").asString();

  // Prefer a published archive asset.
  if(root.isMember("assets") && root["assets"].isArray())
  {
    for(const auto& asset : root["assets"])
    {
      const std::string name = asset.get("name", "").asString();
      const std::string url = asset.get("browser_download_url", "").asString();
      if(url.empty() || name.empty() || !isArchiveName(name))
        continue;
      ResolvedLink result;
      result.ok = true;
      result.download_url = url;
      result.file_name = name;
      result.mod_name = repo;
      result.version = tag;
      // GitHub requires a User-Agent; the asset CDN accepts Limo's.
      result.user_agent = limo_user_agent;
      return result;
    }
  }

  // Fall back to the source code zipball when the release has no archive asset.
  const std::string zipball = root.get("zipball_url", "").asString();
  if(!zipball.empty())
  {
    ResolvedLink result;
    result.ok = true;
    result.download_url = zipball;
    const std::string tag_label = tag.empty() ? "source" : tag;
    result.file_name = repo + "-" + tag_label + ".zip";
    result.mod_name = repo;
    result.version = tag;
    result.user_agent = limo_user_agent;
    return result;
  }

  return makeError("That GitHub release has no downloadable archive. Download a file manually "
                   "and import it instead.");
}

remote::ResolvedLink remote::LinkImporter::resolveGithub(const std::string& url,
                                                         const std::string& github_token)
{
  // Pull owner/repo (and optional release tag) out of the URL.
  static const std::regex repo_regex(R"(github\.com/([^/\s?#]+)/([^/\s?#]+))", std::regex::icase);
  std::smatch match;
  if(!std::regex_search(url, match, repo_regex))
    return makeError("Could not parse a GitHub owner/repository from that URL.");
  const std::string owner = match[1].str();
  std::string repo = match[2].str();
  if(repo.size() > 4 && repo.compare(repo.size() - 4, 4, ".git") == 0)
    repo = repo.substr(0, repo.size() - 4);

  static const std::regex tag_regex(R"(/releases/tag/([^/\s?#]+))", std::regex::icase);
  std::smatch tag_match;
  std::string api_url;
  if(std::regex_search(url, tag_match, tag_regex))
    api_url = std::format(
      "https://api.github.com/repos/{}/{}/releases/tags/{}", owner, repo, tag_match[1].str());
  else
    api_url = std::format("https://api.github.com/repos/{}/{}/releases/latest", owner, repo);

  cpr::Header headers{ { "User-Agent", limo_user_agent },
                       { "Accept", "application/vnd.github+json" } };
  // issue #239: an optional token lifts the unauthenticated 60-req/hr GitHub API limit.
  if(!github_token.empty())
    headers["Authorization"] = "Bearer " + github_token;
  cpr::Response response =
    cpr::Get(cpr::Url(api_url), headers, cpr::Timeout{ kRequestTimeoutMs });
  if(response.status_code == 404)
    return makeError("No GitHub release found for that repository (it may have no published "
                     "releases). Download a file manually and import it instead.");
  if(response.status_code == 403)
    return makeError("GitHub rate limit reached. Try again later, or download the asset manually "
                     "and import the file.");
  if(response.status_code != 200)
    return makeError(std::format("GitHub request failed (HTTP {}).", response.status_code));
  return parseGithubReleaseJson(response.text, repo);
}
