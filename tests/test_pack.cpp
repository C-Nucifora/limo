#include "../src/core/pack.h"
#include <catch2/catch_test_macros.hpp>


TEST_CASE("Pack enabled set is the union of active packs", "[packs]")
{
  const std::vector<Pack> packs{ { "A", "", { 0, 1 } }, { "B", "", { 1, 2 } }, { "C", "", { 5 } } };
  const auto enabled = pack_util::enabledSet(packs, { "A", "B" });
  REQUIRE(enabled == std::set<int>{ 0, 1, 2 });
  REQUIRE(pack_util::enabledSet(packs, {}).empty());
  REQUIRE(pack_util::enabledSet(packs, { "C" }) == std::set<int>{ 5 });
}

TEST_CASE("Pack deploy order follows priority then in-pack order, de-duplicated", "[packs]")
{
  // Priority is the pack's position in the vector.
  const std::vector<Pack> packs{ { "Hi", "", { 2, 0 } }, { "Lo", "", { 1, 2 } } };
  // Hi's [2,0] first, then Lo's [1] (2 already placed).
  REQUIRE(pack_util::deployOrder(packs, { "Hi", "Lo" }) == std::vector<int>{ 2, 0, 1 });
  // Only Lo active.
  REQUIRE(pack_util::deployOrder(packs, { "Lo" }) == std::vector<int>{ 1, 2 });
  // Inactive packs contribute nothing.
  REQUIRE(pack_util::deployOrder(packs, {}).empty());
}
