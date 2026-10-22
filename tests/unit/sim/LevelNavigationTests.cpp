#include "croftech/level/LevelLoad.h"
#include "croftech/sim/GameLevel.h"

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

TEST_CASE("the shipped levels build navigation meshes with no warnings") {
    for (const char* path : {CROFTECH_CONTENT_DIR "/levels/movement_test.level",
                             CROFTECH_CONTENT_DIR "/levels/weapons_test.level",
                             CROFTECH_SOURCE_DIR "/games/sandbox/content/levels/map1.map"}) {
        CAPTURE(path);
        std::vector<std::string> importWarnings;
        std::string error;
        const std::optional<ct::Level> level = ct::loadAnyLevelFile(path, importWarnings, error);
        REQUIRE_MESSAGE(level, error);
        const ct::NavMesh mesh(ct::navInput(*level), ct::navAgent({}));
        CHECK(mesh.warnings().empty());
        CHECK(mesh.polygonCount() > 0);
    }
}
