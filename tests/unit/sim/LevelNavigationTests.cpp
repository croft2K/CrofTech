#include "croftech/level/LevelLoad.h"
#include "croftech/sim/GameLevel.h"

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

TEST_CASE("the shipped levels build navigation meshes with no warnings") {
    for (const char* path : {CROFTECH_CONTENT_DIR "/levels/movement_test.level",
                             CROFTECH_CONTENT_DIR "/levels/weapons_test.level",
                             CROFTECH_CONTENT_DIR "/levels/enemies_test.level",
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

TEST_CASE("the enemies test level plays with a charger and no warnings") {
    std::vector<std::string> importWarnings;
    std::string error;
    const std::optional<ct::Level> level = ct::loadAnyLevelFile(
        CROFTECH_CONTENT_DIR "/levels/enemies_test.level", importWarnings, error);
    REQUIRE_MESSAGE(level, error);
    ct::GameSettings settings;
    ct::WeaponSettings shotgun;
    shotgun.name = "shotgun";
    ct::WeaponSettings rockets;
    rockets.name = "rocket_launcher";
    settings.weapons = {shotgun, rockets};
    ct::EnemySettings charger;
    charger.name = "charger";
    charger.movement = ct::enemyMovement(charger, settings.movement);
    settings.enemies = {charger};
    const ct::GameLevel gameLevel(*level, {}, settings);
    for (const std::string& warning : gameLevel.warnings()) {
        FAIL_CHECK(warning);
    }
    CHECK(gameLevel.startState().world.enemies.size() == 5);
}
