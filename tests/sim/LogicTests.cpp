#include "SimHarness.h"

#include "croftech/sim/StateSave.h"

#include <doctest/doctest.h>

#include <memory>
#include <ostream> // doctest prints string_view through operator<<
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ct::test::floorBrush;
using ct::test::SimHarness;

namespace {

ct::LevelEntity entity(std::string className, std::vector<ct::EntityProperty> properties,
                       std::vector<ct::EntityOutput> outputs = {}) {
    return {std::move(className), std::move(properties), std::move(outputs)};
}

// A counter that only counts, for watching how often something happened.
ct::LevelEntity probe(std::string name) {
    return entity("counter", {{"origin", "0 0 0"}, {"name", std::move(name)}, {"count", "65535"}});
}

ct::Brush owned(ct::Vec3 min, ct::Vec3 max, std::uint32_t entity) {
    ct::Brush brush = ct::makeBoxBrush(min, max);
    brush.entity = entity;
    return brush;
}

ct::EntityId idOf(const SimHarness& sim, std::uint32_t levelEntity) {
    return sim.level().entity(levelEntity)->id;
}

std::uint32_t countOf(const SimHarness& sim, std::uint32_t levelEntity) {
    return sim.state().world.counters.get(idOf(sim, levelEntity))->value;
}

ct::UserCmd forward() {
    ct::UserCmd cmd;
    cmd.forward = 127;
    return cmd;
}

// A trigger between x = 2 and 4 on the floor, telling probe 1 when players go in and probe 2
// when they come out.
ct::Level triggerLevel(std::vector<ct::EntityProperty> triggerProperties = {}) {
    triggerProperties.push_back({"name", "zone"});
    return {{floorBrush(), owned({2.0f, -1.0f, 0.0f}, {4.0f, 1.0f, 2.0f}, 0)},
            {entity("trigger", std::move(triggerProperties),
                    {{"on_enter", "entered", "add", 0.0f}, {"on_exit", "left", "add", 0.0f}}),
             probe("entered"), probe("left")}};
}

} // namespace

TEST_CASE("players walk through triggers, which see them go in and come out") {
    SimHarness sim(triggerLevel());
    CHECK(sim.level().warnings().empty());
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(forward(), ct::kTickRate);
    CHECK(sim.transform().position.x > 6.0f);
    CHECK(countOf(sim, 1) == 1);
    CHECK(countOf(sim, 2) == 1);
    CHECK(sim.state().world.triggers.get(idOf(sim, 0))->playersInside == 0);
}

TEST_CASE("each player going in fires on_enter, and a once trigger fires only the first time") {
    SimHarness sim(triggerLevel());
    sim.spawn({3.0f, 0.0f, 0.0f}, 0, 0);
    sim.spawn({3.0f, 0.5f, 0.0f}, 0, 1);
    sim.run(ct::UserCmd{}, 1);
    CHECK(countOf(sim, 1) == 2);
    CHECK(sim.state().world.triggers.get(idOf(sim, 0))->playersInside == 0b11);

    SimHarness once(triggerLevel({{"once", "1"}}));
    once.spawn({3.0f, 0.0f, 0.0f});
    for (int i = 0; i < 3; ++i) {
        once.place({3.0f, 0.0f, 0.0f});
        once.run(ct::UserCmd{}, 1);
        once.place({8.0f, 0.0f, 0.0f});
        once.run(ct::UserCmd{}, 1);
    }
    CHECK(countOf(once, 1) == 1);
    CHECK(countOf(once, 2) == 0); // switched off as the player went in, so it saw nobody leave
}

TEST_CASE("a trigger that starts switched off fires once an input switches it on") {
    ct::Level level = triggerLevel({{"start_disabled", "1"}});
    // A timer switches it on after half a second.
    level.entities.push_back(entity(
        "timer", {{"origin", "0 0 0"}, {"interval", "0.5"}, {"repeat", "0"}, {"start_on", "1"}},
        {{"on_timer", "zone", "enable", 0.0f}}));
    SimHarness sim(level);
    sim.spawn({3.0f, 0.0f, 0.0f});
    sim.run(ct::UserCmd{}, 63);
    CHECK(countOf(sim, 1) == 0);
    sim.run(ct::UserCmd{}, 1);
    CHECK(countOf(sim, 1) == 1); // the player was already inside
}

TEST_CASE("relays pass inputs on only while switched on, and outputs go in the order listed") {
    // A timer firing every tick tells the relay, then a counter that switches the relay off on
    // its tenth.
    const ct::Level level{
        {floorBrush()},
        {entity("timer", {{"origin", "0 0 0"}, {"interval", "0.0078125"}, {"start_on", "1"}},
                {{"on_timer", "relay", "trigger", 0.0f}, {"on_timer", "ten", "add", 0.0f}}),
         entity("relay", {{"origin", "0 0 0"}, {"name", "relay"}},
                {{"on_trigger", "passed", "add", 0.0f}}),
         entity("counter", {{"origin", "0 0 0"}, {"name", "ten"}, {"count", "10"}},
                {{"on_reached", "relay", "disable", 0.0f}}),
         probe("passed")}};
    SimHarness sim(level);
    CHECK(sim.level().warnings().empty());
    sim.run(ct::UserCmd{}, 30);
    CHECK(countOf(sim, 3) == 10);
    CHECK((sim.state().world.relays.get(idOf(sim, 1))->flags & ct::kLogicEnabled) == 0);
}

TEST_CASE("timers fire every interval, or once") {
    const auto timerLevel = [](std::string_view repeat) {
        return ct::Level{{floorBrush()},
                         {entity("timer",
                                 {{"origin", "0 0 0"},
                                  {"interval", "0.5"},
                                  {"repeat", std::string(repeat)},
                                  {"start_on", "1"}},
                                 {{"on_timer", "fired", "add", 0.0f}}),
                          probe("fired")}};
    };
    SimHarness repeating(timerLevel("1"));
    repeating.run(ct::UserCmd{}, 63);
    CHECK(countOf(repeating, 1) == 0);
    repeating.run(ct::UserCmd{}, 1);
    CHECK(countOf(repeating, 1) == 1);
    repeating.run(ct::UserCmd{}, 4 * 64);
    CHECK(countOf(repeating, 1) == 5);

    SimHarness once(timerLevel("0"));
    once.run(ct::UserCmd{}, 5 * 64);
    CHECK(countOf(once, 1) == 1);
}

TEST_CASE("delayed outputs wait as entities and arrive on the tick, in the order sent") {
    // At tick 1 the timer sends add then reset to a counter of two, to arrive a second later: in
    // that order it ends at 0, the other way round at 1. A third output adds straight away.
    const ct::Level level{
        {floorBrush()},
        {entity(
             "timer",
             {{"origin", "0 0 0"}, {"interval", "0.0078125"}, {"repeat", "0"}, {"start_on", "1"}},
             {{"on_timer", "two", "add", 1.0f},
              {"on_timer", "two", "reset", 1.0f},
              {"on_timer", "now", "add", 0.0f}}),
         entity("counter", {{"origin", "0 0 0"}, {"name", "two"}, {"count", "2"}}), probe("now")}};
    SimHarness sim(level);
    sim.run(ct::UserCmd{}, 1);
    CHECK(countOf(sim, 2) == 1);
    CHECK(sim.state().world.pendingInputs.size() == 2);
    sim.run(ct::UserCmd{}, ct::kTickRate - 1);
    CHECK(sim.state().world.pendingInputs.size() == 2);
    sim.run(ct::UserCmd{}, 1);
    CHECK(sim.state().world.pendingInputs.size() == 0);
    CHECK(countOf(sim, 1) == 0);
    CHECK(sim.state().world.entities.isAlive(idOf(sim, 0)));
}

TEST_CASE("wiring that loops with no delay stops at the per-tick limit") {
    const ct::Level level{
        {floorBrush()},
        {entity(
             "timer",
             {{"origin", "0 0 0"}, {"interval", "0.0078125"}, {"repeat", "0"}, {"start_on", "1"}},
             {{"on_timer", "loop", "trigger", 0.0f}}),
         entity("relay", {{"origin", "0 0 0"}, {"name", "loop"}},
                {{"on_trigger", "loop", "trigger", 0.0f}, {"on_trigger", "seen", "add", 0.0f}}),
         probe("seen")}};
    SimHarness sim(level);
    sim.run(ct::UserCmd{}, 1);
    const std::uint32_t seen = countOf(sim, 2);
    CHECK(seen > 1000);
    CHECK(seen < ct::kMaxInputDeliveriesPerTick);
    sim.run(ct::UserCmd{}, 5);
    CHECK(countOf(sim, 2) == seen); // the loop ended with the tick
}

TEST_CASE("wiring that can't work is reported and skipped") {
    const ct::Level level{{floorBrush(), owned({2.0f, -1.0f, 0.0f}, {4.0f, 1.0f, 2.0f}, 0)},
                          {entity("trigger", {},
                                  {{"on_enter", "nobody", "add", 0.0f},
                                   {"on_enter", "relay", "explode", 0.0f},
                                   {"on_fire", "relay", "trigger", 0.0f},
                                   {"on_enter", "relay", "trigger", 0.0f}}),
                           entity("relay", {{"origin", "0 0 0"}, {"name", "relay"}}),
                           entity("counter", {{"origin", "0 0 0"}, {"count", "0"}}),
                           entity("trigger", {})}};
    SimHarness sim(level);
    const auto warnings = sim.level().warnings();
    REQUIRE(warnings.size() == 5);
    CHECK(warnings[0] == "counter (entity 2): count must be a whole number from 1 to 65535");
    CHECK(warnings[1] == "trigger (entity 3) has no brushes");
    CHECK(warnings[2] == "trigger (entity 0) on_enter: no entity is named 'nobody'");
    CHECK(warnings[3] ==
          "trigger (entity 0) on_enter: relay 'relay' (entity 1) has no input 'explode'");
    CHECK(warnings[4] == "trigger (entity 0) has no event 'on_fire'");
    // The output that works still works.
    REQUIRE(sim.level().entity(0)->outputs.size() == 3);
    CHECK(sim.level().entity(0)->outputs[2].targets.size() == 1);
}

TEST_CASE("logic state saves and loads mid-delay, and carries on identically") {
    const ct::Level level{
        {floorBrush()},
        {entity("timer", {{"origin", "0 0 0"}, {"interval", "0.25"}, {"start_on", "1"}},
                {{"on_timer", "count", "add", 0.4f}}),
         probe("count")}};
    SimHarness sim(level);
    sim.run(ct::UserCmd{}, 100);
    REQUIRE(sim.state().world.pendingInputs.size() > 0);

    SimHarness loaded(level);
    std::string error;
    REQUIRE_MESSAGE(ct::loadState(ct::saveState(sim.state()), loaded.state(), error), error);
    for (int i = 0; i < 300; ++i) {
        sim.run(ct::UserCmd{}, 1);
        loaded.run(ct::UserCmd{}, 1);
        REQUIRE(ct::hashState(sim.state()) == ct::hashState(loaded.state()));
    }
    CHECK(countOf(sim, 1) == 10); // fired every 32 ticks, arriving 51 ticks later
}
