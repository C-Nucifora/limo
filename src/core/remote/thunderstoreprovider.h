/*!
 * \file thunderstoreprovider.h
 * \brief Header for the remote::ThunderstoreProvider class.
 *
 * Full implementation of the Thunderstore public API.
 * No API key is required — all endpoints are public.
 *
 * Relevant upstream issues: limo-app/limo#60, limo-app/limo#2.
 *
 * API reference: https://thunderstore.io/api/docs/
 */

#pragma once

#include "remotesource.h"
#include <json/json.h>
#include <string>
#include <vector>


namespace remote
{

/*!
 * \brief RemoteSource implementation for the Thunderstore mod repository.
 *
 * Thunderstore hosts mods organised into "communities" (one per game).
 * All REST endpoints used here are listed at https://thunderstore.io/api/docs/
 * and require no authentication for read operations.
 *
 * The \c community parameter in every method maps directly to the Thunderstore
 * community slug visible in the website URL, e.g.:
 *   https://thunderstore.io/c/ror2/  ->  community = "ror2"
 *   https://thunderstore.io/c/valheim/ -> community = "valheim"
 *
 * Package identifiers on Thunderstore follow the form
 *   "<namespace>-<package_name>"  (e.g. "BepInEx-BepInExPack")
 * which is what this provider stores in RemoteMod::id and expects in
 * mod_id / file_id parameters.
 */
class ThunderstoreProvider : public RemoteSource
{
public:
  ThunderstoreProvider() = default;

  std::string name() const override;

  /*!
   * \brief Search Thunderstore packages in a community by keyword.
   *
   * Calls GET /api/experimental/community/{community}/package/?q={query}
   * which returns paginated JSON.  Only the first page (up to max_results
   * entries) is fetched.
   *
   * \param community Thunderstore community slug (e.g. "ror2").
   * \param query     Keyword(s) to match against name/description.
   * \param max_results Maximum entries to return (capped to page size).
   * \return Matching packages as RemoteMod objects.
   */
  std::vector<RemoteMod> search(const std::string& community,
                                const std::string& query,
                                int max_results = 20) override;

  /*!
   * \brief Fetch full metadata for a Thunderstore package.
   *
   * Calls GET /api/experimental/community/{community}/package/{namespace}/{name}/
   * where namespace and name are derived by splitting mod_id on '-'.
   *
   * \param community Thunderstore community slug.
   * \param mod_id    Package identifier in "namespace-name" form.
   * \return Populated RemoteMod.
   */
  RemoteMod getModInfo(const std::string& community,
                       const std::string& mod_id) override;

  /*!
   * \brief List all versions of a Thunderstore package.
   *
   * Reuses the package detail response from getModInfo; each version
   * maps to a RemoteFile with download_url already populated.
   *
   * \param community Thunderstore community slug.
   * \param mod_id    Package identifier in "namespace-name" form.
   * \return One RemoteFile per published version, newest first.
   */
  std::vector<RemoteFile> getFiles(const std::string& community,
                                   const std::string& mod_id) override;

  /*!
   * \brief Return the download URL for a specific package version.
   *
   * For Thunderstore the URL is embedded in the version object returned
   * by the package-detail endpoint, so this calls getFiles and returns
   * the URL for the matching version_uuid (file_id).
   *
   * \param community Thunderstore community slug.
   * \param mod_id    Package identifier in "namespace-name" form.
   * \param file_id   Version UUID string (from RemoteFile::id).
   * \return Direct CDN download URL for the .zip archive.
   */
  std::string getDownloadUrl(const std::string& community,
                             const std::string& mod_id,
                             const std::string& file_id) override;

  bool requiresApiKey() const override { return false; }

private:
  /*! \brief Base URL for all Thunderstore API calls. */
  static constexpr const char* BASE_URL = "https://thunderstore.io";
  /*! \brief User-Agent header value sent with every request. */
  static constexpr const char* USER_AGENT = "limo-mod-manager/1.2.2 (limo-app/limo#60)";

  /*!
   * \brief Split a "namespace-name" mod_id into its two components.
   *
   * Thunderstore package IDs are "<namespace>-<name>".  The namespace itself
   * may not contain hyphens, but the name can, so only the first hyphen is
   * used as the delimiter.
   *
   * \param mod_id "namespace-name" string.
   * \return Pair of {namespace, name}, or throws if the string is malformed.
   */
  static std::pair<std::string, std::string> splitModId(const std::string& mod_id);

  /*!
   * \brief Convert a single Thunderstore package JSON object to RemoteMod.
   * \param pkg JSON Value representing one package.
   * \return Populated RemoteMod.
   */
  static RemoteMod packageToMod(const Json::Value& pkg);

  /*!
   * \brief Convert a single Thunderstore version JSON object to RemoteFile.
   * \param ver JSON Value representing one version entry.
   * \return Populated RemoteFile.
   */
  static RemoteFile versionToFile(const Json::Value& ver);
};

} // namespace remote
