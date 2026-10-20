#include "SimHarness.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using ct::test::floorBrush;
using ct::test::SimHarness;

namespace {

constexpr int kOneSecond = static_cast<int>(ct::kTickRate);

ct::UserCmd walkForward() {
    ct::UserCmd cmd;
    cmd.forward = 127;
    return cmd;
}

float horizontalSpeed(const ct::PlayerMotion& motion) {
    return std::sqrt(motion.velocity.x * motion.velocity.x + motion.velocity.y * motion.velocity.y);
}

} // namespace

TEST_CASE("a player standing still stays on the floor") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run({}, kOneSecond);
    CHECK(sim.onGround());
    CHECK(sim.feetZ() == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(sim.transform().position.x == 0.0f);
}

TEST_CASE("a falling player lands on the floor") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 5.0f});
    sim.run({}, 1);
    CHECK_FALSE(sim.onGround());
    sim.run({}, 2 * kOneSecond);
    CHECK(sim.onGround());
    CHECK(sim.feetZ() == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(sim.motion().velocity.z == 0.0f);
}

TEST_CASE("walking reaches the maximum ground speed and no more") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(walkForward(), kOneSecond);
    CHECK(horizontalSpeed(sim.motion()) ==
          doctest::Approx(sim.movement().maxGroundSpeed).epsilon(0.01));
    CHECK(sim.transform().position.x > 5.0f);
    CHECK(std::fabs(sim.transform().position.y) < 1e-4f);
}

TEST_CASE("diagonal input is not faster than straight input") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 0.0f});
    ct::UserCmd cmd = walkForward();
    cmd.side = 127;
    sim.run(cmd, kOneSecond);
    CHECK(horizontalSpeed(sim.motion()) <= sim.movement().maxGroundSpeed * 1.001f);
}

TEST_CASE("friction stops a player who lets go") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(walkForward(), kOneSecond);
    sim.run({}, kOneSecond);
    CHECK(horizontalSpeed(sim.motion()) == 0.0f);
}

TEST_CASE("yaw turns the walking direction") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 0.0f});
    ct::UserCmd cmd = walkForward();
    cmd.yaw = 16384; // 90 degrees: +Y
    sim.run(cmd, kOneSecond / 2);
    CHECK(sim.transform().position.y > 2.0f);
    CHECK(std::fabs(sim.transform().position.x) < 1e-3f);
}

TEST_CASE("a wall stops the player") {
    SimHarness sim({floorBrush(), ct::makeBoxBrush({5.0f, -10.0f, 0.0f}, {6.0f, 10.0f, 3.0f})});
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(walkForward(), 2 * kOneSecond);
    const float maxX = 5.0f - sim.movement().halfExtents.x;
    CHECK(sim.transform().position.x <= maxX);
    CHECK(sim.transform().position.x == doctest::Approx(maxX).epsilon(0.001));
    CHECK(sim.onGround());
}

TEST_CASE("sliding along a wall at an angle keeps moving") {
    SimHarness sim({floorBrush(), ct::makeBoxBrush({5.0f, -20.0f, 0.0f}, {6.0f, 20.0f, 3.0f})});
    sim.spawn({4.0f, 0.0f, 0.0f});
    ct::UserCmd cmd = walkForward();
    cmd.yaw = 4096; // 22.5 degrees towards +Y, into the wall
    sim.run(cmd, kOneSecond);
    CHECK(sim.transform().position.y > 1.5f);
    CHECK(sim.transform().position.x <= 5.0f - sim.movement().halfExtents.x);
}

TEST_CASE("the player walks up stairs no taller than the step height") {
    std::vector<ct::Brush> brushes{floorBrush()};
    for (int i = 0; i < 5; ++i) {
        const float x = 3.0f + 0.5f * static_cast<float>(i);
        brushes.push_back(
            ct::makeBoxBrush({x, -2.0f, 0.0f}, {x + 0.5f, 2.0f, 0.3f * static_cast<float>(i + 1)}));
    }
    brushes.push_back(ct::makeBoxBrush({5.5f, -2.0f, 0.0f}, {10.0f, 2.0f, 1.5f}));
    SimHarness sim(std::move(brushes));
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(walkForward(), kOneSecond);
    CHECK(sim.transform().position.x > 5.5f);
    CHECK(sim.feetZ() == doctest::Approx(1.5f).epsilon(0.01));
    CHECK(sim.onGround());
}

TEST_CASE("a ledge taller than the step height blocks walking") {
    SimHarness sim({floorBrush(), ct::makeBoxBrush({3.0f, -2.0f, 0.0f}, {5.0f, 2.0f, 0.6f})});
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(walkForward(), kOneSecond);
    CHECK(sim.transform().position.x <= 3.0f - sim.movement().halfExtents.x);
    CHECK(sim.feetZ() == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("the player walks up a gentle ramp and stays on the ground") {
    // 4 m long, 1.5 m high: about 20 degrees.
    SimHarness sim({floorBrush(), ct::makeRampBrush({2.0f, -2.0f, 0.0f}, {6.0f, 2.0f, 1.5f}),
                    ct::makeBoxBrush({6.0f, -2.0f, 0.0f}, {10.0f, 2.0f, 1.5f})});
    sim.spawn({0.0f, 0.0f, 0.0f});
    bool leftGround = false;
    for (int i = 0; i < kOneSecond; ++i) {
        sim.run(walkForward(), 1);
        leftGround = leftGround || !sim.onGround();
    }
    CHECK_FALSE(leftGround);
    CHECK(sim.transform().position.x > 6.5f);
    CHECK(sim.feetZ() == doctest::Approx(1.5f).epsilon(0.01));
}

TEST_CASE("a slope steeper than the ground limit cannot be walked up") {
    // 1 m long, 3 m high: about 72 degrees.
    SimHarness sim({floorBrush(), ct::makeRampBrush({2.0f, -2.0f, 0.0f}, {3.0f, 2.0f, 3.0f})});
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(walkForward(), 2 * kOneSecond);
    CHECK(sim.feetZ() < 1.0f);
}

TEST_CASE("walking down stairs keeps the player on the ground") {
    std::vector<ct::Brush> brushes{floorBrush()};
    for (int i = 0; i < 5; ++i) {
        const float x = 0.5f * static_cast<float>(i);
        brushes.push_back(
            ct::makeBoxBrush({x, -2.0f, 0.0f}, {x + 0.5f, 2.0f, 0.3f * static_cast<float>(5 - i)}));
    }
    SimHarness sim(std::move(brushes));
    sim.spawn({0.25f, 0.0f, 1.5f});
    sim.run({}, 1);
    ct::UserCmd cmd = walkForward();
    int airborneTicks = 0;
    for (int i = 0; i < kOneSecond / 2; ++i) {
        sim.run(cmd, 1);
        airborneTicks += sim.onGround() ? 0 : 1;
    }
    CHECK(sim.transform().position.x > 3.0f);
    CHECK(airborneTicks == 0);
}

TEST_CASE("a jump peaks at v^2 / 2g") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run({}, 1);
    ct::UserCmd jump;
    jump.buttons = ct::kButtonJump;
    float peak = 0.0f;
    for (int i = 0; i < kOneSecond; ++i) {
        sim.run(jump, 1);
        peak = std::max(peak, sim.feetZ());
    }
    const float expected =
        sim.movement().jumpSpeed * sim.movement().jumpSpeed / (2.0f * sim.movement().gravity);
    CHECK(peak == doctest::Approx(expected).epsilon(0.01));
}

TEST_CASE("holding jump does not re-jump unless auto-jump is on") {
    ct::UserCmd jump;
    jump.buttons = ct::kButtonJump;

    SimHarness manual({floorBrush()});
    manual.spawn({0.0f, 0.0f, 0.0f});
    manual.run(jump, 2 * kOneSecond);
    CHECK(manual.onGround());

    ct::MovementSettings settings;
    settings.autoJump = true;
    SimHarness automatic({floorBrush()}, settings);
    automatic.spawn({0.0f, 0.0f, 0.0f});
    int airborneTicks = 0;
    for (int i = 0; i < 2 * kOneSecond; ++i) {
        automatic.run(jump, 1);
        airborneTicks += automatic.onGround() ? 0 : 1;
    }
    CHECK(airborneTicks > kOneSecond);
}

TEST_CASE("strafe-jumping gains speed beyond the ground limit") {
    ct::MovementSettings settings;
    settings.autoJump = true;
    SimHarness sim({floorBrush()}, settings);
    sim.spawn({-40.0f, 0.0f, 0.0f});
    sim.run(walkForward(), kOneSecond / 2);

    // Hold forward + right while turning the view steadily right (about 2 degrees a tick),
    // jumping continuously. With the default air settings this gains roughly 2 m/s per second.
    ct::UserCmd cmd;
    cmd.forward = 127;
    cmd.side = 127;
    cmd.buttons = ct::kButtonJump;
    std::uint16_t yaw = 0;
    for (int i = 0; i < 2 * kOneSecond; ++i) {
        yaw = static_cast<std::uint16_t>(yaw - 400);
        cmd.yaw = yaw;
        sim.run(cmd, 1);
    }
    CHECK(horizontalSpeed(sim.motion()) > sim.movement().maxGroundSpeed * 1.2f);
}
