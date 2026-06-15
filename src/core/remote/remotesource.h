/*!
 * \file remotesource.h
 * \brief Interface for provider-neutral remote mod source access.
 *
 * Implements the provider abstraction requested in limo-app/limo#60,
 * related to limo-app/limo#2, #4, and #209.
 *
 * Each provider (Thunderstore, Gamebanana, mod.io, ...) derives from
 * RemoteSource and implements the four virtual operations:
 *   search        — keyword search within a community/game namespace
 *   getModInfo    — fetch summary metadata for one mod
 *   getFiles      — list downloadable file entries for one mod
 *   getDownloadUrl — resolve the final CDN URL for a specific file
 *
 * Callers use the provider-neutral structs (RemoteMod, RemoteFile) so
 * UI code does not need to know which backend is active.
 */

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>


namespace remote
{

/*!
 * \brief Provider-neutral description of a single mod.
 *
 * Fields map to the lowest-common-denominator across Thunderstore,
 * Gamebanana, and mod.io.  Provider-specific extras live in \c extra_json.
 */
struct RemoteMod
{
  /*! \brief Unique identifier within the provider (may be a slug or integer). */
  std::string id;
  /*! \brief Human-readable mod name. */
  std::string name;
  /*! \brief Short summary / description. */
  std::string summary;
  /*! \brief Author / team name. */
  std::string author;
  /*! \brief Most-recent version string, e.g. "1.2.3". */
  std::string version;
  /*! \brief URL of the mod page on the provider's website. */
  std::string page_url;
  /*! \brief URL of the primary icon/thumbnail image, or empty. */
  std::string icon_url;
  /*! \brief Total download count across all versions (0 if unavailable). */
  long total_downloads = 0;
  /*! \brief Provider-specific raw JSON, preserved for callers that need it. */
  std::string extra_json;
};

/*!
 * \brief Provider-neutral description of one downloadable file entry.
 */
struct RemoteFile
{
  /*! \brief Unique identifier for this file within the provider. */
  std::string id;
  /*! \brief Display name of the file / release. */
  std::string name;
  /*! \brief Version string for this specific file. */
  std::string version;
  /*! \brief Direct download URL if known at list time, or empty. */
  std::string download_url;
  /*! \brief File size in bytes (0 if unavailable). */
  long size_bytes = 0;
  /*! \brief ISO-8601 upload timestamp or empty. */
  std::string uploaded_at;
  /*! \brief Short description / changelog for this release. */
  std::string description;
};

/*!
 * \brief Abstract base class for remote mod-source providers.
 *
 * Derive from this class to implement a new source (Thunderstore, Gamebanana,
 * mod.io, etc.).  All network I/O should use the vendored \c cpr library so it
 * remains consistent with the NexusMods integration in src/core/nexus/.
 *
 * Thread-safety: implementations are not required to be thread-safe.
 * Callers that invoke these from worker threads must provide their own locking.
 */
class RemoteSource
{
public:
  virtual ~RemoteSource() = default;

  /*!
   * \brief Human-readable provider name, e.g. "Thunderstore" or "Gamebanana".
   * \return Provider display name.
   */
  virtual std::string name() const = 0;

  /*!
   * \brief Search for mods matching a keyword within a game namespace.
   *
   * \param community Provider-specific community / game identifier.
   *        For Thunderstore this is a community slug (e.g. "ror2").
   *        For Gamebanana this is a game ID string.
   *        For mod.io this is a game ID string.
   * \param query       Keyword(s) to search for.
   * \param max_results Maximum number of results to return (hint only).
   * \return Vector of matching mods, ordered by provider relevance.
   * \throws std::runtime_error on HTTP or parse failure.
   */
  virtual std::vector<RemoteMod> search(const std::string& community,
                                        const std::string& query,
                                        int max_results = 20) = 0;

  /*!
   * \brief Fetch full metadata for a single mod.
   *
   * \param community Provider-specific community / game identifier (same as search).
   * \param mod_id    Provider-specific mod identifier (from RemoteMod::id).
   * \return Populated RemoteMod struct.
   * \throws std::runtime_error on HTTP or parse failure.
   */
  virtual RemoteMod getModInfo(const std::string& community,
                               const std::string& mod_id) = 0;

  /*!
   * \brief List all downloadable file entries for a mod.
   *
   * \param community Provider-specific community / game identifier.
   * \param mod_id    Provider-specific mod identifier.
   * \return Vector of RemoteFile entries for the mod's releases.
   * \throws std::runtime_error on HTTP or parse failure.
   */
  virtual std::vector<RemoteFile> getFiles(const std::string& community,
                                           const std::string& mod_id) = 0;

  /*!
   * \brief Resolve the final CDN download URL for a specific file.
   *
   * For providers where getFiles already populates RemoteFile::download_url
   * this may simply return that value.  For providers that require a
   * separate API call (e.g. mod.io with an auth token) this performs the
   * extra round-trip.
   *
   * \param community Provider-specific community / game identifier.
   * \param mod_id    Provider-specific mod identifier.
   * \param file_id   Provider-specific file identifier (from RemoteFile::id).
   * \return Direct download URL string.
   * \throws std::runtime_error on HTTP or parse failure, or if the provider
   *         requires credentials that have not been supplied.
   */
  virtual std::string getDownloadUrl(const std::string& community,
                                     const std::string& mod_id,
                                     const std::string& file_id) = 0;

  /*!
   * \brief Check whether the provider requires an API key / token.
   * \return True if credentials are needed to make any request.
   */
  virtual bool requiresApiKey() const { return false; }

  /*!
   * \brief Supply an API key or OAuth token to the provider.
   *
   * Only meaningful when requiresApiKey() returns true.
   * \param key API key or bearer token string.
   */
  virtual void setApiKey(const std::string& /*key*/) {}
};

} // namespace remote
