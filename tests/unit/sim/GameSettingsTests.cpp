#include "croftech/core/ConfigFile.h"
#include "croftech/level/EntityClasses.h"
#include "croftech/sim/GameSettings.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream> // doctest prints string_view through operator<<
#include <string>
#include <vector>

namespace {

std::vector<ct::LayeredConfig::Layer> oneLayer(std::string_view text) {
    return {{"rules.cfg", ct::parseConfig(text)}};
}

void checkDefaults(const ct::RulesSettings& rules) {
    const ct::RulesSettings defaults;
    CHECK(rules.startHealth == defaults.startHealth);
    CHECK(rules.startArmour == defaults.startArmour);
    CHECK(rules.armourShare == doctest::Approx(defaults.armourShare));
    CHECK(rules.respawnDelay == doctest::Approx(defaults.respawnDelay));
    CHECK(rules.maxHealth == defaults.maxHealth);
    CHECK(rules.maxArmour == defaults.maxArmour);
    CHECK(rules.overchargeDecay == doctest::Approx(defaults.overchargeDecay));
}

} // namespace

TEST_CASE("rules variables start at the built-in defaults") {
    ct::Console console;
    ct::registerGameCVars(console);
    checkDefaults(ct::gameSettingsFromCVars(console).rules);
}

TEST_CASE("rules config sets the game settings and refuses values out of range") {
    ct::Console console;
    ct::registerGameCVars(console);
    const auto problems = console.applyConfig(ct::kRulesCVarPrefix, oneLayer("start_health 150\n"
                                                                             "start_armour 50\n"
                                                                             "armour_share 2\n"
                                                                             "respawn_delay 3\n"));
    const ct::GameSettings settings = ct::gameSettingsFromCVars(console);
    CHECK(settings.rules.startHealth == 150);
    CHECK(settings.rules.startArmour == 50);
    CHECK(settings.rules.armourShare == doctest::Approx(ct::RulesSettings{}.armourShare));
    CHECK(settings.rules.respawnDelay == doctest::Approx(3.0f));
    CHECK(problems.size() == 1);
}

TEST_CASE("the shipped rules.cfg matches the built-in defaults") {
    const auto text = ct::readTextFile(CROFTECH_CONTENT_DIR "/config/rules.cfg");
    REQUIRE(text.has_value());
    ct::Console console;
    ct::registerGameCVars(console);
    CHECK(console.applyConfig(ct::kRulesCVarPrefix, oneLayer(*text)).empty());
    checkDefaults(ct::gameSettingsFromCVars(console).rules);
}

TEST_CASE("each weapon's file sets its own variables, in the order the game lists them") {
    const std::vector<std::string> weapons{"pistol", "cannon"};
    ct::Console console;
    ct::registerGameCVars(console, weapons);
    const auto problems =
        console.applyConfig(ct::weaponCVarPrefix("cannon"), oneLayer("damage 80\n"
                                                                     "pellets 0\n"
                                                                     "spread 2.5\n"
                                                                     "given 1\n"));
    CHECK(problems.size() == 1); // pellets must be at least 1
    const ct::GameSettings settings = ct::gameSettingsFromCVars(console, weapons);
    REQUIRE(settings.weapons.size() == 2);
    CHECK(settings.weapons[0].damage == ct::WeaponSettings{}.damage);
    CHECK_FALSE(settings.weapons[0].given);
    CHECK(settings.weapons[1].damage == 80);
    CHECK(settings.weapons[1].pellets == 1);
    CHECK(settings.weapons[1].spread == doctest::Approx(2.5f));
    CHECK(settings.weapons[1].given);
    CHECK(settings.weapons[1].name == "cannon");
    CHECK(console.find("weapon.pistol.fire_interval") != nullptr);
}

TEST_CASE("a game's settings files are movement, rules and one per weapon, up to the limit") {
    std::vector<std::string> weapons;
    for (std::uint32_t i = 0; i < ct::kMaxWeapons + 2; ++i) {
        weapons.push_back("w" + std::to_string(i));
    }
    const std::vector<ct::GameConfigFile> files = ct::gameConfigFiles(weapons);
    REQUIRE(files.size() == 2 + ct::kMaxWeapons);
    CHECK(files[0].path == "config/movement.cfg");
    CHECK(files[1].prefix == ct::kRulesCVarPrefix);
    CHECK(files[2].path == "weapons/w0.cfg");
    CHECK(files[2].prefix == "weapon.w0.");
    ct::Console console;
    ct::registerGameCVars(console, weapons);
    CHECK(ct::gameSettingsFromCVars(console, weapons).weapons.size() == ct::kMaxWeapons);
}

TEST_CASE("the shipped weapon files set only known settings") {
    for (const std::string& weapon :
         {std::string("machinegun"), std::string("shotgun"), std::string("grenade_launcher"),
          std::string("rocket_launcher")}) {
        CAPTURE(weapon);
        const auto text =
            ct::readTextFile(std::string(CROFTECH_CONTENT_DIR "/weapons/") + weapon + ".cfg");
        REQUIRE(text.has_value());
        ct::Console console;
        const std::vector<std::string> weapons{weapon};
        ct::registerGameCVars(console, weapons);
        CHECK(console.applyConfig(ct::weaponCVarPrefix(weapon), oneLayer(*text)).empty());
        CHECK(ct::gameSettingsFromCVars(console, weapons).weapons.at(0).given);
    }
}

TEST_CASE("enemy files set enemy.<name>.* variables, and each kind moves at its own size") {
    const std::vector<std::string> weapons;
    const std::vector<std::string> enemies{"grunt"};
    const std::vector<ct::GameConfigFile> files = ct::gameConfigFiles(weapons, enemies);
    REQUIRE(files.size() == 3);
    CHECK(files[2].path == "enemies/grunt.cfg");
    CHECK(files[2].prefix == "enemy.grunt.");
    ct::Console console;
    ct::registerGameCVars(console, weapons, enemies);
    console.execute("enemy.grunt.width 1.2; enemy.grunt.height 2.4; enemy.grunt.speed 3");
    const ct::GameSettings settings = ct::gameSettingsFromCVars(console, weapons, enemies);
    REQUIRE(settings.enemies.size() == 1);
    const ct::EnemySettings& grunt = settings.enemies[0];
    CHECK(grunt.name == "grunt");
    CHECK(grunt.movement.halfExtents.x == 0.6f);
    CHECK(grunt.movement.halfExtents.z == 1.2f);
    CHECK(grunt.movement.maxGroundSpeed == 3.0f);
    // The rest of its movement is the game's.
    CHECK(grunt.movement.gravity == settings.movement.gravity);
}

TEST_CASE("the shipped enemy files set only known settings") {
    for (const std::string& enemy : {std::string("charger"), std::string("gunner")}) {
        CAPTURE(enemy);
        const auto text =
            ct::readTextFile(std::string(CROFTECH_CONTENT_DIR "/enemies/") + enemy + ".cfg");
        REQUIRE(text.has_value());
        ct::Console console;
        const std::vector<std::string> enemies{enemy};
        ct::registerGameCVars(console, {}, enemies);
        CHECK(console.applyConfig(ct::enemyCVarPrefix(enemy), oneLayer(*text)).empty());
        const ct::EnemySettings settings =
            ct::gameSettingsFromCVars(console, {}, enemies).enemies.at(0);
        CHECK((settings.meleeDamage > 0 || !settings.weapon.empty()));
    }
}

TEST_CASE("an enemy's weapon is found among the game's weapons by name") {
    const std::vector<std::string> weapons{"pistol", "rifle"};
    const std::vector<std::string> enemies{"grunt", "brute"};
    ct::Console console;
    ct::registerGameCVars(console, weapons, enemies);
    console.execute("enemy.grunt.weapon rifle; enemy.brute.weapon cannon");
    const ct::GameSettings settings = ct::gameSettingsFromCVars(console, weapons, enemies);
    CHECK(settings.enemies[0].weaponIndex == 1);
    CHECK(settings.enemies[1].weapon == "cannon");
    CHECK(settings.enemies[1].weaponIndex == 0xFFFFFFFFu);
}

TEST_CASE("an enemy type's look and sounds are read, and ones that won't do are left out") {
    const std::vector<std::string> enemies{"grunt", "brute"};
    ct::Console console;
    ct::registerGameCVars(console, {}, enemies);
    console.applyConfig(ct::enemyCVarPrefix("grunt"), oneLayer("description Walks at you slowly\n"
                                                               "sounds charger\n"
                                                               "colour 0.5 0.25 1\n"));
    console.applyConfig(ct::enemyCVarPrefix("brute"), oneLayer("sounds ../secret\n"
                                                               "colour 0.5 2 1\n"));
    const ct::GameSettings settings = ct::gameSettingsFromCVars(console, {}, enemies);
    const ct::EnemySettings& grunt = settings.enemies.at(0);
    CHECK(grunt.description == "Walks at you slowly");
    CHECK(grunt.soundSet() == "charger");
    REQUIRE(grunt.colour.has_value());
    CHECK(grunt.colour->y == 0.25f);
    const ct::EnemySettings& brute = settings.enemies.at(1);
    CHECK(brute.soundSet() == "brute");
    CHECK_FALSE(brute.colour.has_value());
}

TEST_CASE("every enemy setting one enemy may change is a setting of its type's file") {
    ct::Console console;
    const std::vector<std::string> enemies{"charger"};
    ct::registerGameCVars(console, {}, enemies);
    for (const ct::EnemySetting& setting : ct::enemySettings()) {
        INFO(setting.key);
        CHECK(console.find("enemy.charger." + std::string(setting.key)) != nullptr);
        CHECK(ct::findEnemySetting(setting.key).has_value());
    }
    CHECK_FALSE(ct::findEnemySetting("description").has_value());
    CHECK_FALSE(ct::findEnemySetting("colour").has_value());
    // A level's enemies and spawners set them as properties, so none may be one of theirs.
    for (const std::string_view className : {ct::kEnemyClass, ct::kSpawnerClass}) {
        const ct::EntityClassInfo* entityClass = ct::findEntityClass(className);
        REQUIRE(entityClass != nullptr);
        for (const ct::PropertyInfo& property : entityClass->properties) {
            INFO(property.key);
            CHECK_FALSE(ct::findEnemySetting(property.key).has_value());
        }
    }
}

TEST_CASE("an enemy setting is set within its range, and a weapon past the game's is none") {
    ct::EnemyTuning tuning;
    const std::uint32_t speed = *ct::findEnemySetting("speed");
    ct::setEnemySetting(tuning, speed, 9.5f);
    CHECK(tuning.speed == 9.5f);
    CHECK(ct::enemySettingValue(tuning, speed) == 9.5f);
    ct::setEnemySetting(tuning, speed, 1000.0f);
    CHECK(tuning.speed == 50.0f);

    const std::uint32_t health = *ct::findEnemySetting("health");
    ct::setEnemySetting(tuning, health, 41.6f);
    CHECK(tuning.health == 42);
    ct::setEnemySetting(tuning, health, -5.0f);
    CHECK(tuning.health == 1);

    const std::uint32_t weapon = *ct::findEnemySetting("weapon");
    ct::setEnemySetting(tuning, weapon, 2.0f);
    CHECK(tuning.weaponIndex == 2);
    CHECK(ct::enemySettingValue(tuning, weapon) == 2.0f);
    ct::setEnemySetting(tuning, weapon, static_cast<float>(ct::kMaxWeapons));
    CHECK(tuning.weaponIndex == 0xFFFFFFFFu);
}
