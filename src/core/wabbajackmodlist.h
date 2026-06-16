/*!
 * \file wabbajackmodlist.h
 * \brief Header for the WabbajackModlist parser.
 *
 * fork #197: Parses a Wabbajack ".wabbajack" file (a ZIP archive containing a
 * JSON entry named "modlist") and extracts the metadata plus the list of
 * source archives. Only the download-orchestration data is extracted here;
 * replaying the install directives is out of scope.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>


/*!
 * \brief Represents a single source archive referenced by a Wabbajack modlist.
 *
 * fork #197
 */
struct WabbajackArchive
{
  /*! \brief Display name of the archive. */
  std::string name;
  /*! \brief NexusMods game domain name (only valid if is_nexus is true). */
  std::string game_name;
  /*! \brief NexusMods mod id, or -1 if unknown / non-Nexus. */
  long long mod_id = -1;
  /*! \brief NexusMods file id, or -1 if unknown / non-Nexus. */
  long long file_id = -1;
  /*! \brief Mod version string, if present. */
  std::string version;
  /*! \brief Archive size in bytes. */
  unsigned long long size = 0;
  /*! \brief True if this archive is sourced from NexusMods. */
  bool is_nexus = false;
};

/*!
 * \brief Represents a parsed Wabbajack modlist.
 *
 * fork #197
 */
struct WabbajackModlist
{
  /*! \brief Name of the modlist. */
  std::string name;
  /*! \brief Author of the modlist. */
  std::string author;
  /*! \brief Target game type. */
  std::string game_type;
  /*! \brief Modlist version. */
  std::string version;
  /*! \brief All source archives referenced by the modlist. */
  std::vector<WabbajackArchive> archives;
  /*! \brief Number of install directives (informational only). */
  int directive_count = 0;
  /*! \brief True if parsing succeeded. */
  bool ok = false;
  /*! \brief Human readable error message if ok is false. */
  std::string error;
};

/*!
 * \brief Parses a Wabbajack ".wabbajack" file.
 *
 * fork #197: Opens the archive with libarchive, locates the "modlist" entry,
 * reads it fully into memory, parses the JSON and fills a WabbajackModlist.
 * Never throws; on any failure the returned struct has ok == false and a
 * populated error message.
 * \param path Path to the ".wabbajack" file.
 * \return The parsed modlist.
 */
WabbajackModlist parseWabbajack(const std::filesystem::path& path);
