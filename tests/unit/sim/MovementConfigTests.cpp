#include "croftech/sim/MovementConfig.h"

#include <doctest/doctest.h>

TEST_CASE("movement config applies known settings and reports the rest") {
    ct::MovementSettings settings;
    const auto entries = ct::parseConfig("gravity 9.81\n"
                                         "auto_jump 1\n"
                                         "player_height 2.0\n"
                                         "jump_speed fast\n"
                                         "warp_speed 9\n"
                                         "friction -1\n");
    const auto problems = ct::applyMovementConfig(entries, settings);
    CHECK(settings.gravity == doctest::Approx(9.81f));
    CHECK(settings.autoJump);
    CHECK(settings.halfExtents.z == doctest::Approx(1.0f));
    CHECK(settings.jumpSpeed == ct::MovementSettings{}.jumpSpeed);
    CHECK(settings.friction == ct::MovementSettings{}.friction);
    CHECK(problems.size() == 3);
}

TEST_CASE("the shipped movement.cfg matches the built-in defaults") {
    const auto text = ct::readTextFile(CROFTECH_CONTENT_DIR "/config/movement.cfg");
    REQUIRE(text.has_value());
    ct::MovementSettings fromFile;
    CHECK(ct::applyMovementConfig(ct::parseConfig(*text), fromFile).empty());
    const ct::MovementSettings defaults;
    CHECK(fromFile.gravity == doctest::Approx(defaults.gravity));
    CHECK(fromFile.maxGroundSpeed == doctest::Approx(defaults.maxGroundSpeed));
    CHECK(fromFile.jumpSpeed == doctest::Approx(defaults.jumpSpeed));
    CHECK(fromFile.airWishSpeedCap == doctest::Approx(defaults.airWishSpeedCap));
    CHECK(fromFile.halfExtents.x == doctest::Approx(defaults.halfExtents.x));
    CHECK(fromFile.halfExtents.z == doctest::Approx(defaults.halfExtents.z));
    CHECK(fromFile.autoJump == defaults.autoJump);
}
