#include "SimHarness.h"

#include "croftech/level/LevelLoad.h"
#include "croftech/script/ScriptFiles.h"
#include "croftech/sim/Enemies.h"
#include "croftech/sim/StateSave.h"

#include <doctest/doctest.h>

#include <ostream> // doctest prints string_view through operator<<
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

ct::LevelEntity script(std::string name, std::string scriptName,
                       std::vector<ct::EntityOutput> outputs = {}) {
    return entity(
        "script",
        {{"origin", "0 0 0"}, {"name", std::move(name)}, {"script", std::move(scriptName)}},
        std::move(outputs));
}

// A counter that only counts, for watching how often something happened.
ct::LevelEntity probe(std::string name) {
    return entity("counter", {{"origin", "0 0 0"}, {"name", std::move(name)}, {"count", "65535"}});
}

std::uint32_t countOf(const SimHarness& sim, std::uint32_t levelEntity) {
    return sim.state().world.counters.get(sim.level().entity(levelEntity)->id)->value;
}

const ct::Script& scriptOf(const SimHarness& sim, std::uint32_t levelEntity) {
    return *sim.state().world.scripts.get(sim.level().entity(levelEntity)->id);
}

ct::Brush owned(ct::Vec3 min, ct::Vec3 max, std::uint32_t entity) {
    ct::Brush brush = ct::makeBoxBrush(min, max);
    brush.entity = entity;
    return brush;
}

// Counts presses, and on the third fires on_done and stops counting.
const ct::ScriptSource kThreePresses{"three_presses", R"(
    return {
        state = { presses = 0, done = false },
        events = { "on_press", "on_done" },
        inputs = {
            press = function(game)
                if game.state.done then return end
                game.state.presses += 1
                game:fire("on_press")
                if game.state.presses == 3 then
                    game.state.done = true
                    game:fire("on_done")
                end
            end,
        },
    }
)"};

} // namespace

TEST_CASE("a script takes inputs from the wiring and fires events into it") {
    // A trigger the player walks in and out of three times presses the script.
    const ct::Level level{
        {floorBrush(), owned({2.0f, -1.0f, 0.0f}, {4.0f, 1.0f, 2.0f}, 0)},
        {entity("trigger", {{"name", "plate"}}, {{"on_enter", "lock", "press", 0.0f}}),
         script("lock", "three_presses",
                {{"on_press", "pressed", "add", 0.0f}, {"on_done", "done", "add", 0.5f}}),
         probe("pressed"), probe("done")}};
    SimHarness sim(level, ct::GameSettings{}, std::span(&kThreePresses, 1));
    REQUIRE(sim.level().warnings().empty());
    sim.spawn({0.0f, 0.0f, 0.0f});
    for (int press = 0; press < 4; ++press) {
        sim.place({3.0f, 0.0f, 0.0f});
        sim.run(ct::UserCmd{}, 2);
        sim.place({0.0f, 0.0f, 0.0f});
        sim.run(ct::UserCmd{}, 2);
    }
    CHECK(countOf(sim, 2) == 3);
    CHECK(countOf(sim, 3) == 0); // on_done waits half a second
    sim.run(ct::UserCmd{}, 64);
    CHECK(countOf(sim, 3) == 1);
    const ct::Script& lock = scriptOf(sim, 1);
    CHECK(lock.values[0] == 1.0); // done
    CHECK(lock.values[1] == 3.0); // presses
    CHECK(lock.calls == 4);
    CHECK(sim.takeScriptMessages().empty());
}

TEST_CASE("a script starts with play, sends inputs by name and calls itself later") {
    const ct::ScriptSource clock{"clock", R"(
        return {
            state = { ticks = 0 },
            start = function(game)
                game:send("hits", "add")
                game:after(0.25, "tick")
            end,
            inputs = {
                tick = function(game)
                    game.state.ticks += 1
                    if game.state.ticks < 4 then game:after(0.25, "tick") end
                    game:send("hits", "add", 0.5)
                end,
            },
        }
    )"};
    const ct::Level level{{floorBrush()}, {script("clock", "clock"), probe("hits")}};
    SimHarness sim(level, ct::GameSettings{}, std::span(&clock, 1));
    REQUIRE(sim.level().warnings().empty());
    sim.run(ct::UserCmd{}, 1);
    CHECK(countOf(sim, 1) == 1);
    sim.run(ct::UserCmd{}, 32);
    CHECK(scriptOf(sim, 0).values[0] == 1.0);
    CHECK(countOf(sim, 1) == 1); // its add arrives half a second later
    sim.run(ct::UserCmd{}, 256);
    CHECK(scriptOf(sim, 0).values[0] == 4.0);
    CHECK(countOf(sim, 1) == 5);
}

TEST_CASE("a script that fails or sends to nothing is reported, and the game goes on") {
    const ct::ScriptSource broken{"broken", R"(
        return {
            state = { n = 0 },
            start = function(game)
                print("starting")
                game:send("nobody", "add")
                game:send("hits", "explode")
            end,
            inputs = {
                fail = function(game) game.state.n = 1; error("no good") end,
            },
        }
    )"};
    const ct::Level level{
        {floorBrush()},
        {script("broken", "broken"), probe("hits"),
         entity("timer", {{"origin", "0 0 0"}, {"interval", "0.1"}, {"start_on", "1"}},
                {{"on_timer", "broken", "fail", 0.0f}})}};
    SimHarness sim(level, ct::GameSettings{}, std::span(&broken, 1));
    sim.run(ct::UserCmd{}, 20);
    const std::vector<ct::ScriptMessage> messages = sim.takeScriptMessages();
    REQUIRE(messages.size() == 4);
    CHECK_FALSE(messages[0].error);
    CHECK(messages[0].text == "script 'broken' (entity 0): starting");
    CHECK(messages[1].text == "script 'broken' (entity 0): game:send: no entity is named 'nobody'");
    CHECK(messages[2].text ==
          "script 'broken' (entity 0): game:send: counter 'hits' (entity 1) has no input "
          "'explode'");
    CHECK(messages[3].error);
    CHECK(messages[3].text == "script 'broken' (entity 0): broken:10: no good");
    CHECK(scriptOf(sim, 0).values[0] == 0.0);
    CHECK(sim.takeScriptMessages().empty());
}

TEST_CASE("wiring to a script is checked against what it declares") {
    const ct::ScriptSource bad{"bad", "return { state = { x = math.cos(0) } }"};
    const ct::Level level{
        {floorBrush()},
        {script("lock", "three_presses", {{"on_open", "hits", "add", 0.0f}}),
         entity("relay", {{"origin", "0 0 0"}, {"name", "relay"}},
                {{"on_trigger", "lock", "unlock", 0.0f}, {"on_trigger", "lock", "press", 0.0f}}),
         probe("hits"), script("broken", "bad"), script("missing", "nowhere"),
         script("unnamed", "")}};
    const ct::ScriptSource sources[] = {kThreePresses, bad};
    SimHarness sim(level, ct::GameSettings{}, sources);
    const std::span<const std::string> warnings = sim.level().warnings();
    REQUIRE(warnings.size() == 6);
    CHECK(warnings[0].starts_with("scripts/bad.luau: bad:1: math.cos isn't available"));
    CHECK(warnings[1] == "script 'broken' (entity 3): script 'bad' isn't loaded "
                         "(scripts/bad.luau)");
    CHECK(warnings[2] == "script 'missing' (entity 4): script 'nowhere' isn't loaded "
                         "(scripts/nowhere.luau)");
    CHECK(warnings[3] == "script 'unnamed' (entity 5): names no script");
    CHECK(warnings[4] == "script 'lock' (entity 0) has no event 'on_open'");
    CHECK(warnings[5] ==
          "relay 'relay' (entity 1) on_trigger: script 'lock' (entity 0) has no input 'unlock'");
    // The output that works still works.
    CHECK(sim.level().entity(1)->outputs[1].targets.size() == 1);
}

TEST_CASE("script state saves and loads mid-call-chain, and carries on identically") {
    const ct::ScriptSource dice{"dice", R"(
        return {
            state = { total = 0, rolls = 0 },
            start = function(game) game:after(0.1, "roll") end,
            inputs = {
                roll = function(game)
                    game.state.total += game:random(1, 6)
                    game.state.rolls += 1
                    game:after(0.1 * game:random(1, 3), "roll")
                end,
            },
        }
    )"};
    const ct::Level level{{floorBrush()}, {script("dice", "dice")}};
    SimHarness sim(level, ct::GameSettings{}, std::span(&dice, 1));
    sim.run(ct::UserCmd{}, 200);
    REQUIRE(sim.state().world.pendingInputs.size() == 1);

    SimHarness loaded(level, ct::GameSettings{}, std::span(&dice, 1));
    std::string error;
    REQUIRE_MESSAGE(ct::loadState(ct::saveState(sim.state()), loaded.state(), error), error);
    for (int i = 0; i < 500; ++i) {
        sim.run(ct::UserCmd{}, 1);
        loaded.run(ct::UserCmd{}, 1);
        REQUIRE(ct::hashState(sim.state()) == ct::hashState(loaded.state()));
    }
    const ct::Script& rolled = scriptOf(sim, 0);
    CHECK(rolled.values[0] > 20.0); // rolls, sorted first
    CHECK(rolled.values[1] >= rolled.values[0]);
    CHECK(rolled.values[1] <= 6.0 * rolled.values[0]);
}

TEST_CASE("the script test level's vault opens to the plates in order, and a wrong one sends a "
          "guard") {
    std::vector<std::string> warnings;
    std::string error;
    const std::optional<ct::Level> level =
        ct::loadAnyLevelFile(CROFTECH_CONTENT_DIR "/levels/script_test.level", warnings, error);
    REQUIRE_MESSAGE(level, error);
    ct::ContentPaths content;
    content.addLayer(CROFTECH_CONTENT_DIR);
    const std::vector<ct::ScriptSource> scripts = ct::loadLevelScripts(content, *level, warnings);
    ct::GameSettings settings;
    ct::EnemySettings charger;
    charger.name = "charger";
    charger.movement = ct::enemyMovement(charger, settings.movement);
    settings.enemies = {charger};
    SimHarness sim(*level, settings, scripts);
    const auto named = [&](std::string_view name) {
        const std::span<const ct::GameLevel::NamedEntity> found = sim.level().entitiesNamed(name);
        REQUIRE(found.size() == 1);
        return found[0].index;
    };
    const auto vault = [&] {
        return ct::MoverPhase(
            sim.state().world.movers.get(sim.level().entity(named("vault"))->id)->phase);
    };
    const auto step = [&](float x) {
        sim.place({x, 0.0f, 0.1f});
        sim.run({}, 4);
        sim.place({x, -4.0f, 0.0f});
        sim.run({}, 4);
    };
    sim.spawn({0.0f, -6.0f, 0.0f}, 16384);
    sim.run({}, 1);
    const std::vector<ct::ScriptMessage> hello = sim.takeScriptMessages();
    REQUIRE(hello.size() == 1);
    CHECK(hello[0].text.find("left, right, middle") != std::string::npos);

    // Left, then middle: wrong, and a guard comes half a second later.
    step(-5.0f);
    step(0.0f);
    CHECK(sim.state().world.enemies.size() == 0);
    sim.run({}, 64);
    CHECK(sim.state().world.enemies.size() == 1);
    CHECK(vault() == ct::MoverPhase::AtStart);

    // Left, right, middle: open.
    step(-5.0f);
    step(5.0f);
    step(0.0f);
    CHECK(vault() == ct::MoverPhase::ToEnd);

    // A plate after that changes nothing.
    step(5.0f);
    sim.run({}, 64);
    CHECK(sim.state().world.enemies.size() == 1);
}

TEST_CASE("the combination starts again if the plates take too long") {
    std::vector<std::string> warnings;
    std::string error;
    const std::optional<ct::Level> level =
        ct::loadAnyLevelFile(CROFTECH_CONTENT_DIR "/levels/script_test.level", warnings, error);
    REQUIRE_MESSAGE(level, error);
    ct::ContentPaths content;
    content.addLayer(CROFTECH_CONTENT_DIR);
    const std::vector<ct::ScriptSource> scripts = ct::loadLevelScripts(content, *level, warnings);
    SimHarness sim(*level, ct::GameSettings{}, scripts);
    sim.spawn({0.0f, -6.0f, 0.0f}, 16384);
    sim.run({}, 1);
    sim.takeScriptMessages();
    sim.place({-5.0f, 0.0f, 0.1f});
    sim.run({}, 4);
    sim.place({-5.0f, -4.0f, 0.0f});
    sim.run({}, 10 * 128);
    const std::vector<ct::ScriptMessage> messages = sim.takeScriptMessages();
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].text == "script 'lock' (entity 7): too slow: start again");
}
