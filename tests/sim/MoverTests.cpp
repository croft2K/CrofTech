#include "SimHarness.h"

#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/StateSave.h"

#include <doctest/doctest.h>

#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

using ct::test::floorBrush;
using ct::test::SimHarness;

namespace {

ct::LevelEntity entity(std::string className, std::vector<ct::EntityProperty> properties,
                       std::vector<ct::EntityOutput> outputs = {}) {
    return {std::move(className), std::move(properties), std::move(outputs)};
}

ct::LevelEntity probe(std::string name) {
    return entity("counter", {{"origin", "0 0 0"}, {"name", std::move(name)}, {"count", "65535"}});
}

// A timer that sends one input to the target once, at the tick given.
ct::LevelEntity sendAt(int tick, std::string target, std::string input) {
    const float seconds = static_cast<float>(tick) / static_cast<float>(ct::kTickRate);
    return entity("timer",
                  {{"origin", "0 0 0"},
                   {"interval", std::format("{}", seconds)},
                   {"repeat", "0"},
                   {"start_on", "1"}},
                  {{"on_timer", std::move(target), std::move(input), 0.0f}});
}

ct::Brush owned(ct::Vec3 min, ct::Vec3 max, std::uint32_t entity) {
    ct::Brush brush = ct::makeBoxBrush(min, max);
    brush.entity = entity;
    return brush;
}

const ct::Mover& moverOf(const SimHarness& sim, std::uint32_t levelEntity) {
    return *sim.state().world.movers.get(sim.level().entity(levelEntity)->id);
}

std::uint32_t countOf(const SimHarness& sim, std::uint32_t levelEntity) {
    return sim.state().world.counters.get(sim.level().entity(levelEntity)->id)->value;
}

bool insideSolid(const SimHarness& sim) {
    const ct::CollisionScene scene(sim.level(), sim.state().world);
    return scene.overlapsBox(sim.transform().position, sim.movement().halfExtents, 1e-4f);
}

ct::UserCmd walk(std::uint16_t yaw) {
    ct::UserCmd cmd;
    cmd.yaw = yaw;
    cmd.forward = 127;
    return cmd;
}

constexpr std::uint16_t kFacingY = 16384;

// A door in a wall across the room at y = 2..2.5, 2 m wide and 3 m tall; the doorway's lintel
// is above it.
ct::Level doorLevel(std::vector<ct::EntityProperty> doorProperties,
                    std::vector<ct::EntityOutput> doorOutputs = {}) {
    doorProperties.push_back({"name", "door"});
    doorProperties.push_back({"move", "0 0 3"});
    return {{floorBrush(), ct::makeBoxBrush({-8.0f, 2.0f, 0.0f}, {-1.0f, 2.5f, 4.0f}),
             ct::makeBoxBrush({1.0f, 2.0f, 0.0f}, {8.0f, 2.5f, 4.0f}),
             ct::makeBoxBrush({-1.0f, 2.0f, 3.0f}, {1.0f, 2.5f, 4.0f}),
             owned({-1.0f, 2.125f, 0.0f}, {1.0f, 2.375f, 3.0f}, 0)},
            {entity("mover", std::move(doorProperties), std::move(doorOutputs))}};
}

} // namespace

TEST_CASE("a mover travels to its end at its speed, then comes back after its wait") {
    ct::Level level = doorLevel({{"speed", "3"}, {"wait", "1"}},
                                {{"on_leave_start", "left_start", "add", 0.0f},
                                 {"on_reached_end", "reached_end", "add", 0.0f},
                                 {"on_leave_end", "left_end", "add", 0.0f},
                                 {"on_reached_start", "reached_start", "add", 0.0f}});
    level.entities.push_back(sendAt(1, "door", "move_to_end"));
    for (const char* name : {"left_start", "reached_end", "left_end", "reached_start"}) {
        level.entities.push_back(probe(name));
    }
    SimHarness sim(level);
    CHECK(sim.level().warnings().empty());

    sim.run(ct::UserCmd{}, 1);
    CHECK(countOf(sim, 2) == 1);
    CHECK(moverOf(sim, 0).offset.z == doctest::Approx(3.0f / 128.0f));
    sim.run(ct::UserCmd{}, 127);
    CHECK(moverOf(sim, 0).offset == ct::Vec3{0.0f, 0.0f, 3.0f});
    CHECK(countOf(sim, 3) == 1);

    // A second's wait, then a second back.
    sim.run(ct::UserCmd{}, 127);
    CHECK(countOf(sim, 4) == 0);
    sim.run(ct::UserCmd{}, 1);
    CHECK(countOf(sim, 4) == 1);
    sim.run(ct::UserCmd{}, 127);
    CHECK(moverOf(sim, 0).offset == ct::Vec3{});
    CHECK(countOf(sim, 5) == 1);
    CHECK(moverOf(sim, 0).phase == static_cast<std::uint32_t>(ct::MoverPhase::AtStart));
}

TEST_CASE("a door opens when a player walks into it, lets them through and shuts behind them") {
    SimHarness sim(doorLevel({{"activate", "touch"}, {"speed", "3"}, {"wait", "1"}}));
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(walk(kFacingY), 2 * ct::kTickRate);
    CHECK(sim.transform().position.y > 4.0f);
    CHECK_FALSE(insideSolid(sim));
    sim.run(ct::UserCmd{}, 3 * ct::kTickRate);
    CHECK(moverOf(sim, 0).offset == ct::Vec3{});
}

TEST_CASE("a closing door that meets a player goes back up instead of crushing them") {
    ct::Level level = doorLevel({{"start_at_end", "1"}, {"speed", "3"}, {"wait", "-1"}},
                                {{"on_reached_start", "shut", "add", 0.0f}});
    level.entities.push_back(sendAt(1, "door", "move_to_start"));
    level.entities.push_back(probe("shut"));
    SimHarness sim(level);
    sim.spawn({0.0f, 2.25f, 0.0f}); // standing in the doorway
    sim.run(ct::UserCmd{}, ct::kTickRate);
    CHECK(countOf(sim, 2) == 0);
    CHECK(moverOf(sim, 0).offset.z > 1.7f);
    CHECK(sim.feetZ() == doctest::Approx(0.0f).epsilon(0.01));
    CHECK_FALSE(insideSolid(sim));
    // Without a wait it stays open once it has gone back.
    CHECK(moverOf(sim, 0).phase == static_cast<std::uint32_t>(ct::MoverPhase::AtEnd));
}

TEST_CASE("a lift rises with the player standing on it, waits while they stay, then comes down") {
    const ct::Level level{
        {floorBrush(), owned({-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.25f}, 0)},
        {entity("mover",
                {{"move", "0 0 4"}, {"activate", "stand"}, {"speed", "2"}, {"wait", "1"}})}};
    SimHarness sim(level);
    sim.spawn({0.0f, 0.0f, 0.25f});
    sim.run(ct::UserCmd{}, 1);
    for (std::uint32_t i = 0; i < 3 * ct::kTickRate; ++i) {
        sim.run(ct::UserCmd{}, 1);
        REQUIRE(sim.onGround());
        REQUIRE(sim.feetZ() == doctest::Approx(0.25f + moverOf(sim, 0).offset.z).epsilon(0.001));
    }
    CHECK(moverOf(sim, 0).offset == ct::Vec3{0.0f, 0.0f, 4.0f});

    // Off the side, a fall to the floor; a second later the lift heads back down.
    sim.run(walk(0), ct::kTickRate / 2);
    sim.run(ct::UserCmd{}, ct::kTickRate);
    CHECK(sim.feetZ() == doctest::Approx(0.0f).epsilon(0.01));
    sim.run(ct::UserCmd{}, 3 * ct::kTickRate);
    CHECK(moverOf(sim, 0).offset == ct::Vec3{});
}

TEST_CASE("a lift going down carries its rider down without them leaving the ground") {
    ct::Level level{{floorBrush(), owned({-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.25f}, 0)},
                    {entity("mover", {{"name", "lift"},
                                      {"move", "0 0 4"},
                                      {"start_at_end", "1"},
                                      {"speed", "4"},
                                      {"wait", "-1"}})}};
    level.entities.push_back(sendAt(32, "lift", "move_to_start"));
    SimHarness sim(level);
    sim.spawn({0.0f, 0.0f, 4.25f});
    sim.run(ct::UserCmd{}, 2 * ct::kTickRate);
    CHECK(moverOf(sim, 0).offset == ct::Vec3{});
    CHECK(sim.feetZ() == doctest::Approx(0.25f).epsilon(0.001));
    CHECK(sim.onGround());
}

TEST_CASE("a platform carries a player standing on it back and forth") {
    const ct::Level level{{floorBrush(), owned({-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.25f}, 0)},
                          {entity("mover", {{"move", "6 0 0"},
                                            {"speed", "2"},
                                            {"wait", "1"},
                                            {"loop", "1"},
                                            {"start_moving", "1"}})}};
    SimHarness sim(level);
    sim.spawn({0.0f, 0.0f, 0.25f});
    sim.run(ct::UserCmd{}, 3 * ct::kTickRate);
    CHECK(moverOf(sim, 0).offset.x == doctest::Approx(6.0f));
    CHECK(sim.transform().position.x == doctest::Approx(6.0f).epsilon(0.01));
    // A second's wait at each end, and three seconds each way.
    sim.run(ct::UserCmd{}, 4 * ct::kTickRate);
    CHECK(moverOf(sim, 0).offset.x == doctest::Approx(0.0f));
    CHECK(sim.transform().position.x == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(sim.onGround());
}

TEST_CASE("a mover pushes players out of its way, and stops when they have nowhere to go") {
    const auto pusher = [](bool wall) {
        ct::Level level{
            {floorBrush(), owned({-4.0f, -1.0f, 0.0f}, {-2.0f, 1.0f, 2.0f}, 0)},
            {entity("mover",
                    {{"move", "8 0 0"}, {"speed", "2"}, {"start_moving", "1"}, {"wait", "-1"}})}};
        if (wall) {
            level.brushes.push_back(ct::makeBoxBrush({3.0f, -4.0f, 0.0f}, {4.0f, 4.0f, 3.0f}));
        }
        return level;
    };
    SimHarness open(pusher(false));
    open.spawn({0.0f, 0.0f, 0.0f});
    open.run(ct::UserCmd{}, 3 * ct::kTickRate);
    CHECK(open.transform().position.x > 4.0f);
    CHECK_FALSE(insideSolid(open));

    SimHarness walled(pusher(true));
    walled.spawn({0.0f, 0.0f, 0.0f});
    walled.run(ct::UserCmd{}, 3 * ct::kTickRate);
    CHECK_FALSE(insideSolid(walled));
    CHECK(walled.transform().position.x < 3.0f - 0.4f);
    // Its front is at x = -2 as built, so it stopped about a player's width short of the wall.
    CHECK(moverOf(walled, 0).offset.x == doctest::Approx(4.2f).epsilon(0.01));
    CHECK(moverOf(walled, 0).phase == static_cast<std::uint32_t>(ct::MoverPhase::ToEnd));
}

TEST_CASE("stop holds a mover where it is until resume") {
    ct::Level level{
        {floorBrush(), owned({-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.25f}, 0)},
        {entity("mover",
                {{"name", "platform"}, {"move", "6 0 0"}, {"speed", "2"}, {"start_moving", "1"}})}};
    level.entities.push_back(sendAt(64, "platform", "stop"));
    level.entities.push_back(sendAt(192, "platform", "resume"));
    SimHarness sim(level);
    sim.run(ct::UserCmd{}, 64);
    const float stoppedAt = moverOf(sim, 0).offset.x;
    CHECK(stoppedAt == doctest::Approx(63.0f * 2.0f / 128.0f));
    sim.run(ct::UserCmd{}, 127);
    CHECK(moverOf(sim, 0).offset.x == stoppedAt);
    sim.run(ct::UserCmd{}, 2);
    CHECK(moverOf(sim, 0).offset.x > stoppedAt);
}

TEST_CASE("movers and their riders save and load mid-move and carry on identically") {
    const ct::Level level{
        {floorBrush(), owned({-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.25f}, 0)},
        {entity("mover",
                {{"move", "0 0 3"}, {"activate", "stand"}, {"speed", "2"}, {"wait", "0.5"}})}};
    SimHarness sim(level);
    sim.spawn({0.0f, 0.0f, 0.25f});
    sim.run(ct::UserCmd{}, 100);

    SimHarness loaded(level);
    std::string error;
    REQUIRE_MESSAGE(ct::loadState(ct::saveState(sim.state()), loaded.state(), error), error);
    ct::UserCmd jumpAround = walk(0);
    jumpAround.buttons = ct::kButtonJump;
    for (int i = 0; i < 400; ++i) {
        const ct::UserCmd cmd = i < 200 ? ct::UserCmd{} : jumpAround;
        sim.run(cmd, 1);
        loaded.run(cmd, 1);
        REQUIRE(ct::hashState(sim.state()) == ct::hashState(loaded.state()));
    }
}
