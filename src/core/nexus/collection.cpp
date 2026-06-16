#include "collection.h"
#include "../log.h"
#include "../parseerror.h"
#include <fstream>
#include <sstream>

using namespace nexus;


bool Collection::Entry::isNexusSource() const
{
  return !domain.empty() && mod_id > 0;
}

std::string Collection::Entry::modUrl() const
{
  if(!isNexusSource())
    return "";
  return "https://www.nexusmods.com/" + domain + "/mods/" + std::to_string(mod_id);
}

Collection::Collection(const std::string& json_string)
{
  Json::Value json_body;
  Json::Reader reader;
  if(!reader.parse(json_string.c_str(), json_body))
    throw ParseError("Failed to parse collection manifest.");
  init(json_body);
}

Collection::Collection(const Json::Value& json_body)
{
  init(json_body);
}

Collection Collection::fromFile(const std::string& path)
{
  std::ifstream file(path, std::ios::binary);
  if(!file.is_open())
    throw ParseError("Could not open collection file '" + path + "'.");
  std::stringstream buffer;
  buffer << file.rdbuf();
  return Collection(buffer.str());
}

void Collection::init(const Json::Value& json_body)
{
  const Json::Value& info = json_body["info"];
  name = info["name"].asString();
  author = info["author"].asString();
  if(info.isMember("version"))
    version = info["version"].asString();
  description = info["description"].asString();
  game_domain = info["domainName"].asString();
  // Some manifests record the game domain at the top level instead.
  if(game_domain.empty() && json_body.isMember("gameId"))
    game_domain = json_body["gameId"].asString();
  // The game domain is later turned into a nexusmods.com URL; restrict it to an
  // allowlist of alphanumeric characters so a hostile manifest can not inject path
  // segments or other URL components. Reject anything that does not match [a-zA-Z0-9]+.
  bool valid_domain = !game_domain.empty();
  for(const char c : game_domain)
  {
    if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
    {
      valid_domain = false;
      break;
    }
  }
  if(!valid_domain)
    game_domain.clear();

  // Parse the mod list, preserving manifest order as the install order.
  const Json::Value& mods = json_body["mods"];
  if(mods.isArray())
  {
    entries_.reserve(mods.size());
    int index = 0;
    for(const Json::Value& mod : mods)
    {
      Entry entry;
      entry.name = mod["name"].asString();
      entry.version = mod["version"].asString();
      entry.optional = mod.get("optional", false).asBool();
      // Phase defaults to the position in the list, but an explicit phase wins.
      entry.phase = mod.get("phase", index).asInt();

      const Json::Value& source = mod["source"];
      // The game domain is usually carried only on the info block; fall back to it.
      entry.domain = game_domain;
      if(source.isMember("modId"))
        entry.mod_id = source["modId"].asInt64();
      if(source.isMember("fileId"))
        entry.file_id = source["fileId"].asInt64();
      entry.logical_file_name = source["logicalFilename"].asString();
      if(entry.logical_file_name.empty())
        entry.logical_file_name = source["fileExpression"].asString();

      entries_.push_back(entry);
      index++;
    }
  }

  // Parse ordering rules; references are resolved to entry indices where possible.
  const Json::Value& rules = json_body["modRules"];
  if(rules.isArray())
  {
    for(const Json::Value& rule : rules)
    {
      Rule parsed;
      const std::string type = rule["type"].asString();
      if(type == "after")
        parsed.type = Rule::after;
      else
        parsed.type = Rule::before;
      parsed.source_index = resolveReference(rule["source"]);
      parsed.reference_index = resolveReference(rule["reference"]);
      // Skip rules referencing mods not present in the manifest.
      if(parsed.source_index >= 0 && parsed.reference_index >= 0)
        rules_.push_back(parsed);
    }
  }

  Log::debug("Parsed collection '" + name + "' with " + std::to_string(entries_.size()) +
             " mod(s) and " + std::to_string(rules_.size()) + " rule(s).");
}

int Collection::resolveReference(const Json::Value& reference) const
{
  if(!reference.isObject())
    return -1;
  // Nexus collection references identify a mod by its file id or, failing that, by a
  // logical file name. Match against the parsed entries.
  long file_id = reference.isMember("fileId") ? reference["fileId"].asInt64() : -1;
  const std::string logical = reference["logicalFileName"].asString();
  for(size_t i = 0; i < entries_.size(); i++)
  {
    if(file_id > 0 && entries_[i].file_id == file_id)
      return static_cast<int>(i);
    if(!logical.empty() && entries_[i].logical_file_name == logical)
      return static_cast<int>(i);
  }
  return -1;
}

Collection Collection::fromEntries(const std::string& name,
                                   const std::string& game_domain,
                                   const std::vector<Entry>& entries,
                                   const std::string& author)
{
  Collection collection;
  collection.name = name;
  collection.game_domain = game_domain;
  collection.author = author;
  collection.entries_ = entries;
  return collection;
}

Json::Value Collection::toJson() const
{
  Json::Value root;

  Json::Value info;
  info["name"] = name;
  info["author"] = author;
  info["version"] = version;
  info["description"] = description;
  info["domainName"] = game_domain;
  root["info"] = info;
  root["gameId"] = game_domain;

  Json::Value mods(Json::arrayValue);
  for(const Entry& entry : entries_)
  {
    Json::Value mod;
    mod["name"] = entry.name;
    mod["version"] = entry.version;
    mod["optional"] = entry.optional;
    mod["phase"] = entry.phase;

    Json::Value source;
    source["type"] = "nexus";
    source["modId"] = static_cast<Json::Int64>(entry.mod_id);
    source["fileId"] = static_cast<Json::Int64>(entry.file_id);
    if(!entry.logical_file_name.empty())
      source["logicalFilename"] = entry.logical_file_name;
    mod["source"] = source;

    mods.append(mod);
  }
  root["mods"] = mods;

  Json::Value rules(Json::arrayValue);
  for(const Rule& rule : rules_)
  {
    if(rule.source_index < 0 || rule.source_index >= static_cast<int>(entries_.size()) ||
       rule.reference_index < 0 || rule.reference_index >= static_cast<int>(entries_.size()))
      continue;
    Json::Value json_rule;
    json_rule["type"] = rule.type == Rule::after ? "after" : "before";

    Json::Value source;
    source["fileId"] = static_cast<Json::Int64>(entries_[rule.source_index].file_id);
    source["logicalFileName"] = entries_[rule.source_index].logical_file_name;
    json_rule["source"] = source;

    Json::Value reference;
    reference["fileId"] = static_cast<Json::Int64>(entries_[rule.reference_index].file_id);
    reference["logicalFileName"] = entries_[rule.reference_index].logical_file_name;
    json_rule["reference"] = reference;

    rules.append(json_rule);
  }
  root["modRules"] = rules;

  return root;
}

std::string Collection::toString() const
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  return Json::writeString(builder, toJson());
}

void Collection::toFile(const std::string& path) const
{
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if(!file.is_open())
    throw ParseError("Could not write collection file '" + path + "'.");
  file << toString();
  if(!file.good())
    throw ParseError("Error while writing collection file '" + path + "'.");
}

const std::vector<Collection::Entry>& Collection::getEntries() const
{
  return entries_;
}

const std::vector<Collection::Rule>& Collection::getRules() const
{
  return rules_;
}
