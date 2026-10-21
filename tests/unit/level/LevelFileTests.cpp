#include "TestFiles.h"

#include "croftech/level/BrushGeometry.h"
#include "croftech/level/LevelFile.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <optional>
#include <string>

#include "sim/TestLevels.h"

namespace {

// A bowl open at the top: a square floor, a ring of 45 degree faces, then a steeper ring that
// never closes. The floor and first ring are five complete faces, so only the closure check
// catches it.
constexpr const char* kOpenBowl = "croftech_level 1\n"
                                  "brush\n"
                                  "    plane 0 0 -1 0\n"
                                  "    plane 0.70710677 0 -0.70710677 0.70710677\n"
                                  "    plane -0.70710677 0 -0.70710677 0.70710677\n"
                                  "    plane 0 0.70710677 -0.70710677 0.70710677\n"
                                  "    plane 0 -0.70710677 -0.70710677 0.70710677\n"
                                  "    plane 0.98058068 0 -0.19611614 1.7650452\n"
                                  "    plane -0.98058068 0 -0.19611614 1.7650452\n"
                                  "    plane 0 0.98058068 -0.19611614 1.7650452\n"
                                  "    plane 0 -0.98058068 -0.19611614 1.7650452\n"
                                  "end\n";

// A unit box in the current format; the bottom face and corner 7's z can be swapped out.
std::string boxText(std::string_view bottomFace = "face 0 2 3 1", std::string_view topZ = "1") {
    return std::string("croftech_level 2\n"
                       "brush\n"
                       "    vertex 0 0 0\n    vertex 1 0 0\n    vertex 0 1 0\n    vertex 1 1 0\n"
                       "    vertex 0 0 1\n    vertex 1 0 1\n    vertex 0 1 1\n    vertex 1 1 ") +
           std::string(topZ) +
           "\n"
           "    face 1 3 7 5\n    face 0 4 6 2\n    face 2 6 7 3\n    face 0 1 5 4\n"
           "    face 4 5 7 6\n    " +
           std::string(bottomFace) + "\nend\n";
}

// The same box in version 1, as planes.
std::string planeBoxText(std::string_view lastPlane = "plane 0 0 -1 0") {
    return std::string("croftech_level 1\n"
                       "brush\n"
                       "    plane 1 0 0 1\n"
                       "    plane -1 0 0 0\n"
                       "    plane 0 1 0 1\n"
                       "    plane 0 -1 0 0\n"
                       "    plane 0 0 1 1\n    ") +
           std::string(lastPlane) + "\nend\n";
}

std::string parseError(const std::string& text) {
    std::string error;
    const std::optional<ct::Level> level = ct::parseLevel(text, error);
    CHECK_FALSE(level.has_value());
    return error;
}

} // namespace

TEST_CASE("the movement test level saves and loads back unchanged") {
    const ct::Level level = ct::test::makeMovementTestLevel();
    std::string error;
    const std::optional<std::string> text = ct::formatLevel(level, error);
    REQUIRE_MESSAGE(text.has_value(), error);
    const std::optional<ct::Level> loaded = ct::parseLevel(*text, error);
    REQUIRE_MESSAGE(loaded.has_value(), error);
    CHECK(*loaded == level);

    const std::optional<std::string> again = ct::formatLevel(*loaded, error);
    REQUIRE(again.has_value());
    CHECK(*again == *text);
}

TEST_CASE("the shipped movement test level loads and has a spawn point") {
    std::string error;
    const std::optional<ct::Level> level =
        ct::loadLevelFile(CROFTECH_CONTENT_DIR "/levels/movement_test.level", error);
    REQUIRE_MESSAGE(level.has_value(), error);
    CHECK_FALSE(level->brushes.empty());
    const auto spawns = ct::readSpawnPoints(*level, error);
    REQUIRE_MESSAGE(spawns.has_value(), error);
    CHECK_FALSE(spawns->empty());
}

TEST_CASE("comments, blank lines, indentation and CRLF line ends are accepted") {
    std::string error;
    const auto level = ct::parseLevel("# a level\r\n"
                                      "\r\n"
                                      "croftech_level 1\r\n"
                                      "entity light\r\n"
                                      "\t# comment inside a block\r\n"
                                      "  colour   1 0.5 0  \r\n"
                                      "  label two words\r\n"
                                      "  flag\r\n"
                                      "end",
                                      error);
    REQUIRE_MESSAGE(level.has_value(), error);
    REQUIRE(level->entities.size() == 1);
    const ct::LevelEntity& light = level->entities[0];
    CHECK(light.className == "light");
    REQUIRE(light.properties.size() == 3);
    CHECK(light.properties[0] == ct::EntityProperty{"colour", "1 0.5 0"});
    CHECK(light.properties[1] == ct::EntityProperty{"label", "two words"});
    CHECK(light.properties[2] == ct::EntityProperty{"flag", ""});

    // An empty value saves without a trailing space and loads back the same.
    const auto text = ct::formatLevel(*level, error);
    REQUIRE(text.has_value());
    CHECK(text->find("    flag\n") != std::string::npos);
    CHECK(ct::parseLevel(*text, error) == level);
}

TEST_CASE("a box brush loads with its corners and faces in file order") {
    std::string error;
    const auto level = ct::parseLevel(boxText(), error);
    REQUIRE_MESSAGE(level.has_value(), error);
    REQUIRE(level->brushes.size() == 1);
    CHECK(level->brushes[0] == ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
}

TEST_CASE("version 1 brushes, made of planes, load as corners and save as version 2") {
    std::string error;
    const auto level = ct::parseLevel(planeBoxText(), error);
    REQUIRE_MESSAGE(level.has_value(), error);
    REQUIRE(level->brushes.size() == 1);
    const ct::Brush& brush = level->brushes[0];
    CHECK(brush.vertices.size() == 8);
    CHECK(brush.faces.size() == 6);
    ct::Aabb bounds;
    REQUIRE(ct::computeBrushBounds(brush, bounds));
    CHECK(bounds.min == ct::Vec3{0.0f, 0.0f, 0.0f});
    CHECK(bounds.max == ct::Vec3{1.0f, 1.0f, 1.0f});
    // Faces come in plane order: the first plane was +X.
    CHECK(ct::facePlane(brush, 0) == ct::Plane{{1.0f, 0.0f, 0.0f}, 1.0f});

    const auto text = ct::formatLevel(*level, error);
    REQUIRE(text.has_value());
    CHECK(text->starts_with("croftech_level 2\n"));
    CHECK(ct::parseLevel(*text, error) == level);
}

TEST_CASE("malformed level files are rejected with the line of the problem") {
    CHECK(parseError("") == "line 1: not a level file (it is empty)");
    CHECK(parseError("brush\nend\n").starts_with("line 1: not a level file"));
    CHECK(parseError("croftech_level 3\n") == "line 1: unsupported level file version");
    CHECK(parseError("croftech_level 2\nwall\n") == "line 2: expected 'brush' or 'entity'");
    CHECK(parseError("croftech_level 2\nbrush extra\n") == "line 2: expected 'brush' or 'entity'");
    CHECK(parseError("croftech_level 2\n\nbrush\n    vertex 1 0 0\n") ==
          "line 3: brush has no 'end'");
    CHECK(parseError("croftech_level 2\nentity a b\nend\n") == "line 2: invalid entity class name");
    CHECK(parseError("croftech_level 2\nentity a\n    bad/key 1\nend\n") ==
          "line 3: invalid property key");
    CHECK(parseError("croftech_level 2\nentity a\n    k 1\n    k 2\nend\n") ==
          "line 2: property 'k' appears twice");
    CHECK(parseError("croftech_level 2\nbrush\n    vertex 1 0\nend\n")
              .starts_with("line 3: a vertex is three numbers"));
    CHECK(parseError("croftech_level 2\nbrush\n    vertex 1 0 nan\nend\n")
              .starts_with("line 3: a vertex is three numbers"));
    CHECK(parseError("croftech_level 2\nbrush\n    face 0 1\nend\n")
              .starts_with("line 3: a face is 3 to"));
    CHECK(parseError("croftech_level 2\nbrush\n    face 0 1 -2\nend\n")
              .starts_with("line 3: a face is 3 to"));
    CHECK(parseError("croftech_level 2\nbrush\n    face 0 1 x\nend\n")
              .starts_with("line 3: a face is 3 to"));
    CHECK(parseError("croftech_level 2\nbrush\n    plane 1 0 0 1\nend\n") ==
          "line 3: expected 'vertex', 'face' or 'end'");
    CHECK(parseError("croftech_level 1\nbrush\n    plane 1 0 0\nend\n")
              .starts_with("line 3: a plane is four numbers"));
    CHECK(parseError("croftech_level 1\nbrush\n    plane 0 0 0 1\nend\n") ==
          "line 3: plane normal is zero");
    CHECK(parseError("croftech_level 1\nbrush\n    face 1 0 0 1\nend\n") ==
          "line 3: expected 'plane' or 'end'");
}

TEST_CASE("brushes must be closed convex solids within the world") {
    // Missing its bottom face: open downwards.
    CHECK(parseError(boxText("")) == "line 2: brush is not a closed solid");
    CHECK(parseError(boxText("face 0 2 3 1 4")) == "line 2: brush is not a closed solid");
    CHECK(parseError(boxText("face 0 2 3 9")) ==
          "line 2: face uses corner 9, which does not exist");
    CHECK(parseError(boxText("face 0 2 3 1", "3000")).starts_with("line 2: brush reaches outside"));
    CHECK(parseError(boxText("face 0 2 3 1", "0.5")) == "line 2: a face is not flat");
    CHECK(parseError("croftech_level 2\nbrush\nend\n").starts_with("line 2: a brush needs 4 to"));

    std::string text = "croftech_level 2\nbrush\n";
    for (int i = 0; i <= 512; ++i) {
        text += "    face 0 1 2\n";
    }
    CHECK(parseError(text + "end\n") == "line 515: more than 512 faces");

    // Version 1: the planes must enclose something inside the world.
    CHECK(parseError(planeBoxText("plane 0 0 1 1")) ==
          "line 2: planes do not enclose a valid brush");
    CHECK(parseError(planeBoxText("plane 0 0 -1 -5")) ==
          "line 2: planes do not enclose a valid brush");
    CHECK(parseError(planeBoxText("plane 0 0 -1 3000")) ==
          "line 2: planes do not enclose a valid brush");
    CHECK(parseError(kOpenBowl) == "line 2: planes do not enclose a valid brush");
}

TEST_CASE("formatLevel refuses levels that would not load again") {
    ct::Level level;
    level.brushes.push_back(ct::makeBoxBrush({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    level.brushes[0].faces.pop_back();
    std::string error;
    CHECK_FALSE(ct::formatLevel(level, error).has_value());
    CHECK(error == "brush 0: brush is not a closed solid");

    level.brushes.clear();
    level.entities.push_back({"note", {{"text", "two\nlines"}}});
    CHECK_FALSE(ct::formatLevel(level, error).has_value());
    CHECK(error == "entity 0 (note): property 'text' has an invalid value");

    level.entities[0] = {"note", {{"end", "x"}}};
    CHECK_FALSE(ct::formatLevel(level, error).has_value());
    CHECK(error == "entity 0 (note): invalid property key");
}

TEST_CASE("levels save to disk and load back") {
    ct::test::TempDirectory temp;
    const std::filesystem::path path = temp.path() / "test.level";
    const ct::Level level = ct::test::makeMovementTestLevel();
    std::string error;
    REQUIRE_MESSAGE(ct::saveLevelFile(level, path, error), error);
    CHECK_FALSE(std::filesystem::exists(temp.path() / "test.level.tmp"));
    const auto loaded = ct::loadLevelFile(path, error);
    REQUIRE_MESSAGE(loaded.has_value(), error);
    CHECK(*loaded == level);

    // A failed save leaves the existing file alone.
    ct::Level broken;
    broken.entities.push_back({"bad class", {}});
    CHECK_FALSE(ct::saveLevelFile(broken, path, error));
    CHECK(ct::loadLevelFile(path, error) == level);

    CHECK_FALSE(ct::loadLevelFile(temp.path() / "missing.level", error).has_value());
    CHECK(error.find("cannot read") != std::string::npos);
}

TEST_CASE("spawn points read origin and yaw in degrees") {
    ct::Level level;
    level.entities.push_back(ct::makePlayerSpawn({1.0f, 2.0f, 3.0f}, 90.0f));
    level.entities.push_back({"light", {{"origin", "0 0 0"}}});
    level.entities.push_back(ct::makePlayerSpawn({-1.0f, 0.0f, 0.5f}, -90.0f));
    level.entities.push_back({"player_spawn", {{"origin", "0 0 0"}}});

    std::string error;
    const auto spawns = ct::readSpawnPoints(level, error);
    REQUIRE_MESSAGE(spawns.has_value(), error);
    REQUIRE(spawns->size() == 3);
    CHECK((*spawns)[0].feetPosition == ct::Vec3{1.0f, 2.0f, 3.0f});
    CHECK((*spawns)[0].yaw == 16384);
    CHECK((*spawns)[1].yaw == 49152);
    CHECK((*spawns)[2].yaw == 0);

    level.entities[3].properties[0].value = "0 0";
    CHECK_FALSE(ct::readSpawnPoints(level, error).has_value());
    CHECK(error == "entity 3 (player_spawn): origin must be three numbers");

    level.entities[3].properties[0].value = "0 0 5000";
    CHECK_FALSE(ct::readSpawnPoints(level, error).has_value());
    CHECK(error == "entity 3 (player_spawn): origin is outside the world");
}
