/*!
 * \file collection.h
 * \brief Header for the nexus::Collection class. Fork features #1/#2: import and
 * export of Nexus Collection manifests (collection.json).
 */

#pragma once

#include <json/json.h>
#include <string>
#include <vector>


/*!
 * \brief The nexus namespace contains structs and functions needed for accessing the NexusMods API.
 */
namespace nexus
{
/*!
 * \brief Parses and emits Nexus Collection manifests (collection.json).
 *
 * A Nexus Collection manifest describes an ordered list of mods (each identified by
 * a NexusMods domain, mod id and file id), together with install rules that constrain
 * the relative order in which mods must be applied. This class converts such a manifest
 * to and from an in-memory representation. It performs no network access: resolving the
 * referenced files to download URLs is left to nexus::Api.
 */
class Collection
{
public:
  /*!
   * \brief Describes a single mod referenced by a collection manifest.
   */
  struct Entry
  {
    /*! \brief Display name of the mod. */
    std::string name;
    /*! \brief Mod version as recorded in the manifest. */
    std::string version;
    /*! \brief NexusMods domain the mod belongs to, e.g. "skyrimspecialedition". */
    std::string domain;
    /*! \brief The mod id on NexusMods. -1 if unknown (e.g. non-Nexus source). */
    long mod_id = -1;
    /*! \brief The file id on NexusMods. -1 if unknown. */
    long file_id = -1;
    /*! \brief Optional logical file name as recorded in the manifest. */
    std::string logical_file_name;
    /*! \brief Install phase / order index. Lower phases are installed first. */
    int phase = 0;
    /*! \brief If true: the manifest marks this mod as optional. */
    bool optional = false;
    /*!
     * \brief True if this entry references a NexusMods mod (has a valid domain and mod id).
     * Entries that do not are skipped during import.
     */
    bool isNexusSource() const;
    /*!
     * \brief Reconstructs the NexusMods mod page URL for this entry.
     * \return The mod page URL, or an empty string if this is not a Nexus source.
     */
    std::string modUrl() const;
  };

  /*!
   * \brief Describes a single ordering rule between two mods.
   */
  struct Rule
  {
    /*! \brief The kind of constraint. */
    enum Type
    {
      /*! \brief The source mod must be installed before the reference mod. */
      before = 0,
      /*! \brief The source mod must be installed after the reference mod. */
      after = 1
    };

    /*! \brief Type of the constraint. */
    Type type = before;
    /*! \brief Index into Collection::entries of the constrained mod. -1 if unresolved. */
    int source_index = -1;
    /*! \brief Index into Collection::entries of the reference mod. -1 if unresolved. */
    int reference_index = -1;
  };

  /*!
   * \brief Constructs an empty collection.
   */
  Collection() = default;
  /*!
   * \brief Parses the given collection.json string into a Collection.
   * \param json_string The raw collection.json contents.
   * \throws ParseError If the string is not valid JSON.
   */
  explicit Collection(const std::string& json_string);
  /*!
   * \brief Constructs a Collection from a parsed JSON document.
   * \param json_body The parsed collection.json document.
   */
  explicit Collection(const Json::Value& json_body);

  /*!
   * \brief Parses a collection.json file from disk.
   * \param path Path to the collection.json file.
   * \return The parsed Collection.
   * \throws ParseError If the file can not be read or parsed.
   */
  static Collection fromFile(const std::string& path);
  /*!
   * \brief Builds a Collection manifest from a list of mod entries.
   *
   * This is the entry point for the export feature: given (mod_id, file_id, version, phase)
   * tuples it produces a Collection that can be serialized via toJson()/toString().
   * \param name Name of the collection.
   * \param game_domain NexusMods domain of the target game, e.g. "skyrimspecialedition".
   * \param entries The mods to include. Entries are emitted in the given order.
   * \param author Optional author name written to the manifest info block.
   * \return The constructed Collection.
   */
  static Collection fromEntries(const std::string& name,
                                const std::string& game_domain,
                                const std::vector<Entry>& entries,
                                const std::string& author = "");

  /*!
   * \brief Serializes this Collection to a collection.json JSON document.
   * \return The JSON document.
   */
  Json::Value toJson() const;
  /*!
   * \brief Serializes this Collection to a collection.json string.
   * \return The serialized manifest.
   */
  std::string toString() const;
  /*!
   * \brief Writes this Collection to a collection.json file.
   * \param path Destination path.
   * \throws ParseError If the file can not be written.
   */
  void toFile(const std::string& path) const;

  /*!
   * \brief Returns the mod entries in install order.
   * \return The entries.
   */
  const std::vector<Entry>& getEntries() const;
  /*!
   * \brief Returns the ordering rules.
   * \return The rules.
   */
  const std::vector<Rule>& getRules() const;

  /*! \brief Name of the collection. */
  std::string name;
  /*! \brief Author of the collection. */
  std::string author;
  /*! \brief Version string of the collection. */
  std::string version = "1.0.0";
  /*! \brief Free text description of the collection. */
  std::string description;
  /*! \brief NexusMods domain of the target game, e.g. "skyrimspecialedition". */
  std::string game_domain;

private:
  /*!
   * \brief Initializes all members from the given parsed JSON document.
   * \param json_body The parsed collection.json document.
   */
  void init(const Json::Value& json_body);
  /*!
   * \brief Resolves a mod reference object from a rule into an index into entries_.
   * \param reference The rule reference object from the manifest.
   * \return The matching entry index, or -1 if no entry matches.
   */
  int resolveReference(const Json::Value& reference) const;

  /*! \brief The mod entries in install order. */
  std::vector<Entry> entries_;
  /*! \brief The ordering rules. */
  std::vector<Rule> rules_;
};
}
