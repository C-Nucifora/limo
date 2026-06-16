/*!
 * \file wabbajackmodlist.cpp
 * \brief Implementation of the Wabbajack modlist parser.
 *
 * fork #197
 */

#include "wabbajackmodlist.h"
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <cctype>
#include <json/json.h>


namespace
{
/*! \brief Upper bound on the size of the in-archive "modlist" JSON entry. */
constexpr unsigned long long MAX_MODLIST_BYTES = 256ull * 1024ull * 1024ull;

/*! \brief Returns a lowercased copy of the given string. */
std::string toLower(const std::string& input)
{
  std::string result = input;
  std::transform(result.begin(),
                 result.end(),
                 result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

/*! \brief Returns true if needle is contained in haystack (case-insensitive). */
bool containsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
  return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

/*!
 * \brief Reads the named entry from the archive into a string.
 * \param path Path to the archive.
 * \param wanted_entry Case-insensitive entry name to extract.
 * \param out_data Receives the entry contents on success.
 * \param error Receives an error message on failure.
 * \return True if the entry was found and read.
 */
bool readArchiveEntry(const std::filesystem::path& path,
                      const std::string& wanted_entry,
                      std::string& out_data,
                      std::string& error)
{
  struct archive* source = archive_read_new();
  if(source == nullptr)
  {
    error = "Failed to allocate archive reader.";
    return false;
  }
  archive_read_support_filter_all(source);
  archive_read_support_format_all(source);
  if(archive_read_open_filename(source, path.string().c_str(), 10240) != ARCHIVE_OK)
  {
    error = std::string("Failed to open archive: ") + archive_error_string(source);
    archive_read_free(source);
    return false;
  }

  const std::string wanted = toLower(wanted_entry);
  bool found = false;
  struct archive_entry* entry = nullptr;
  while(archive_read_next_header(source, &entry) == ARCHIVE_OK)
  {
    const char* name = archive_entry_pathname(entry);
    if(name == nullptr)
      continue;
    if(toLower(name) != wanted)
      continue;
    found = true;
    out_data.clear();
    const void* buff = nullptr;
    size_t size = 0;
    int64_t offset = 0;
    int return_code = ARCHIVE_OK;
    while((return_code = archive_read_data_block(source, &buff, &size, &offset)) == ARCHIVE_OK)
    {
      if(out_data.size() + size > MAX_MODLIST_BYTES)
      {
        error = "The 'modlist' entry is unreasonably large.";
        found = false;
        break;
      }
      out_data.append(static_cast<const char*>(buff), size);
    }
    if(found && return_code != ARCHIVE_EOF)
    {
      error = "Failed to read the 'modlist' entry.";
      found = false;
    }
    break;
  }

  archive_read_free(source);
  if(!found && error.empty())
    error = "The archive does not contain a 'modlist' entry.";
  return found;
}

/*! \brief Reads a JSON string field, falling back to alternative key names. */
std::string readString(const Json::Value& value, std::initializer_list<const char*> keys)
{
  for(const char* key : keys)
  {
    if(value.isMember(key) && value[key].isString())
      return value[key].asString();
  }
  return {};
}

/*! \brief Reads a JSON integer field, falling back to alternative key names. */
long long readInt(const Json::Value& value,
                  std::initializer_list<const char*> keys,
                  long long fallback)
{
  for(const char* key : keys)
  {
    if(!value.isMember(key))
      continue;
    const Json::Value& field = value[key];
    if(field.isIntegral())
      return field.asInt64();
    if(field.isString())
    {
      try
      {
        return std::stoll(field.asString());
      }
      catch(...)
      {
        // Ignore unparseable strings and fall through to the fallback.
      }
    }
  }
  return fallback;
}
} // namespace

WabbajackModlist parseWabbajack(const std::filesystem::path& path)
{
  WabbajackModlist modlist;

  std::string raw;
  if(!readArchiveEntry(path, "modlist", raw, modlist.error))
    return modlist;

  Json::Value root;
  Json::CharReaderBuilder builder;
  std::string parse_errors;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  if(!reader->parse(raw.data(), raw.data() + raw.size(), &root, &parse_errors))
  {
    modlist.error = "Failed to parse modlist JSON: " + parse_errors;
    return modlist;
  }
  if(!root.isObject())
  {
    modlist.error = "Modlist JSON root is not an object.";
    return modlist;
  }

  modlist.name = readString(root, { "Name" });
  modlist.author = readString(root, { "Author" });
  modlist.game_type = readString(root, { "GameType" });
  modlist.version = readString(root, { "Version" });

  if(root.isMember("Directives") && root["Directives"].isArray())
    modlist.directive_count = static_cast<int>(root["Directives"].size());

  const Json::Value& archives = root["Archives"];
  if(archives.isArray())
  {
    for(const auto& entry : archives)
    {
      if(!entry.isObject())
        continue;
      WabbajackArchive archive;
      archive.name = readString(entry, { "Name" });
      const long long raw_size = readInt(entry, { "Size" }, 0);
      archive.size = raw_size > 0 ? static_cast<unsigned long long>(raw_size) : 0ull;

      const Json::Value& state = entry["State"];
      if(state.isObject())
      {
        const std::string type = readString(state, { "$type", "Type" });
        if(containsCaseInsensitive(type, "nexus"))
        {
          archive.is_nexus = true;
          archive.game_name = readString(state, { "GameName", "Game" });
          archive.mod_id = readInt(state, { "ModID", "ModId" }, -1);
          archive.file_id = readInt(state, { "FileID", "FileId" }, -1);
          archive.version = readString(state, { "Version" });
        }
      }
      modlist.archives.push_back(std::move(archive));
    }
  }

  modlist.ok = true;
  return modlist;
}
