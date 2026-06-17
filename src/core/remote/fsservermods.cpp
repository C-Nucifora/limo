#include "fsservermods.h"
#include <algorithm>
#include <cctype>
#include <cpr/cpr.h>
#include <format>
#include <regex>
#include <set>


namespace
{
constexpr int kRequestTimeoutMs = 20000;

std::string toLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

// Returns the scheme+host(+port) origin of an absolute URL, or "" if it can't be determined.
std::string originOf(const std::string& url)
{
  const std::size_t scheme = url.find("://");
  if(scheme == std::string::npos)
    return "";
  const std::size_t path = url.find('/', scheme + 3);
  return path == std::string::npos ? url : url.substr(0, path);
}

// Returns the directory portion of a URL (up to and including the last '/' of the path).
std::string dirOf(const std::string& url)
{
  const std::size_t scheme = url.find("://");
  const std::size_t search_from = scheme == std::string::npos ? 0 : scheme + 3;
  const std::size_t last_slash = url.find_last_of('/');
  if(last_slash == std::string::npos || last_slash < search_from)
    return url + "/";
  return url.substr(0, last_slash + 1);
}

// Percent-decodes the common %20 (space) sequence in a file name; other escapes are left as-is.
std::string decodeFileName(std::string name)
{
  std::size_t pos = 0;
  while((pos = name.find("%20", pos)) != std::string::npos)
    name.replace(pos, 3, " ");
  return name;
}

std::string baseName(const std::string& path)
{
  // Strip any query/fragment, then take the last path segment.
  std::string clean = path.substr(0, path.find_first_of("?#"));
  const std::size_t slash = clean.find_last_of('/');
  return slash == std::string::npos ? clean : clean.substr(slash + 1);
}
} // namespace


std::string remote::FsServerMods::normalizeUrl(const std::string& server_url)
{
  std::string url = server_url;
  // Trim surrounding whitespace.
  const auto first = url.find_first_not_of(" \t\r\n");
  const auto last = url.find_last_not_of(" \t\r\n");
  if(first == std::string::npos)
    return "";
  url = url.substr(first, last - first + 1);

  if(url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
    url = "http://" + url;

  // Strip a trailing slash for consistent handling.
  if(url.size() > 1 && url.back() == '/')
    url.pop_back();

  // If there is no path beyond the host, point at the standard /mods index.
  const std::size_t scheme = url.find("://");
  const std::size_t path = url.find('/', scheme + 3);
  if(path == std::string::npos)
    url += "/mods";
  return url;
}

std::vector<remote::FsServerMod> remote::FsServerMods::parseModIndex(const std::string& html,
                                                                     const std::string& index_url)
{
  std::vector<FsServerMod> mods;
  std::set<std::string> seen_urls;
  const std::string origin = originOf(index_url);
  const std::string dir = dirOf(index_url);

  // Match href="...zip" (single or double quoted), case-insensitive on the extension.
  static const std::regex href_regex(R"(href\s*=\s*["']([^"'<>]+?\.zip)["'])",
                                     std::regex::icase);
  for(auto it = std::sregex_iterator(html.begin(), html.end(), href_regex);
      it != std::sregex_iterator();
      ++it)
  {
    std::string href = (*it)[1].str();
    // Skip parent-directory and obviously non-file links.
    if(href.empty() || href.find("..") != std::string::npos)
      continue;

    std::string absolute;
    if(toLower(href).rfind("http://", 0) == 0 || toLower(href).rfind("https://", 0) == 0)
      absolute = href;
    else if(href.front() == '/')
      absolute = origin + href;
    else
      absolute = dir + href;

    if(!seen_urls.insert(absolute).second)
      continue;

    const std::string name = decodeFileName(baseName(href));
    if(name.empty())
      continue;
    mods.push_back({ name, absolute });
  }
  return mods;
}

std::vector<remote::FsServerMod> remote::FsServerMods::fetchModList(const std::string& server_url,
                                                                    std::string& error)
{
  error.clear();
  const std::string index_url = normalizeUrl(server_url);
  if(index_url.empty())
  {
    error = "Please enter a server address.";
    return {};
  }
  cpr::Response response = cpr::Get(cpr::Url(index_url), cpr::Timeout{ kRequestTimeoutMs });
  if(response.error)
  {
    error = std::format("Could not reach '{}': {}", index_url, response.error.message);
    return {};
  }
  if(response.status_code != 200)
  {
    error = std::format("Server returned HTTP {} for '{}'.", response.status_code, index_url);
    return {};
  }
  std::vector<FsServerMod> mods = parseModIndex(response.text, index_url);
  if(mods.empty())
    error = std::format("No .zip mods found at '{}'.", index_url);
  return mods;
}
