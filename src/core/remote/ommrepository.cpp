#include "ommrepository.h"
#include "../log.h"
#include <cctype>
#include <charconv>
#include <cpr/cpr.h>
#include <format>
#include <pugixml.hpp>

using namespace remote;

namespace
{
/*! \brief Returns the first non-empty attribute among \p names on \p node. */
std::string firstAttr(const pugi::xml_node& node, std::initializer_list<const char*> names)
{
  for(const char* name : names)
  {
    const char* value = node.attribute(name).value();
    if(value && *value)
      return value;
  }
  return {};
}

/*! \brief Returns the first non-empty child element text among \p names on \p node. */
std::string firstChildText(const pugi::xml_node& node, std::initializer_list<const char*> names)
{
  for(const char* name : names)
  {
    pugi::xml_node child = node.child(name);
    if(child)
    {
      const char* text = child.text().get();
      if(text && *text)
        return text;
    }
  }
  return {};
}

/*! \brief Returns the first value found either as an attribute or a child element of \p node. */
std::string firstValue(const pugi::xml_node& node, std::initializer_list<const char*> names)
{
  std::string value = firstAttr(node, names);
  if(!value.empty())
    return value;
  return firstChildText(node, names);
}

/*! \brief Returns true if \p name (case-insensitive) matches one of the package element names. */
bool isPackageElement(const std::string& name)
{
  std::string lower;
  lower.reserve(name.size());
  for(char c : name)
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return lower == "remote" || lower == "package" || lower == "mod" || lower == "entry";
}

/*! \brief Parses a long from \p text, returning -1 on failure. */
long parseLong(const std::string& text)
{
  if(text.empty())
    return -1;
  long value = -1;
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if(ec != std::errc{})
    return -1;
  return value;
}
} // namespace

OmmRepository::OmmRepository(std::string url, std::string user, std::string password) :
  url_(std::move(url)), user_(std::move(user)), password_(std::move(password))
{}

bool OmmRepository::connect()
{
  connected_ = fetchAndParse();
  return connected_;
}

bool OmmRepository::isConnected() const
{
  return connected_;
}

std::string OmmRepository::title() const
{
  return title_;
}

std::string OmmRepository::url() const
{
  return url_;
}

std::vector<RemotePackage> OmmRepository::listPackages()
{
  if(!connected_)
  {
    if(!fetchAndParse())
      return {};
    connected_ = true;
  }
  return packages_;
}

std::optional<std::string> OmmRepository::resolveDownloadUrl(const RemotePackage& package) const
{
  const std::string raw = package.primaryDownloadUrl();
  if(raw.empty())
  {
    Log::warning(std::format("OMM: package \"{}\" has no downloadable file.", package.name));
    return {};
  }
  return resolveUrl(raw);
}

std::optional<RemotePackage> OmmRepository::checkForUpdate(const std::string& package_name,
                                                          const std::string& installed_version)
{
  std::optional<RemotePackage> newest;
  for(const RemotePackage& package : listPackages())
  {
    if(package.name != package_name)
      continue;
    if(!newest || compareVersions(package.version, newest->version) > 0)
      newest = package;
  }
  if(!newest)
    return {};
  if(compareVersions(newest->version, installed_version) > 0)
    return newest;
  return {};
}

int OmmRepository::compareVersions(const std::string& a, const std::string& b)
{
  auto split = [](const std::string& version)
  {
    std::vector<std::string> parts;
    std::string current;
    for(char c : version)
    {
      if(c == '.' || c == '-' || c == '_')
      {
        parts.push_back(current);
        current.clear();
      }
      else
        current.push_back(c);
    }
    parts.push_back(current);
    return parts;
  };

  const std::vector<std::string> a_parts = split(a);
  const std::vector<std::string> b_parts = split(b);
  const std::size_t count = std::max(a_parts.size(), b_parts.size());
  for(std::size_t i = 0; i < count; i++)
  {
    const std::string a_part = i < a_parts.size() ? a_parts[i] : "0";
    const std::string b_part = i < b_parts.size() ? b_parts[i] : "0";

    const long a_num = parseLong(a_part);
    const long b_num = parseLong(b_part);
    if(a_num >= 0 && b_num >= 0)
    {
      if(a_num != b_num)
        return a_num < b_num ? -1 : 1;
    }
    else if(a_part != b_part)
      return a_part < b_part ? -1 : 1;
  }
  return 0;
}

bool OmmRepository::fetchAndParse()
{
  if(url_.empty())
  {
    Log::error("OMM: cannot fetch repository with an empty URL.");
    return false;
  }

  cpr::Session session;
  session.SetUrl(cpr::Url(url_));
  if(!user_.empty() || !password_.empty())
    session.SetAuth(cpr::Authentication(user_, password_, cpr::AuthMode::BASIC));
  const cpr::Response response = session.Get();

  if(response.error)
  {
    Log::error(std::format("OMM: failed to reach repository \"{}\": {}",
                           url_,
                           response.error.message));
    return false;
  }
  if(response.status_code != 200)
  {
    Log::error(std::format(
      "OMM: repository \"{}\" returned HTTP status {}.", url_, response.status_code));
    return false;
  }

  pugi::xml_document doc;
  const pugi::xml_parse_result parse_result = doc.load_string(response.text.c_str());
  if(!parse_result)
  {
    Log::error(std::format(
      "OMM: failed to parse descriptor from \"{}\": {}", url_, parse_result.description()));
    return false;
  }

  pugi::xml_node root = doc.first_child();
  if(!root)
  {
    Log::error(std::format("OMM: descriptor from \"{}\" is empty.", url_));
    return false;
  }

  title_ = firstValue(root, { "title", "name" });
  if(title_.empty())
    title_ = url_;

  base_url_ = firstValue(root, { "downpath", "url", "base" });

  // Package entries may live directly under the root or inside a container.
  std::vector<pugi::xml_node> containers{ root };
  for(const char* container_name : { "remotes", "packages", "mods", "entries" })
  {
    pugi::xml_node container = root.child(container_name);
    if(container)
      containers.push_back(container);
  }

  std::vector<RemotePackage> packages;
  for(const pugi::xml_node& container : containers)
  {
    for(pugi::xml_node node = container.first_child(); node; node = node.next_sibling())
    {
      if(node.type() != pugi::node_element || !isPackageElement(node.name()))
        continue;

      RemotePackage package;
      package.name = firstValue(node, { "name", "ident", "title" });
      package.version = firstValue(node, { "version", "vers" });
      package.category = firstValue(node, { "category", "cat" });
      package.description = firstValue(node, { "description", "desc" });

      // Per-package files: prefer explicit <file>/<download> children.
      for(const char* file_element : { "file", "download" })
      {
        for(pugi::xml_node file_node = node.child(file_element); file_node;
            file_node = file_node.next_sibling(file_element))
        {
          RemoteFile file;
          file.file_name = firstValue(file_node, { "name", "file" });
          file.url = firstValue(file_node, { "url", "href", "file" });
          if(file.url.empty())
            file.url = file_node.text().get();
          file.size = parseLong(firstValue(file_node, { "size", "bytes" }));
          file.checksum = firstValue(file_node, { "checksum", "md5", "hash" });
          if(!file.url.empty() || !file.file_name.empty())
            package.files.push_back(std::move(file));
        }
      }

      // Fallback: a single file/url described directly on the package element.
      if(package.files.empty())
      {
        RemoteFile file;
        file.url = firstValue(node, { "url", "href", "file", "download" });
        file.file_name = firstValue(node, { "file", "filename" });
        file.size = parseLong(firstValue(node, { "size", "bytes" }));
        file.checksum = firstValue(node, { "checksum", "md5", "hash" });
        if(!file.url.empty())
          package.files.push_back(std::move(file));
      }

      // Derive a missing file name from the URL where possible.
      for(RemoteFile& file : package.files)
      {
        if(file.file_name.empty() && !file.url.empty())
        {
          const auto slash = file.url.find_last_of('/');
          std::string tail = slash == std::string::npos ? file.url : file.url.substr(slash + 1);
          const auto query = tail.find('?');
          if(query != std::string::npos)
            tail = tail.substr(0, query);
          file.file_name = tail;
        }
      }

      if(package.name.empty() && package.files.empty())
        continue;
      if(package.name.empty())
        package.name = package.files.front().file_name;
      packages.push_back(std::move(package));
    }
  }

  packages_ = std::move(packages);
  Log::info(std::format(
    "OMM: connected to \"{}\" ({} packages).", title_, packages_.size()));
  return true;
}

std::string OmmRepository::resolveUrl(const std::string& raw_url) const
{
  if(raw_url.empty())
    return raw_url;
  // Already absolute.
  if(raw_url.find("://") != std::string::npos)
    return raw_url;

  // Resolve against an explicit base URL if present.
  std::string base = base_url_;
  if(base.empty())
  {
    // Fall back to the directory of the descriptor URL.
    const auto slash = url_.find_last_of('/');
    if(slash != std::string::npos)
      base = url_.substr(0, slash);
  }
  if(base.empty())
    return raw_url;

  if(base.back() == '/' && raw_url.front() == '/')
    return base + raw_url.substr(1);
  if(base.back() != '/' && raw_url.front() != '/')
    return base + "/" + raw_url;
  return base + raw_url;
}
