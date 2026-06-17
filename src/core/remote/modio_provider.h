/*!
 * \file modio_provider.h
 * \brief Header for the remote::ModioProvider class.
 *
 * Scaffold for the mod.io REST API provider.
 * Relevant upstream issues: limo-app/limo#60, limo-app/limo#209.
 *
 * mod.io API reference: https://docs.mod.io/restapiref
 *
 * Status: SCAFFOLDED.
 *   - Full request/parse structure is in place and compiles.
 *   - Requires a mod.io API key (free, obtained at https://mod.io/apikey/widget).
 *   - search(), getModInfo(), getFiles() are fully wired; getDownloadUrl()
 *     requires an OAuth token for authenticated users (falls back to the
 *     public CDN URL embedded in the file object when available).
 *
 * \note mod.io requires API key authentication even for read-only operations,
 *       so requiresApiKey() returns true and setApiKey() must be called before
 *       any request is made.
 */

#pragma once

#include "remotesource.h"
#include <json/json.h>
#include <string>
#include <vector>


namespace remote
{

/*!
 * \brief RemoteSource implementation for the mod.io repository.
 *
 * \par Community identifier
 * The \c community parameter maps to a mod.io game ID, as an integer string,
 * e.g. "2" for the mod.io sandbox game.  The game ID is visible in the mod.io
 * studio URL or can be obtained via GET /games.
 *
 * \par Mod identifier
 * \c mod_id is the mod.io mod ID (integer as string).
 *
 * \par File identifier
 * \c file_id is the mod.io modfile ID (integer as string).
 *
 * \par Authentication
 * mod.io requires an API key for all requests.  Supply it via setApiKey().
 * Download URL generation additionally requires an OAuth 2.0 bearer token for
 * mods that do not expose a public binary_url; this is not yet implemented
 * (TODO: limo-app/limo#60 OAuth flow).
 */
class ModioProvider : public RemoteSource
{
public:
  ModioProvider() = default;
  explicit ModioProvider(const std::string& api_key);

  std::string name() const override;

  bool requiresApiKey() const override { return true; }

  /*!
   * \brief Supply the mod.io API key.
   * \param key mod.io API key string (not an OAuth token).
   */
  void setApiKey(const std::string& key) override;

  /*!
   * \brief Supply a mod.io OAuth 2.0 bearer token for authenticated downloads.
   *
   * Required to resolve download URLs for subscriber-only modfiles that do not
   * expose a public binary_url.  The token is obtained out-of-band via the
   * mod.io OAuth email flow (POST /v1/oauth/emailrequest followed by
   * POST /v1/oauth/emailexchange) or from the mod.io site's access-token widget.
   *
   * \param token mod.io OAuth 2.0 access token (without the "Bearer " prefix).
   */
  void setOAuthToken(const std::string& token);

  /*!
   * \brief Search mod.io for mods matching a keyword in a game.
   *
   * Uses GET /v1/games/{game_id}/mods with the _q parameter.
   *
   * \param community mod.io game ID as string.
   * \param query     Keyword string.
   * \param max_results Maximum results (mod.io page_size limit is 100).
   * \return Matching mods as RemoteMod objects.
   */
  std::vector<RemoteMod> search(const std::string& community,
                                const std::string& query,
                                int max_results = 20) override;

  /*!
   * \brief Fetch metadata for a single mod.io mod.
   *
   * Uses GET /v1/games/{game_id}/mods/{mod_id}.
   *
   * \param community mod.io game ID as string.
   * \param mod_id    mod.io mod ID as string.
   * \return Populated RemoteMod.
   */
  RemoteMod getModInfo(const std::string& community,
                       const std::string& mod_id) override;

  /*!
   * \brief List all modfiles for a mod.io mod.
   *
   * Uses GET /v1/games/{game_id}/mods/{mod_id}/files.
   *
   * \param community mod.io game ID as string.
   * \param mod_id    mod.io mod ID as string.
   * \return One RemoteFile per modfile entry.
   */
  std::vector<RemoteFile> getFiles(const std::string& community,
                                   const std::string& mod_id) override;

  /*!
   * \brief Resolve the download URL for a specific mod.io file.
   *
   * For publicly accessible mods the binary_url field in the modfile object
   * already contains the CDN URL, so no extra round-trip is needed.
   *
   * For mods requiring subscriber authentication a separate authenticated
   * endpoint is needed — this is not yet implemented.
   * TODO(limo-app/limo#60): implement OAuth token flow for restricted mods.
   *
   * \param community mod.io game ID as string.
   * \param mod_id    mod.io mod ID as string.
   * \param file_id   mod.io modfile ID as string.
   * \return Direct CDN download URL.
   */
  std::string getDownloadUrl(const std::string& community,
                             const std::string& mod_id,
                             const std::string& file_id) override;

private:
  // Grants the unit tests access to the static JSON parsers below without exposing
  // them publicly or coupling them to the (network-only) public methods.
  friend struct RemoteProviderTestAccess;

  /*! \brief mod.io v1 API base URL. */
  static constexpr const char* BASE_URL = "https://api.mod.io/v1";
  /*! \brief User-Agent header value sent with every request. */
  static constexpr const char* USER_AGENT = "limo-mod-manager/1.2.2 (limo-app/limo#60)";

  /*! \brief mod.io API key. Set via setApiKey(). */
  std::string api_key_;

  /*! \brief Optional mod.io OAuth 2.0 bearer token. Set via setOAuthToken(). */
  std::string oauth_token_;

  /*!
   * \brief Assert that an API key has been set; throw if not.
   * \throws std::runtime_error if api_key_ is empty.
   */
  void requireKey() const;

  /*!
   * \brief Resolve the authenticated CDN download URL for a modfile.
   *
   * Uses GET /v1/games/{game_id}/mods/{mod_id}/files/{file_id}/download with an
   * Authorization: Bearer header.  mod.io responds with a 302 redirect to the
   * actual (time-limited) CDN URL, which is returned without following it.
   *
   * \param community mod.io game ID as string.
   * \param mod_id    mod.io mod ID as string.
   * \param file_id   mod.io modfile ID as string.
   * \return Resolved CDN download URL.
   * \throws std::runtime_error if no OAuth token is configured or the request
   *         fails / does not yield a redirect location.
   */
  std::string getAuthenticatedDownloadUrl(const std::string& community,
                                          const std::string& mod_id,
                                          const std::string& file_id) const;

  /*!
   * \brief Convert a mod.io mod JSON object to a RemoteMod.
   * \param obj JSON Value representing one mod entry.
   * \return Populated RemoteMod.
   */
  static RemoteMod modObjectToRemoteMod(const Json::Value& obj);

  /*!
   * \brief Convert a mod.io modfile JSON object to a RemoteFile.
   * \param obj JSON Value representing one modfile entry.
   * \return Populated RemoteFile.
   */
  static RemoteFile modfileToRemoteFile(const Json::Value& obj);
};

} // namespace remote
