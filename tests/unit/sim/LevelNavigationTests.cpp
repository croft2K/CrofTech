#include "croftech/level/LevelLoad.h"
#include "croftech/script/ScriptFiles.h"
#include "croftech/sim/GameLevel.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <vector>

TEST_CASE("the shipped levels build navigation meshes with no warnings") {
    for (const char* path : {CROFTECH_CONTENT_DIR "/levels/movement_test.level",
                             CROFTECH_CONTENT_DIR "/levels/weapons_test.level",
                             CROFTECH_CONTENT_DIR "/levels/enemies_test.level",
                             CROFTECH_CONTENT_DIR "/levels/encounter_test.level",
                             CROFTECH_CONTENT_DIR "/levels/script_test.level",
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

TEST_CASE("the enemies test level plays with chargers and gunners and no warnings") {
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
    ct::EnemySettings gunner = charger;
    gunner.name = "gunner";
    settings.enemies = {charger, gunner};
    const ct::GameLevel gameLevel(*level, {}, settings);
    for (const std::string& warning : gameLevel.warnings()) {
        FAIL_CHECK(warning);
    }
    const std::span<const ct::Enemy> enemies = gameLevel.startState().world.enemies.components();
    CHECK(enemies.size() == 7);
    const auto patrolling =
        std::count_if(enemies.begin(), enemies.end(), [](const ct::Enemy& enemy) {
            return ct::enemyState(enemy) == ct::EnemyState::Patrol;
        });
    CHECK(patrolling == 2);
}

TEST_CASE("the encounter test level has an arena of three waves and no warnings") {
    std::vector<std::string> importWarnings;
    std::string error;
    const std::optional<ct::Level> level = ct::loadAnyLevelFile(
        CROFTECH_CONTENT_DIR "/levels/encounter_test.level", importWarnings, error);
    REQUIRE_MESSAGE(level, error);
    ct::GameSettings settings;
    for (const char* name : {"machinegun", "shotgun", "rocket_launcher"}) {
        ct::WeaponSettings weapon;
        weapon.name = name;
        settings.weapons.push_back(weapon);
    }
    ct::EnemySettings charger;
    charger.name = "charger";
    charger.movement = ct::enemyMovement(charger, settings.movement);
    ct::EnemySettings gunner = charger;
    gunner.name = "gunner";
    settings.enemies = {charger, gunner};
    const ct::GameLevel gameLevel(*level, {}, settings);
    for (const std::string& warning : gameLevel.warnings()) {
        FAIL_CHECK(warning);
    }
    const ct::World& world = gameLevel.startState().world;
    CHECK(world.spawners.size() == 10);
    REQUIRE(world.encounters.size() == 1);
    const ct::LevelEntityRef* ref = world.levelEntities.get(world.encounters.entities()[0]);
    REQUIRE(ref != nullptr);
    CHECK(gameLevel.entity(ref->index)->waveCount == 3);
    CHECK(world.enemies.size() == 0);
}

TEST_CASE("the script test level runs the combination script with no warnings") {
    std::vector<std::string> importWarnings;
    std::string error;
    const std::optional<ct::Level> level = ct::loadAnyLevelFile(
        CROFTECH_CONTENT_DIR "/levels/script_test.level", importWarnings, error);
    REQUIRE_MESSAGE(level, error);
    ct::ContentPaths content;
    content.addLayer(CROFTECH_CONTENT_DIR);
    std::vector<std::string> warnings;
    const std::vector<ct::ScriptSource> scripts = ct::loadLevelScripts(content, *level, warnings);
    CHECK(warnings.empty());
    REQUIRE(scripts.size() == 1);
    CHECK(scripts[0].name == "combination");

    ct::GameSettings settings;
    for (const char* name : {"machinegun", "shotgun"}) {
        ct::WeaponSettings weapon;
        weapon.name = name;
        settings.weapons.push_back(weapon);
    }
    ct::EnemySettings charger;
    charger.name = "charger";
    charger.movement = ct::enemyMovement(charger, settings.movement);
    settings.enemies = {charger};
    const ct::GameLevel gameLevel(*level, {}, settings, scripts);
    for (const std::string& warning : gameLevel.warnings()) {
        FAIL_CHECK(warning);
    }
    const ct::World& world = gameLevel.startState().world;
    CHECK(world.scripts.size() == 1);
    CHECK(world.pendingInputs.size() == 1); // its start function
}
