#include "croftech/sim/MovementConfig.h"

#include <doctest/doctest.h>

#include <vector>

namespace {

std::vector<ct::LayeredConfig::Layer> oneLayer(std::string_view text) {
    return {{"movement.cfg", ct::parseConfig(text)}};
}

void checkDefaults(const ct::MovementSettings& settings) {
    const ct::MovementSettings defaults;
    CHECK(settings.gravity == doctest::Approx(defaults.gravity));
    CHECK(settings.maxGroundSpeed == doctest::Approx(defaults.maxGroundSpeed));
    CHECK(settings.groundAcceleration == doctest::Approx(defaults.groundAcceleration));
    CHECK(settings.airAcceleration == doctest::Approx(defaults.airAcceleration));
    CHECK(settings.airWishSpeedCap == doctest::Approx(defaults.airWishSpeedCap));
    CHECK(settings.friction == doctest::Approx(defaults.friction));
    CHECK(settings.stopSpeed == doctest::Approx(defaults.stopSpeed));
    CHECK(settings.jumpSpeed == doctest::Approx(defaults.jumpSpeed));
    CHECK(settings.stepHeight == doctest::Approx(defaults.stepHeight));
    CHECK(settings.minGroundNormalZ == doctest::Approx(defaults.minGroundNormalZ));
    CHECK(settings.halfExtents.x == doctest::Approx(defaults.halfExtents.x));
    CHECK(settings.halfExtents.y == doctest::Approx(defaults.halfExtents.y));
    CHECK(settings.halfExtents.z == doctest::Approx(defaults.halfExtents.z));
    CHECK(settings.eyeHeight == doctest::Approx(defaults.eyeHeight));
    CHECK(settings.autoJump == defaults.autoJump);
}

} // namespace

TEST_CASE("movement variables start at the built-in defaults") {
    ct::Console console;
    ct::registerMovementCVars(console);
    checkDefaults(ct::movementSettingsFromCVars(console));
}

TEST_CASE("movement config applies known settings and reports the rest") {
    ct::Console console;
    ct::registerMovementCVars(console);
    const auto problems =
        console.applyConfig(ct::kMovementCVarPrefix, oneLayer("gravity 9.81\n"
                                                              "auto_jump 1\n"
                                                              "player_height 2.0\n"
                                                              "jump_speed fast\n"
                                                              "warp_speed 9\n"
                                                              "friction -1\n"
                                                              "player_width 0\n"));
    const ct::MovementSettings settings = ct::movementSettingsFromCVars(console);
    CHECK(settings.gravity == doctest::Approx(9.81f));
    CHECK(settings.autoJump);
    CHECK(settings.halfExtents.z == doctest::Approx(1.0f));
    CHECK(settings.jumpSpeed == ct::MovementSettings{}.jumpSpeed);
    CHECK(settings.friction == ct::MovementSettings{}.friction);
    CHECK(settings.halfExtents.x == ct::MovementSettings{}.halfExtents.x);
    CHECK(problems.size() == 4);
}

TEST_CASE("the console changes movement settings") {
    ct::Console console;
    ct::registerMovementCVars(console);
    console.execute("movement.gravity 16; movement.player_width 1");
    const ct::MovementSettings settings = ct::movementSettingsFromCVars(console);
    CHECK(settings.gravity == doctest::Approx(16.0f));
    CHECK(settings.halfExtents.x == doctest::Approx(0.5f));
    CHECK(settings.halfExtents.y == doctest::Approx(0.5f));
}

TEST_CASE("the shipped movement.cfg matches the built-in defaults") {
    const auto text = ct::readTextFile(CROFTECH_CONTENT_DIR "/config/movement.cfg");
    REQUIRE(text.has_value());
    ct::Console console;
    ct::registerMovementCVars(console);
    CHECK(console.applyConfig(ct::kMovementCVarPrefix, oneLayer(*text)).empty());
    checkDefaults(ct::movementSettingsFromCVars(console));
}
