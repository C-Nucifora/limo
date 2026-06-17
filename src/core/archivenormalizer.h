/*!
 * \file archivenormalizer.h
 * \brief Computes an install prefix that re-roots inconsistently-packed mod archives.
 *
 * Some games (notably Assetto Corsa, issue #240) ship mods whose archive root is
 * inconsistent: a car may be packed as `content/cars/<car>/...` (correct) or as a bare
 * `<car>/...` with `ui_car.json` at the top (needs to land under `content/cars/`).
 * \ref RootLevelCondition can only *strip* leading directories; it cannot *add* a parent.
 *
 * This helper detects, from preset-declared anchors, whether a recognizable marker file
 * (e.g. `ui_car.json`) is present but not already under its expected parent, and if so returns
 * the prefix to prepend at install time. Pure and game-agnostic: the anchors come from the
 * game preset, so the same mechanism works for any game that needs it.
 */

#pragma once

#include <string>
#include <vector>


namespace archive_normalizer
{

/*!
 * \brief A marker file and the archive prefix its tree should live under.
 *
 * E.g. {marker: "ui_car.json", prefix: "content/cars"}: an archive containing a `ui_car.json`
 * that is not already under `content/cars/` should be re-rooted by prepending `content/cars`.
 */
struct Anchor
{
  /*! \brief File name whose presence identifies this content type (case-insensitive). */
  std::string marker;
  /*! \brief Archive-relative prefix the marker's tree belongs under (e.g. "content/cars"). */
  std::string prefix;
};

/*!
 * \brief Computes the prefix to prepend so a marker's content lands under the right parent.
 *
 * The archive paths are first reduced by \p root_level leading directory components (mirroring
 * the installer's strip). The anchors are tried in order; the first anchor whose marker file is
 * present decides the result: an empty string if the marker already lives under its prefix
 * (the archive is correctly rooted), otherwise the anchor's prefix. Returns an empty string if
 * no anchor's marker is present.
 *
 * \param archive_paths Archive-relative file paths (forward-slash separated).
 * \param root_level    Number of leading directory components stripped at install.
 * \param anchors       Preset-declared marker→prefix anchors.
 * \return The prefix to prepend (e.g. "content/cars"), or "" for no change.
 */
std::string contentPrefix(const std::vector<std::string>& archive_paths,
                          int root_level,
                          const std::vector<Anchor>& anchors);

} // namespace archive_normalizer
