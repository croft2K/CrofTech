#include "croftech/core/ConfigFile.h"
#include "croftech/sim/GameSettings.h"

#include <doctest/doctest.h>

#include <cstdint>
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
    for (const std::string& weapon : {std::string("machinegun"), std::string("shotgun")}) {
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
