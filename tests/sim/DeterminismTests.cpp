#include "SimHarness.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using ct::test::floorBrush;
using ct::test::SimHarness;

namespace {

std::vector<ct::Brush> testBrushes() {
    return {floorBrush(), ct::makeBoxBrush({8.0f, -10.0f, 0.0f}, {9.0f, 10.0f, 3.0f}),
            ct::makeRampBrush({-6.0f, -3.0f, 0.0f}, {-2.0f, 3.0f, 1.2f}),
            ct::makeBoxBrush({0.0f, 4.0f, 0.0f}, {2.0f, 6.0f, 0.4f})};
}

// Varied input from a fixed-seed generator: turning, strafing, jumping.
ct::UserCmd scriptedCmd(std::uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    ct::UserCmd cmd;
    cmd.yaw = static_cast<std::uint16_t>(seed >> 16);
    cmd.forward = static_cast<std::int8_t>(static_cast<int>((seed >> 8) % 255u) - 127);
    cmd.side = static_cast<std::int8_t>(static_cast<int>((seed >> 4) % 255u) - 127);
    cmd.buttons = (seed & 0x30u) == 0 ? ct::kButtonJump : 0;
    return cmd;
}

std::vector<std::uint64_t> runAndHash(int ticks) {
    SimHarness sim(testBrushes());
    sim.spawn({0.0f, 0.0f, 0.0f});
    std::uint32_t seed = 12345;
    std::vector<std::uint64_t> hashes;
    ct::UserCmd cmd;
    for (int i = 0; i < ticks; ++i) {
        // Hold each command for 16 ticks, like a player holding keys.
        if (i % 16 == 0) {
            cmd = scriptedCmd(seed);
        }
        sim.run(cmd, 1);
        hashes.push_back(ct::hashState(sim.state()));
    }
    return hashes;
}

} // namespace

TEST_CASE("the same inputs produce bit-identical state every tick") {
    const std::vector<std::uint64_t> first = runAndHash(10000);
    const std::vector<std::uint64_t> second = runAndHash(10000);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        if (first[i] != second[i]) {
            FAIL("state diverged at tick " << i + 1);
        }
    }
}

TEST_CASE("the state hash changes when the player moves") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run({}, 1);
    const std::uint64_t before = ct::hashState(sim.state());
    ct::UserCmd cmd;
    cmd.forward = 127;
    sim.run(cmd, 1);
    CHECK(ct::hashState(sim.state()) != before);
}
