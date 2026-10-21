#include "SimHarness.h"

#include "croftech/client/StepSmoothing.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using ct::test::floorBrush;
using ct::test::SimHarness;

namespace {

constexpr int kFramesPerTick = 4;
constexpr float kFrameSeconds = ct::kTickSeconds / kFramesPerTick;

// Eight steps 0.25 m high and 0.5 m deep, rising along +X from x = 2.
std::vector<ct::Brush> stairs() {
    std::vector<ct::Brush> brushes{floorBrush()};
    for (int i = 0; i < 8; ++i) {
        const float x = 2.0f + static_cast<float>(i) * 0.5f;
        brushes.push_back(
            ct::makeBoxBrush({x, -2.0f, 0.0f}, {10.0f, 2.0f, static_cast<float>(i + 1) * 0.25f}));
    }
    return brushes;
}

ct::UserCmd forward() {
    ct::UserCmd cmd;
    cmd.forward = 127;
    return cmd;
}

struct Climb {
    float biggestRawStep = 0.0f;   // the most the eyes moved in one frame
    float biggestDrawnStep = 0.0f; // the most the drawn view did
    float rawZ = 0.0f;
    float drawnZ = 0.0f;
};

// Runs ticks of the command, drawing several frames a tick from the newest state.
void run(SimHarness& sim, ct::StepSmoothing& smoothing, const ct::UserCmd& cmd, int ticks,
         float seconds, Climb& climb, bool& first) {
    const ct::EntityId player = sim.state().world.playerControls.entities()[0];
    for (int tick = 0; tick < ticks; ++tick) {
        sim.run(cmd, 1);
        for (int frame = 0; frame < kFramesPerTick; ++frame) {
            const ct::Vec3 eye = ct::eyePosition(sim.movement(), sim.transform());
            const ct::Vec3 drawn = smoothing.apply(&sim.state(), player, 0, sim.movement(), eye,
                                                   seconds, kFrameSeconds);
            if (!first) {
                climb.biggestRawStep =
                    std::max(climb.biggestRawStep, std::fabs(eye.z - climb.rawZ));
                climb.biggestDrawnStep =
                    std::max(climb.biggestDrawnStep, std::fabs(drawn.z - climb.drawnZ));
            }
            first = false;
            climb.rawZ = eye.z;
            climb.drawnZ = drawn.z;
            CHECK(drawn.x == eye.x);
            CHECK(drawn.y == eye.y);
        }
    }
}

} // namespace

TEST_CASE("the view eases up stairs instead of jumping a step at a time") {
    SimHarness sim(stairs());
    sim.spawn({0.0f, 0.0f, 0.0f});
    ct::StepSmoothing smoothing;
    Climb climb;
    bool first = true;
    run(sim, smoothing, forward(), ct::kTickRate, 0.1f, climb, first);
    REQUIRE(sim.feetZ() > 1.9f);
    CHECK(climb.biggestRawStep > 0.2f);
    CHECK(climb.biggestDrawnStep < 0.05f);
    // Behind while climbing, never more than a step.
    CHECK(climb.drawnZ <= climb.rawZ);
    CHECK(climb.drawnZ > climb.rawZ - sim.movement().stepHeight);

    // Standing at the top, it settles where the eyes are.
    run(sim, smoothing, {}, ct::kTickRate / 2, 0.1f, climb, first);
    CHECK(std::fabs(climb.drawnZ - climb.rawZ) < 0.001f);
}

TEST_CASE("with smoothing off the view follows every step at once") {
    SimHarness sim(stairs());
    sim.spawn({0.0f, 0.0f, 0.0f});
    ct::StepSmoothing smoothing;
    Climb climb;
    bool first = true;
    run(sim, smoothing, forward(), ct::kTickRate, 0.0f, climb, first);
    CHECK(climb.biggestDrawnStep == climb.biggestRawStep);
    CHECK(climb.drawnZ == climb.rawZ);
}

TEST_CASE("jumps and falls are followed at once") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 0.0f});
    ct::StepSmoothing smoothing;
    Climb climb;
    bool first = true;
    ct::UserCmd jump;
    jump.buttons = ct::kButtonJump;
    run(sim, smoothing, jump, ct::kTickRate, 0.1f, climb, first);
    CHECK(climb.biggestDrawnStep == climb.biggestRawStep);
}
