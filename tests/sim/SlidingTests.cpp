// Sliding along walls and slopes in the movement test level, and staying out of solid while doing
// it. Regression tests for players sticking to walls and being steered sideways on ramps.

#include "SimHarness.h"
#include "TestLevels.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>

using ct::test::SimHarness;

namespace {

constexpr int kOneSecond = static_cast<int>(ct::kTickRate);
constexpr float kDegrees = 3.14159265f / 180.0f;

std::uint16_t yawFromDegrees(float degrees) {
    return static_cast<std::uint16_t>(
        static_cast<std::int32_t>(std::lround(degrees * 65536.0f / 360.0f)) & 0xffff);
}

float headingDegrees(const ct::PlayerMotion& motion) {
    return std::atan2(motion.velocity.y, motion.velocity.x) / kDegrees;
}

bool insideSolid(const ct::CollisionWorld& world, const SimHarness& sim) {
    const ct::Vec3 position = sim.transform().position;
    return world.traceBox(position, position, sim.movement().halfExtents).startSolid;
}

ct::UserCmd walk(float yawDegrees) {
    ct::UserCmd cmd;
    cmd.yaw = yawFromDegrees(yawDegrees);
    cmd.forward = 127;
    return cmd;
}

} // namespace

TEST_CASE("walking along a wall at a shallow angle slides along it") {
    const ct::Level level = ct::test::makeMovementTestLevel();
    const ct::CollisionWorld world(level.brushes);

    // Each outer wall, walked with the wall on the player's right, starting 1 m away from it.
    struct Wall {
        const char* name;
        ct::Vec3 start;
        float alongYaw;
    };
    const Wall walls[] = {
        {"south", {-30.0f, -39.0f, 0.0f}, 0.0f},
        {"east", {39.0f, -30.0f, 0.0f}, 90.0f},
        {"north", {30.0f, 39.0f, 0.0f}, 180.0f},
        {"west", {-39.0f, 30.0f, 0.0f}, 270.0f},
    };
    for (const Wall& wall : walls) {
        for (const float into : {1.0f, 5.0f, 15.0f, 30.0f}) {
            CAPTURE(wall.name);
            CAPTURE(into);
            SimHarness sim(level.brushes);
            sim.spawn(wall.start);
            const ct::UserCmd cmd = walk(wall.alongYaw - into);
            bool everInside = false;
            for (int tick = 0; tick < 3 * kOneSecond; ++tick) {
                sim.run(cmd, 1);
                everInside = everInside || insideSolid(world, sim);
            }
            const ct::Vec3 along{std::cos(wall.alongYaw * kDegrees),
                                 std::sin(wall.alongYaw * kDegrees), 0.0f};
            CHECK_FALSE(everInside);
            CHECK(sim.onGround());
            CHECK(ct::dot(sim.transform().position - wall.start, along) > 20.0f);
        }
    }
}

TEST_CASE("walking up a ramp keeps the player's heading") {
    const ct::Level level = ct::test::makeMovementTestLevel();
    // The 20 degree ramp rises towards +X from x = 6 to 12, between y = 5 and 9. Steeper headings
    // would leave its side before x = 8.
    for (const float yaw : {0.0f, 2.0f, 10.0f, 20.0f, -15.0f}) {
        CAPTURE(yaw);
        SimHarness sim(level.brushes);
        sim.spawn({3.0f, 7.0f, 0.0f});
        const ct::UserCmd cmd = walk(yaw);
        int ticks = 0;
        while (sim.transform().position.x < 8.0f && ticks < 2 * kOneSecond) {
            sim.run(cmd, 1);
            ++ticks;
        }
        CHECK(sim.transform().position.x >= 8.0f);
        CHECK(sim.onGround());
        CHECK(sim.feetZ() > 0.5f);
        CHECK(std::fabs(headingDegrees(sim.motion()) - yaw) < 0.25f);
    }
}

TEST_CASE("walking straight up the ramp reaches the platform at full speed") {
    const ct::Level level = ct::test::makeMovementTestLevel();
    SimHarness sim(level.brushes);
    sim.spawn({3.0f, 7.0f, 0.0f});
    int ticks = 0;
    while (sim.transform().position.x < 13.0f && ticks < 2 * kOneSecond) {
        sim.run(walk(0.0f), 1);
        ++ticks;
    }
    CHECK(sim.transform().position.x >= 13.0f); // on the platform, which ends at x = 16
    CHECK(sim.feetZ() == doctest::Approx(2.2f).epsilon(0.01));
    CHECK(ct::length(sim.motion().velocity) ==
          doctest::Approx(sim.movement().maxGroundSpeed).epsilon(0.01));
}

TEST_CASE("walking down the ramp keeps the player's heading and footing") {
    const ct::Level level = ct::test::makeMovementTestLevel();
    for (const float yaw : {180.0f, 190.0f, 165.0f}) {
        CAPTURE(yaw);
        SimHarness sim(level.brushes);
        sim.spawn({15.0f, 7.0f, 2.2f});
        const ct::UserCmd cmd = walk(yaw);
        bool leftGround = false;
        int ticks = 0;
        while (sim.transform().position.x > 8.0f && ticks < 2 * kOneSecond) {
            sim.run(cmd, 1);
            leftGround = leftGround || !sim.onGround();
            ++ticks;
        }
        CHECK(sim.transform().position.x <= 8.0f);
        CHECK_FALSE(leftGround);
        const float expected = yaw > 180.0f ? yaw - 360.0f : yaw;
        CHECK(std::fabs(headingDegrees(sim.motion()) - expected) < 0.25f);
    }
}

TEST_CASE("a player wandering the test level never ends up inside solid") {
    const ct::Level level = ct::test::makeMovementTestLevel();
    const ct::CollisionWorld world(level.brushes);
    for (const std::uint32_t seed : {1u, 2u, 3u}) {
        CAPTURE(seed);
        SimHarness sim(level.brushes);
        sim.spawn(ct::test::kMovementTestSpawn);
        std::uint32_t random = seed;
        const auto next = [&random] {
            random = random * 1664525u + 1013904223u;
            return random >> 8;
        };
        ct::UserCmd cmd;
        int insideTicks = 0;
        for (int tick = 0; tick < 60 * kOneSecond; ++tick) {
            if (tick % 32 == 0) {
                cmd.yaw = static_cast<std::uint16_t>(next());
                cmd.forward = static_cast<std::int8_t>(static_cast<int>(next() % 3) * 127 - 127);
                cmd.side = static_cast<std::int8_t>(static_cast<int>(next() % 3) * 127 - 127);
                cmd.buttons = next() % 4 == 0 ? ct::kButtonJump : 0;
            }
            sim.run(cmd, 1);
            insideTicks += insideSolid(world, sim) ? 1 : 0;
        }
        CHECK(insideTicks == 0);
    }
}
