/*!
 * \file gamebanana_provider.h
 * \brief Header for the remote::GamebananProvider class.
 *
 * Scaffold / partial implementation for the GameBanana mod repository.
 * Relevant upstream issues: limo-app/limo#60, limo-app/limo#4.
 *
 * GameBanana API reference: https://api.gamebanana.com/
 *
 * Status: SCAFFOLDED.
 *   - HTTP request/parse structure is complete and compilable.
 *   - search() is fully implemented using the public /Core/List/New endpoint.
 *   - getModInfo() and getFiles() are implemented for the "Mod" itemtype.
 *   - getDownloadUrl() resolves the CDN URL from the files metadata.
 *   No API key is required for read-only operations.
 */

#pragma once

#include "remotesource.h"
#include <json/json.h>
#include <string>
#include <vector>


namespace remote
{

/*!
 * \brief RemoteSource implementation for the GameBanana mod repository.
 *
 * \par Community identifier
 * The \c community parameter maps to a GameBanana game ID integer, passed
 * as a string, e.g. "8133" for Risk of Rain 2.  The game ID can be found
 * in the GameBanana website URL for the game's page.
 *
 * \par Mod identifier
 * \c mod_id is the GameBanana item ID (integer as string), e.g. "123456".
 *
 * \par File identifier
 * \c file_id is the GameBanana file ID (integer as string) within a submission.
 *
 * \note GameBanana supports many item types (Mod, Sound, Skin, …).  This
 *       provider always uses itemtype "Mod" for simplicity; a future revision
 *       could expose itemtype as a constructor parameter.
 */
class GamebananaProvider : public RemoteSource
{
public:
  GamebananaProvider() = default;

  std::string name() const override;

  /*!
   * \brief Search GameBanana for mods matching a keyword in a game.
   *
   * Uses GET https://api.gamebanana.com/Core/List/New with the game filter
   * applied via the gameid parameter and the search term via the keyword
   * parameter.
   *
   * \param community GameBanana game ID as string (e.g. "8133").
   * \param query     Keyword string.
   * \param max_results Maximum number of results.
   * \return Matching mods.
   */
  std::vector<RemoteMod> search(const std::string& community,
                                const std::string& query,
                                int max_results = 20) override;

  /*!
   * \brief Fetch metadata for a single GameBanana mod.
   *
   * Uses GET https://api.gamebanana.com/Core/Item/Data with fields:
   *   name, description, Owner().name, Screenshots().aFiles(), Files().aFiles()
   *
   * \param community GameBanana game ID as string.
   * \param mod_id    GameBanana item ID as string.
   * \return Populated RemoteMod.
   */
  RemoteMod getModInfo(const std::string& community,
                       const std::string& mod_id) override;

  /*!
   * \brief List downloadable files for a GameBanana mod.
   *
   * Parses the "Files().aFiles()" field returned by the item data endpoint.
   *
   * \param community GameBanana game ID as string.
   * \param mod_id    GameBanana item ID as string.
   * \return One RemoteFile per downloadable archive.
   */
  std::vector<RemoteFile> getFiles(const std::string& community,
                                   const std::string& mod_id) override;

  /*!
   * \brief Resolve the download URL for a specific file.
   *
   * GameBanana embeds download URLs in the Files().aFiles() array so this
   * calls getFiles() and returns the matching entry's URL.
   *
   * \param community GameBanana game ID as string.
   * \param mod_id    GameBanana item ID as string.
   * \param file_id   GameBanana file ID as string.
   * \return Direct CDN download URL.
   */
  std::string getDownloadUrl(const std::string& community,
                             const std::string& mod_id,
                             const std::string& file_id) override;

  bool requiresApiKey() const override { return false; }

private:
  /*! \brief Base URL for all GameBanana API calls. */
  static constexpr const char* BASE_URL = "https://api.gamebanana.com";
  /*! \brief User-Agent header value sent with every request. */
  static constexpr const char* USER_AGENT = "limo-mod-manager/1.2.2 (limo-app/limo#60)";
  /*! \brief GameBanana item type to query (always "Mod" for now). */
  static constexpr const char* ITEM_TYPE = "Mod";

  /*!
   * \brief Fetch raw item data JSON for a given GameBanana item ID.
   * \param mod_id GameBanana item ID as string.
   * \param fields Comma-separated list of data fields to request.
   * \return Parsed JSON::Value array containing the requested fields.
   */
  static Json::Value fetchItemData(const std::string& mod_id, const std::string& fields);
};

} // namespace remote
