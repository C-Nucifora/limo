/*!
 * \file Api.h
 * \brief Header for the nexus::Api class.
 */

#pragma once

#include "file.h"
#include "mod.h"
#include "../importmodinfo.h"
#include <cpr/cpr.h>
#include <string>
#include <regex>


/*!
 * \brief The nexus namespace contains structs and functions needed for accessing the NexusMods API.
 */
namespace nexus
{
/*!
 * \brief Contains all data for a mod available through the NexusMods api.
 */
struct Page
{
  /*! \brief URL of the mod page on NexusMods. */
  std::string url;
  /*! \brief Contains an overview of of the mod page, like a description and summary. */
  Mod mod;
  /*! \brief For every Version of the mod: A vector of changes in that version. */
  std::vector<std::pair<std::string, std::vector<std::string>>> changelog;
  /*! \brief Contains data on all available files for the mod. */
  std::vector<File> files;
};

/*!
 * \brief Hosting platforms for mods distributed through git release pages.
 */
enum class GitPlatform
{
  github,
  gitlab
};

/*!
 * \brief Contains data for the latest release of a mod hosted on a git platform.
 */
struct GitRelease
{
  /*! \brief The platform hosting the release. */
  GitPlatform platform;
  /*! \brief The release version, taken from the release's tag name. */
  std::string version;
  /*! \brief Timestamp for when the release was published, as a Unix time. 0 if unknown. */
  std::time_t published_time = 0;
};

/*!
 * \brief Sort orders supported by nexus::Api::searchMods.
 */
enum class SortOrder
{
  /*! \brief Sort by number of endorsements, descending. */
  endorsements,
  /*! \brief Sort by total number of downloads, descending. */
  downloads,
  /*! \brief Sort by upload/update time, most recent first. */
  recent
};

/*!
 * \brief A single result returned by nexus::Api::searchMods.
 *
 * Wraps the existing nexus::Mod (which already carries name, summary, mod_id,
 * endorsement_count, mod_downloads and picture_url) and adds the NexusMods
 * domain the mod belongs to so callers can build mod page URLs.
 */
struct SearchResult
{
  /*! \brief NexusMods domain (game) the mod belongs to. */
  std::string domain_name;
  /*! \brief Mod data as returned by the API. */
  Mod mod;
};

/*!
 * \brief Provides functions for accessing the NexusMods API.
 */
class Api
{
public:
  /*! \brief This is an abstract class, so the constructor is deleted. */
  Api() = delete;

  /*!
   * \brief Sets the API key to use for all operations.
   * \param api_key The new API key.
   */
  static void setApiKey(const std::string& api_key);
  /*!
   * \brief Checks if this class has been initialized with an API key.
   * Does NOT check if the key works.
   * \return True if an API key exists.
   */
  static bool isInitialized();
  /*!
   * \brief Fetches data for the mod accessible by the given NexusMods URL.
   * \param mod_url URL to the mod on NexusMods.
   * \return A Mod object containing all received data.
   */
  static Mod getMod(const std::string& mod_url);
  /*!
   * \brief Fetches data for the mod specified by the NexusMods domain and mod id.
   * \param domain_name The NexusMods domain containing the mod.
   * \param mod_id Target mod id.
   * \return A Mod object containing all received data.
   */
  static Mod getMod(const std::string& domain_name, long mod_id);
  /*!
   * \brief Tracks the mod for the NexusMods account belonging to the API key.
   * \param mod_url URL to the mod on NexusMods.
   */
  static void trackMod(const std::string& mod_url);
  /*!
   * \brief Tracks the mod for the NexusMods account belonging to the API key.
   * \param mod_url URL to the mod on NexusMods.
   */
  static void untrackMod(const std::string& mod_url);
  /*!
   * \brief Endorses or abstains from endorsing the given mod for the account belonging to the
   * API key.
   *
   * Endorsing requires the mod's version to be sent to NexusMods. Abstaining does not.
   *
   * \param domain The NexusMods domain (game) containing the mod.
   * \param mod_id Target mod id.
   * \param endorse If true: endorse the mod. If false: abstain from endorsing it.
   * \param mod_version The mod's version. Required by NexusMods when endorsing, ignored when
   * abstaining.
   * \return True if the operation succeeded, false otherwise.
   */
  static bool endorseMod(const std::string& domain,
                         long mod_id,
                         bool endorse,
                         const std::string& mod_version = "");
  /*!
   * \brief Tracks or untracks the given mod for the account belonging to the API key.
   * \param domain The NexusMods domain (game) containing the mod.
   * \param mod_id Target mod id.
   * \param track If true: track the mod. If false: untrack it.
   * \return True if the operation succeeded, false otherwise.
   */
  static bool trackMod(const std::string& domain, long mod_id, bool track);
  /*!
   * \brief Fetches data for all mods tracked by the account belonging to the API key.
   * \return A vector of Mod objects with the received data.
   */
  static std::vector<Mod> getTrackedMods();
  /*!
   * \brief Fetches the domain/mod id pairs of all mods tracked by the account belonging to the
   * API key, without resolving full mod data.
   * \return A vector of (domain_name, mod_id) pairs. Empty on failure.
   */
  static std::vector<std::pair<std::string, long>> getTrackedModIds();
  /*!
   * \brief Fetches data for all available files for the given mod.
   * \param mod_url URL to the mod on NexusMods.
   * \return A vector of File objects containing the received data.
   */
  static std::vector<File> getModFiles(const std::string& mod_url);
  /*!
   * \brief Generates a download URL for the given mod file. This only works for premium accounts.
   * \param mod_url URL to the mod on NexusMods.
   * \param file_id Id of the file for which a link is to be generated.
   * \return The download URL.
   */
  static std::string getDownloadUrl(const std::string& mod_url, long file_id);
  /*!
   * \brief Generates a download URL from the given nxm Url.
   * \param nxm_url The nxm Url used. This is usually generated through the NexusMods website.
   * \return The download URL.
   */
  static std::string getDownloadUrl(const std::string& nxm_url);
  /*!
   * \brief Fetches changelogs for the given mod.
   * \param mod_url URL to the mod on NexusMods.
   * \return For every Version of the mod: A vector of changes in that version.
   */
  static std::vector<std::pair<std::string, std::vector<std::string>>> getChangelogs(
    const std::string& mod_url);
  /*!
   * \brief Fetches changelogs for the mod specified by the NexusMods domain and mod id.
   *
   * Uses the \c /games/<domain>/mods/<id>/changelogs.json endpoint. Unlike getChangelogs,
   * this method does not throw on network or parse failures: it logs the error and returns
   * an empty vector instead, making it suitable for direct use from UI code.
   *
   * \param domain_name The NexusMods domain containing the mod.
   * \param mod_id Target mod id.
   * \return For every Version of the mod: A vector of changes in that version, ordered by
   * descending version number. Empty on failure.
   */
  static std::vector<std::pair<std::string, std::vector<std::string>>> getModChangelogs(
    const std::string& domain_name, long mod_id);
  /*!
   * \brief Checks if the given URL is a valid NexusMods mod page URL.
   * Only verifies if the URL is semantically correct, not if the target exists.
   * \param url URL to check.
   * \return True if the URL points to a NexusMods page.
   */
  static bool modUrlIsValid(const std::string& url);
  /*!
   * \brief Fetches data to fill a Page object for the given mod.
   * \param mod_url URL to the mod on NexusMods.
   * \return The generated Page object.
   */
  static Page getNexusPage(const std::string& mod_url);
  /*!
   * \brief Checks if the NexusMods API can be accessed with the given API key.
   * \param api_key API key to validate.
   * \return If the key works: The account name and a bool indicating if the account is premium.
   * Else: An empty std::optional.
   */
  static std::optional<std::pair<std::string, bool>> validateKey(const std::string& api_key);
  /*!
   * \brief Generates a NexusMods mod page URL from the given nxm URL.
   * \param nxm_url The nxm Url used. This is usually generated through the NexusMods website.
   * \return The NexusMods mod page URL.
   */
  static std::string getNexusPageUrl(const std::string& nxm_url);
  /*!
   * \brief Getter for the API key.
   * \return The API key.
   */
  static std::string getApiKey();
  /*!
   * \brief Extracts the NexusMods domain and mod id from the given mod page URL.
   * \param url URL to the mod on NexusMods.
   * \return If the given URL is valid: The domain and mod id. Else an empty std::optional.
   */
  static std::optional<std::pair<std::string, int>> extractDomainAndModId(
    const std::string& mod_url);
  /*!
   * \brief Initializes remote members of the given ImportModInfo.
   *
   * Uses data retreived for the mod associated with the ImportModInfo::remote_source member.
   * If remote_source is not valid, uses ImportModInfo::remote_download_url instead.
   *
   * \param info Mod info to initialize.
   * \return True if initialization was successful.
   */
  static bool initModInfo(ImportModInfo& info);
  /*!
   * \brief Checks whether the given string is a valid NexusMods nxm URL.
   * \param nxm_url String to check.
   * \return A regex match object for the string containing a group for every datum in the
   * URL. If the URL is invalid: An empty optional.
   */
  static std::optional<std::smatch> nxmUrlIsValid(const std::string& nxm_url);
  /*!
   * \brief Checks if the given URL points to a GitHub or GitLab repository and, if so,
   * extracts the owner and repository name.
   *
   * Recognizes URLs of the form \c https://github.com/<owner>/<repo> and
   * \c https://gitlab.com/<owner>/<repo> (with or without a trailing \c .git or path/query).
   *
   * \param url URL to check.
   * \return If the URL is a valid git repo URL: The hosting platform together with the
   * owner and repository name. Else: An empty std::optional.
   */
  static std::optional<std::pair<GitPlatform, std::pair<std::string, std::string>>> gitRepoFromUrl(
    const std::string& url);
  /*!
   * \brief Fetches the latest release for a mod hosted on GitHub or GitLab.
   *
   * For GitHub the \c releases/latest endpoint is queried, for GitLab the first entry of the
   * project's releases list is used. Network or parse failures are handled gracefully.
   *
   * \param repo_url URL to the GitHub or GitLab repository.
   * \return If a release could be retrieved: A GitRelease with the version and publish time.
   * Else: An empty std::optional.
   */
  static std::optional<GitRelease> getLatestGitRelease(const std::string& repo_url);
  /*!
   * \brief Searches NexusMods for mods belonging to the given domain.
   *
   * The public NexusMods v1 API does not expose a free text search endpoint, so this
   * fetches the trending/latest/updated mod listings for the domain (depending on the
   * requested sort order) and, if a query is given, filters the results client-side by
   * matching the query against the mod name and summary. Optionally filters by a
   * NexusMods category id.
   *
   * Failures (non-200, rate limit 429, premium-only 403, ...) are logged and result in
   * an empty (or partial) result vector rather than an exception.
   *
   * \param domain_name NexusMods domain (game) to search, e.g. "skyrimspecialedition".
   * \param query Free text query. If empty, all fetched mods are returned.
   * \param sort_order Determines which listing is fetched and how results are ordered.
   * \param category_id If >= 0: only mods with this category id are returned.
   * \return A vector of SearchResult objects. Empty on failure or no matches.
   */
  static std::vector<SearchResult> searchMods(const std::string& domain_name,
                                              const std::string& query,
                                              SortOrder sort_order = SortOrder::endorsements,
                                              int category_id = -1);

private:
  /*!
   * \brief Builds the HTTP header used for requests to git platforms.
   *
   * GitHub requires a User-Agent header to be set; this mirrors that requirement for both
   * supported platforms.
   * \return The header to send with git platform requests.
   */
  static cpr::Header gitAuthHeader();
  /*!
   * \brief Parses an ISO 8601 timestamp (e.g. \c 2023-01-02T03:04:05Z) into a Unix time.
   * \param timestamp The timestamp string as returned by the git platform APIs.
   * \return The parsed Unix time, or 0 if parsing failed.
   */
  static std::time_t parseIso8601(const std::string& timestamp);
  /*!
   * \brief Performs a GET request against the given NexusMods listing endpoint and parses
   * the returned mods into SearchResult objects. Errors are logged, not thrown.
   * \param domain_name NexusMods domain the listing belongs to.
   * \param endpoint Listing endpoint name (e.g. "latest_added", "trending", "updated").
   * \return Parsed results, or an empty vector on failure.
   */
  static std::vector<SearchResult> fetchModListing(const std::string& domain_name,
                                                   const std::string& endpoint);

  /*! \brief The API key used for all operations. */
  inline static std::string api_key_ = "";
};
}
