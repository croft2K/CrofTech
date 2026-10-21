#include "croftech/core/ConfigFile.h"
#include "croftech/sim/GameSettings.h"

#include <doctest/doctest.h>

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
