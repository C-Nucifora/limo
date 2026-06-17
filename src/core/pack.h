/*!
 * \file pack.h
 * \brief First-class "modpack" type and pure helpers for union/order computation (fork #242).
 *
 * A pack is a named, ordered set of mods, distinct from tags. Any number of packs can be active
 * within a profile; the deployed set is the union of the active packs' mods, ordered by pack
 * priority (the pack's position in the application's pack list) and then by each pack's internal
 * order. These helpers are pure so the union/order logic can be unit-tested.
 */

#pragma once

#include <set>
#include <string>
#include <vector>


/*!
 * \brief A named, ordered set of mods that can be toggled on/off as a unit.
 */
struct Pack
{
  /*! \brief Unique pack name. */
  std::string name;
  /*! \brief Free-text notes. */
  std::string notes;
  /*! \brief Member mod ids, in the pack's intended load order. */
  std::vector<int> mod_ids;
};

namespace pack_util
{

/*!
 * \brief Computes the set of mod ids enabled by the active packs (their union).
 * \param packs  All packs, in priority order.
 * \param active Names of the active packs.
 */
std::set<int> enabledSet(const std::vector<Pack>& packs, const std::set<std::string>& active);

/*!
 * \brief Computes the deploy order for the active packs: each active pack's mods in order,
 * concatenated by pack priority, de-duplicated (first occurrence wins).
 * \param packs  All packs, in priority order.
 * \param active Names of the active packs.
 */
std::vector<int> deployOrder(const std::vector<Pack>& packs, const std::set<std::string>& active);

} // namespace pack_util
