#include "croftech/core/ConfigFile.h"
#include "croftech/level/BrushGeometry.h"
#include "croftech/level/LevelFile.h"
#include "croftech/level/MapImport.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

// TrenchBroom's starting brush: 128 x 128 x 32 units centred on the origin, standard format.
constexpr const char* kStandardBrush =
    "{\n"
    "( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) a 0 0 0 1 1\n"
    "( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) a 0 0 0 1 1\n"
    "( -64 -64 -16 ) ( -63 -64 -16 ) ( -64 -63 -16 ) a 0 0 0 1 1\n"
    "( 64 64 16 ) ( 64 65 16 ) ( 65 64 16 ) a 0 0 0 1 1\n"
    "( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) a 0 0 0 1 1\n"
    "( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) a 0 0 0 1 1\n"
    "}\n";

// The same brush in the Valve 220 format, as TrenchBroom writes it.
constexpr const char* kValveBrush =
    "{\n"
    "( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) __TB_empty [ 0 -1 0 0 ] [ 0 0 -1 0 ] 0 1 1\n"
    "( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) __TB_empty [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1\n"
    "( -64 -64 -16 ) ( -63 -64 -16 ) ( -64 -63 -16 ) __TB_empty [ -1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\n"
    "( 64 64 16 ) ( 64 65 16 ) ( 65 64 16 ) __TB_empty [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\n"
    "( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) __TB_empty [ -1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1\n"
    "( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) __TB_empty [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1\n"
    "}\n";

std::string worldspawn(const std::string& brushes) {
    return "// Game: Generic\n"
           "// Format: Valve\n"
           "// entity 0\n"
           "{\n"
           "\"mapversion\" \"220\"\n"
           "\"classname\" \"worldspawn\"\n" +
           brushes + "}\n";
}

struct Import {
    std::optional<ct::Level> level;
    std::vector<std::string> warnings;
    std::string error;
};

Import import(const std::string& text) {
    Import result;
    result.level = ct::importMap(text, {}, result.warnings, result.error);
    return result;
}

void checkStartingBrush(const ct::Brush& brush) {
    ct::Aabb bounds;
    REQUIRE(ct::computeBrushBounds(brush, bounds));
    const float halfWidth = 64.0f * 0.0254f;
    const float halfHeight = 16.0f * 0.0254f;
    CHECK(bounds.min.x == doctest::Approx(-halfWidth));
    CHECK(bounds.max.x == doctest::Approx(halfWidth));
    CHECK(bounds.min.y == doctest::Approx(-halfWidth));
    CHECK(bounds.max.y == doctest::Approx(halfWidth));
    CHECK(bounds.min.z == doctest::Approx(-halfHeight));
    CHECK(bounds.max.z == doctest::Approx(halfHeight));
}

} // namespace

TEST_CASE("a standard-format brush imports as a solid scaled to metres") {
    const Import result = import(worldspawn(kStandardBrush));
    REQUIRE_MESSAGE(result.level.has_value(), result.error);
    CHECK(result.warnings.empty());
    REQUIRE(result.level->brushes.size() == 1);
    checkStartingBrush(result.level->brushes[0]);
}

TEST_CASE("a Valve 220 brush imports the same planes as the standard format") {
    const Import standard = import(worldspawn(kStandardBrush));
    const Import valve = import(worldspawn(kValveBrush));
    REQUIRE_MESSAGE(valve.level.has_value(), valve.error);
    REQUIRE(standard.level.has_value());
    CHECK(valve.level->brushes == standard.level->brushes);
}

TEST_CASE("player starts become player_spawn entities at the feet, in metres") {
    const Import result =
        import(worldspawn(kStandardBrush) + "{\n"
                                            "\"classname\" \"info_player_start\"\n"
                                            "\"origin\" \"100 -50 40\"\n"
                                            "\"angle\" \"90\"\n"
                                            "\"_tb_group\" \"3\"\n"
                                            "}\n"
                                            "{\n"
                                            "\"classname\" \"light\"\n"
                                            "\"origin\" \"0 0 64\"\n"
                                            "\"light\" \"300\"\n"
                                            "}\n");
    REQUIRE_MESSAGE(result.level.has_value(), result.error);
    CHECK(result.warnings.empty());
    REQUIRE(result.level->entities.size() == 2);

    std::string error;
    const auto spawns = ct::readSpawnPoints(*result.level, error);
    REQUIRE_MESSAGE(spawns.has_value(), error);
    REQUIRE(spawns->size() == 1);
    CHECK((*spawns)[0].feetPosition.x == doctest::Approx(100.0f * 0.0254f));
    CHECK((*spawns)[0].feetPosition.y == doctest::Approx(-50.0f * 0.0254f));
    CHECK((*spawns)[0].feetPosition.z == doctest::Approx(16.0f * 0.0254f));
    CHECK((*spawns)[0].yaw == 16384);
    CHECK(ct::findProperty(result.level->entities[0], "_tb_group") == nullptr);

    const ct::LevelEntity& light = result.level->entities[1];
    CHECK(light.className == "light");
    REQUIRE(ct::findProperty(light, "origin") != nullptr);
    CHECK(*ct::findProperty(light, "origin") == "0 0 1.6256");
    CHECK(*ct::findProperty(light, "light") == "300");
}

TEST_CASE(
    "groups import as world brushes, brush entities with their brushes; patches are skipped") {
    const Import result =
        import(worldspawn(kStandardBrush) +
               "{\n\"classname\" \"func_group\"\n\"_tb_type\" \"_tb_layer\"\n" + kValveBrush +
               "}\n" + "{\n\"classname\" \"func_wall\"\n" + kStandardBrush + "}\n" +
               "{\n\"classname\" \"worldspawn\"\n"
               "{\npatchDef2\n{\n\"textures/x\"\n( 3 3 0 0 0 )\n(\n"
               "( ( 0 0 0 0 0 ) )\n)\n}\n}\n}\n");
    REQUIRE_MESSAGE(result.level.has_value(), result.error);
    REQUIRE(result.level->brushes.size() == 3);
    CHECK(result.level->brushes[0].entity == ct::kNoEntity);
    CHECK(result.level->brushes[1].entity == ct::kNoEntity);
    CHECK(result.level->brushes[2].entity == 0);
    REQUIRE(result.level->entities.size() == 1);
    CHECK(result.level->entities[0].className == "func_wall");
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings[0] == "line 41: patches and brush primitives are not supported; skipped");
}

TEST_CASE("Quake doors and triggers become door and trigger entities") {
    const Import result = import(
        worldspawn("") +
        "{\n\"classname\" \"func_door\"\n\"angle\" \"90\"\n"
        "\"speed\" \"200\"\n\"targetname\" \"gate\"\n" +
        kStandardBrush + "}\n" + "{\n\"classname\" \"func_door\"\n\"angle\" \"-1\"\n" +
        kStandardBrush + "}\n" + "{\n\"classname\" \"trigger_once\"\n" + kStandardBrush + "}\n");
    REQUIRE_MESSAGE(result.level.has_value(), result.error);
    CHECK(result.warnings.empty());
    REQUIRE(result.level->entities.size() == 3);
    REQUIRE(result.level->brushes.size() == 3);

    // 128 units wide less the default lip of 8, sideways along +Y.
    const ct::LevelEntity& gate = result.level->entities[0];
    CHECK(gate.className == "door");
    CHECK(*ct::findProperty(gate, "move") == ct::formatVec3({0.0f, 120.0f * 0.0254f, 0.0f}));
    CHECK(*ct::findProperty(gate, "speed") == ct::formatFloat(200.0f * 0.0254f));
    CHECK(*ct::findProperty(gate, "name") == "gate");
    CHECK(ct::findProperty(gate, "angle") == nullptr);
    CHECK(ct::findProperty(gate, "yaw") == nullptr);

    // Up by its 32 unit height less the lip, at the default 100 units a second.
    const ct::LevelEntity& lift = result.level->entities[1];
    CHECK(*ct::findProperty(lift, "move") == ct::formatVec3({0.0f, 0.0f, 24.0f * 0.0254f}));
    CHECK(*ct::findProperty(lift, "speed") == ct::formatFloat(100.0f * 0.0254f));

    CHECK(result.level->entities[2].className == "trigger");
    CHECK(*ct::findProperty(result.level->entities[2], "once") == "1");
    for (std::uint32_t i = 0; i < 3; ++i) {
        CHECK(result.level->brushes[i].entity == i);
    }
}

TEST_CASE("bad brushes are skipped with a warning") {
    const Import result = import(worldspawn("{\n"
                                            "( 0 0 0 ) ( 1 1 1 ) ( 2 2 2 ) a 0 0 0 1 1\n"
                                            "( 64 64 16 ) ( 64 65 16 ) ( 65 64 16 ) a 0 0 0 1 1\n"
                                            "( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) a 0 0 0 1 1\n"
                                            "( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) a 0 0 0 1 1\n"
                                            "}\n"
                                            "{\n"
                                            "( 64 64 16 ) ( 64 65 16 ) ( 65 64 16 ) a 0 0 0 1 1\n"
                                            "( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) a 0 0 0 1 1\n"
                                            "( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) a 0 0 0 1 1\n"
                                            "( 64 64 16 ) ( 64 65 16 ) ( 65 64 16 ) a 0 0 0 1 1\n"
                                            "}\n"));
    REQUIRE_MESSAGE(result.level.has_value(), result.error);
    CHECK(result.level->brushes.empty());
    REQUIRE(result.warnings.size() == 2);
    CHECK(result.warnings[0] == "line 7: brush has a face whose points are in a line; skipped");
    CHECK(result.warnings[1] == "line 13: brush is not a closed solid inside the world; skipped");
}

TEST_CASE("syntax errors fail the import with the line of the problem") {
    CHECK(import("{\n\"classname\" \"worldspawn\"\n").error == "line 1: entity has no closing '}'");
    CHECK(import("\"classname\" \"worldspawn\"\n").error ==
          "line 1: expected '{' to start an entity");
    CHECK(import("{\n\"classname\" \"world\nspawn\"\n}\n").error ==
          "line 2: expected a quoted key and value");
    CHECK(import(worldspawn("{\n( 0 0 0 ) ( 1 0 x ) ( 0 1 0 ) a\n}\n")).error ==
          "line 8: a face starts with three points: ( x y z )");
    CHECK(import(worldspawn("{\n( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 )\n}\n")).error ==
          "line 8: face has no texture name");
    CHECK(import(worldspawn("{\n( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) a\n\"k\" \"v\"\n}\n")).error ==
          "line 9: expected '(' to start a face or '}' to end the brush");
    CHECK(import("{\n\"classname\" \"x\"\n?\n}\n").error == "line 3: unexpected '?' in entity");
}

TEST_CASE("imported levels always save as level files") {
    const Import result = import(worldspawn(kValveBrush) + "{\n"
                                                           "\"classname\" \"info_player_start\"\n"
                                                           "\"origin\" \"0 0 24\"\n"
                                                           "\"bad key\" \"x\"\n"
                                                           "\"message\" \"  padded  \"\n"
                                                           "\"message\" \"again\"\n"
                                                           "}\n");
    REQUIRE_MESSAGE(result.level.has_value(), result.error);
    REQUIRE(result.warnings.size() == 2);
    CHECK(result.warnings[0] ==
          "line 16: info_player_start: property 'bad key' is not supported; dropped");
    CHECK(result.warnings[1] ==
          "line 16: info_player_start: 'message' appears twice; later one dropped");
    std::string error;
    const auto text = ct::formatLevel(*result.level, error);
    REQUIRE_MESSAGE(text.has_value(), error);
    CHECK(ct::parseLevel(*text, error) == result.level);
}

TEST_CASE("a func_door with no brushes comes in as a door with nothing to move") {
    const Import result = import("{\n\"classname\" \"func_door\"\n\"angle\" \"90\"\n}\n");
    REQUIRE_MESSAGE(result.level.has_value(), result.error);
    REQUIRE(result.level->entities.size() == 1);
    CHECK(result.level->entities[0].className == "door");
    CHECK(ct::findProperty(result.level->entities[0], "move") == nullptr);
}

TEST_CASE("a brush entity is skipped even when none of its brushes are valid") {
    const Import result = import("{\n"
                                 "\"classname\" \"func_door\"\n"
                                 "{\n"
                                 "( 0 0 0 ) ( 0 1 0 ) ( 0 0 1 ) door 0 0 0 1 1\n"
                                 "}\n"
                                 "}\n");
    REQUIRE_MESSAGE(result.level.has_value(), result.error);
    CHECK(result.level->entities.empty());
    REQUIRE(result.warnings.size() == 2);
    CHECK(result.warnings[1] == "line 1: brush entity 'func_door' has no valid brushes; skipped");
}

TEST_CASE("the TrenchBroom sample map imports its world, light and quoted text") {
    std::vector<std::string> warnings;
    std::string error;
    const auto level = ct::importMapFile(CROFTECH_SOURCE_DIR "/tests/fuzz/corpus/map/valve.map", {},
                                         warnings, error);
    REQUIRE_MESSAGE(level.has_value(), error);
    CHECK(level->brushes.size() == 2);
    REQUIRE(level->entities.size() == 1);
    const ct::LevelEntity& light = level->entities[0];
    CHECK(light.className == "light");
    REQUIRE(ct::findProperty(light, "message") != nullptr);
    CHECK(*ct::findProperty(light, "message") == "a \"quoted\" word");
    CHECK(ct::findProperty(light, "_tb_layer") == nullptr);
    CHECK(warnings.size() == 3); // the door's flat brush, the door itself, the patch
}
