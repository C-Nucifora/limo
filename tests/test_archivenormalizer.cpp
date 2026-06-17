#include "../src/core/archivenormalizer.h"
#include <catch2/catch_test_macros.hpp>

using archive_normalizer::Anchor;
using archive_normalizer::contentPrefix;

static const std::vector<Anchor> AC_ANCHORS{ { "ui_car.json", "content/cars" },
                                             { "ui_track.json", "content/tracks" } };


TEST_CASE("Bare car archive is re-rooted under content/cars", "[normalizer]")
{
  // Archive packed as <car>/... with ui_car.json at the car root.
  const std::vector<std::string> paths{ "my_car/ui_car.json",
                                        "my_car/body.kn5",
                                        "my_car/skins/red/skin.dds" };
  REQUIRE(contentPrefix(paths, 0, AC_ANCHORS) == "content/cars");
}

TEST_CASE("Correctly-rooted content archive needs no prefix", "[normalizer]")
{
  const std::vector<std::string> paths{ "content/cars/my_car/ui_car.json",
                                        "content/cars/my_car/body.kn5" };
  REQUIRE(contentPrefix(paths, 0, AC_ANCHORS).empty());
}

TEST_CASE("Double-nested archive is handled by the root-level strip then needs no prefix",
          "[normalizer]")
{
  // ModName/content/cars/... — the installer's root_level strips ModName (level 1), after which
  // the marker already lives under content/cars, so no prefix is added.
  const std::vector<std::string> paths{ "ModName/content/cars/my_car/ui_car.json" };
  REQUIRE(contentPrefix(paths, 1, AC_ANCHORS).empty());
}

TEST_CASE("Bare track archive is re-rooted under content/tracks", "[normalizer]")
{
  const std::vector<std::string> paths{ "spa/ui_track.json", "spa/models.ini" };
  REQUIRE(contentPrefix(paths, 0, AC_ANCHORS) == "content/tracks");
}

TEST_CASE("Archive without a known marker is left unchanged", "[normalizer]")
{
  const std::vector<std::string> paths{ "apps/python/myapp/myapp.py" };
  REQUIRE(contentPrefix(paths, 0, AC_ANCHORS).empty());
}

TEST_CASE("Marker matching is case-insensitive", "[normalizer]")
{
  const std::vector<std::string> paths{ "MyCar/UI_Car.JSON" };
  REQUIRE(contentPrefix(paths, 0, AC_ANCHORS) == "content/cars");
}

TEST_CASE("No anchors means no change", "[normalizer]")
{
  const std::vector<std::string> paths{ "my_car/ui_car.json" };
  REQUIRE(contentPrefix(paths, 0, {}).empty());
}
