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

TEST_CASE("Loose car files at the archive root are wrapped in a mod-named subfolder",
          "[normalizer]")
{
  // ui_car.json and the rest sit directly at the root with no enclosing car folder. Without a
  // wrap, relocateUnderPrefix would produce content/cars/ui_car.json (a broken AC install); with
  // the mod name supplied they land under content/cars/<mod>/.
  const std::vector<std::string> paths{ "ui_car.json", "body.kn5", "skins/red/skin.dds" };
  REQUIRE(contentPrefix(paths, 0, AC_ANCHORS, "Lambo Huracan") == "content/cars/Lambo Huracan");
}

TEST_CASE("Loose-root wrap sanitizes path separators in the folder name", "[normalizer]")
{
  const std::vector<std::string> paths{ "ui_car.json", "body.kn5" };
  REQUIRE(contentPrefix(paths, 0, AC_ANCHORS, "a/b\\c") == "content/cars/a_b_c");
}

TEST_CASE("A car already in its own folder is not double-wrapped", "[normalizer]")
{
  // The marker is one level deep (my_car/ui_car.json), so the mod name must NOT be appended.
  const std::vector<std::string> paths{ "my_car/ui_car.json", "my_car/body.kn5" };
  REQUIRE(contentPrefix(paths, 0, AC_ANCHORS, "Ignored Name") == "content/cars");
}

TEST_CASE("Loose root with no folder name falls back to the bare prefix", "[normalizer]")
{
  const std::vector<std::string> paths{ "ui_car.json", "body.kn5" };
  REQUIRE(contentPrefix(paths, 0, AC_ANCHORS, "") == "content/cars");
}
