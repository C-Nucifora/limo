/*!
 * \file fsservermods.h
 * \brief Lists the mods served by a Farming Simulator dedicated server.
 *
 * A FS dedicated server exposes its active mod set as an HTML directory index at
 * `http://<server>:<port>/mods` — a list of `<a href="...zip">` links (the same endpoint
 * FS25_ModManager and the in-game client use). This is a clean, well-defined automation
 * target (issue #241): fetch the index, list the .zip mods, and hand the chosen ones to the
 * normal download/import flow. The HTML parsing is a pure function so it can be unit-tested
 * without a live server.
 */

#pragma once

#include <string>
#include <vector>


namespace remote
{

/*! \brief One downloadable mod advertised by a FS dedicated server. */
struct FsServerMod
{
  /*! \brief File name (e.g. "FS25_FollowMe.zip"). */
  std::string file_name;
  /*! \brief Absolute download URL. */
  std::string download_url;
};

/*!
 * \brief Lists the .zip mods served by a Farming Simulator dedicated server.
 *
 * All methods are static. Network I/O uses the vendored cpr library.
 */
class FsServerMods
{
public:
  /*!
   * \brief Normalizes a user-entered server URL to its `/mods` index URL.
   *
   * Adds a scheme if missing, and appends `/mods` when the URL has no path (so a user can
   * enter just `host:port`). A URL that already points at `/mods` is left as-is.
   * \param server_url The user-entered server URL.
   * \return The normalized mods-index URL.
   */
  static std::string normalizeUrl(const std::string& server_url);

  /*!
   * \brief Parses a FS server's mod-index HTML into a list of downloadable mods.
   * \param html      Raw HTML of the `/mods` index.
   * \param index_url The index URL, used to resolve relative hrefs.
   * \return The .zip mods found, de-duplicated by URL, in document order.
   */
  static std::vector<FsServerMod> parseModIndex(const std::string& html,
                                                const std::string& index_url);

  /*!
   * \brief Fetches and parses a FS server's mod list.
   * \param server_url The user-entered server URL (normalized internally).
   * \param error      Set to a user-facing message on failure (left empty on success).
   * \return The available mods, or an empty list on failure (see \p error).
   */
  static std::vector<FsServerMod> fetchModList(const std::string& server_url, std::string& error);
};

} // namespace remote
