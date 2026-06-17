/*!
 * \file linkimporter.h
 * \brief Resolves a pasted mod-page URL into a direct, downloadable archive link.
 *
 * Implements the "paste a link → download" importer from fork issue #233:
 *   - GitHub Releases (clean, documented API; always available).
 *   - Farming Simulator GIANTS ModHub (experimental, opt-in; fragile HTML scrape
 *     behind a hotlink gate that requires a browser User-Agent + Referer).
 *
 * The resolver is intentionally single-mod and gentle: one HTTP request per user
 * action, no bulk crawling or ID enumeration. The page/JSON parsing is split out
 * into pure functions (\ref parseModHubPage, \ref parseGithubReleaseJson) so it can
 * be unit-tested without network access.
 */

#pragma once

#include <string>


namespace remote
{

/*!
 * \brief Result of resolving a pasted URL to a direct download.
 *
 * On success \ref ok is true and \ref download_url / \ref file_name are populated.
 * \ref user_agent and \ref referer carry the HTTP headers the actual download must
 * send (ModHub's CDN enforces hotlink protection); both empty means "no special
 * headers needed". On failure \ref ok is false and \ref error holds a user-facing
 * message that points back to the robust manual-import path.
 */
struct ResolvedLink
{
  /*! \brief True if the URL was resolved to a direct download. */
  bool ok = false;
  /*! \brief User-facing error message when \ref ok is false. */
  std::string error;
  /*! \brief Final, directly downloadable archive URL. */
  std::string download_url;
  /*! \brief File name the archive should be saved as. */
  std::string file_name;
  /*! \brief Human-readable mod name (used as the imported mod's name). */
  std::string mod_name;
  /*! \brief Version string, if the source exposes one (else empty). */
  std::string version;
  /*! \brief User-Agent the download must send, or empty for the default. */
  std::string user_agent;
  /*! \brief Referer the download must send, or empty for none. */
  std::string referer;
};

/*!
 * \brief Resolves pasted mod-page URLs (GitHub Releases / FS ModHub) to direct downloads.
 *
 * All methods are static; the class is a namespace-like grouping. Network I/O uses the
 * vendored cpr library, consistent with the rest of src/core.
 */
class LinkImporter
{
public:
  /*! \brief A desktop-browser User-Agent used to satisfy ModHub's hotlink gate. */
  static const std::string browser_user_agent;
  /*! \brief Limo's own User-Agent, sent to APIs (e.g. GitHub) that require one. */
  static const std::string limo_user_agent;

  /*! \brief Whether \p url looks like a Farming Simulator ModHub mod-page URL. */
  static bool isModHubUrl(const std::string& url);
  /*! \brief Whether \p url looks like a GitHub repository / release URL. */
  static bool isGithubUrl(const std::string& url);
  /*! \brief Whether \p url is one of the supported import sources. */
  static bool isSupportedUrl(const std::string& url);

  /*!
   * \brief Resolves \p url to a direct download, dispatching by source.
   * \param url         The pasted mod-page / repository URL.
   * \param allow_modhub Whether the experimental ModHub importer is enabled. When false,
   *        a ModHub URL resolves to an error that points at manual import + the setting.
   */
  static ResolvedLink resolve(const std::string& url, bool allow_modhub);

  /*!
   * \brief Fetches and parses a GitHub repo/release URL into a direct asset download.
   * Picks the first archive asset of the chosen release, falling back to the source
   * zipball if the release publishes no archive assets.
   */
  static ResolvedLink resolveGithub(const std::string& url);
  /*!
   * \brief Fetches a FS ModHub mod page (with a browser User-Agent) and scrapes the
   * direct CDN .zip link from it. Sets the User-Agent + Referer the download needs.
   */
  static ResolvedLink resolveModHub(const std::string& mod_page_url);

  // --- pure parsers (no network; unit-tested) -------------------------------------

  /*!
   * \brief Extracts the direct CDN .zip link from a ModHub mod-page's HTML.
   * \param html         Raw HTML of the mod page.
   * \param mod_page_url The page URL, used as the download Referer.
   */
  static ResolvedLink parseModHubPage(const std::string& html, const std::string& mod_page_url);
  /*!
   * \brief Selects a downloadable asset from a GitHub release API JSON document.
   * \param json_text Raw JSON returned by the GitHub releases endpoint.
   * \param repo      Repository name, used as a label / zipball file-name base.
   */
  static ResolvedLink parseGithubReleaseJson(const std::string& json_text, const std::string& repo);
};

} // namespace remote
