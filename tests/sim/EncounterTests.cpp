#include "SimHarness.h"

#include "croftech/level/EntityClasses.h"
#include "croftech/level/LevelLoad.h"
#include "croftech/sim/Damage.h"
#include "croftech/sim/Enemies.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using ct::test::SimHarness;

namespace {

constexpr int kSecond = static_cast<int>(ct::kTickRate);

// A 30 x 30 m floor whose top is at z = 0: small, so its navigation mesh is quick to build.
ct::Brush arenaFloor() { return ct::makeBoxBrush({-15.0f, -15.0f, -1.0f}, {15.0f, 15.0f, 0.0f}); }

ct::LevelEntity entity(std::string className, std::vector<ct::EntityProperty> properties,
                       std::vector<ct::EntityOutput> outputs = {}) {
    return {std::move(className), std::move(properties), std::move(outputs)};
}

ct::LevelEntity counter(std::string name) {
    return entity("counter", {{"origin", "0 0 0"}, {"name", std::move(name)}, {"count", "65535"}});
}

// The encounter "arena", counting its starts, waves and clearing on the counters "started",
// "waves" and "cleared", with any more properties given.
ct::LevelEntity arena(std::vector<ct::EntityProperty> more = {}) {
    std::vector<ct::EntityProperty> properties{{"origin", "0 0 0"}, {"name", "arena"}};
    for (ct::EntityProperty& property : more) {
        properties.push_back(std::move(property));
    }
    return entity(std::string(ct::kEncounterClass), std::move(properties),
                  {{"on_start", "started", "add", 0.0f},
                   {"on_wave", "waves", "add", 0.0f},
                   {"on_cleared", "cleared", "add", 0.0f}});
}

ct::LevelEntity spawnerAt(float x, float y, std::vector<ct::EntityProperty> more,
                          std::vector<ct::EntityOutput> outputs = {}) {
    std::vector<ct::EntityProperty> properties{{"origin", std::format("{} {} 0", x, y)}};
    for (ct::EntityProperty& property : more) {
        properties.push_back(std::move(property));
    }
    return entity(std::string(ct::kSpawnerClass), std::move(properties), std::move(outputs));
}

// A timer that fires once, at the tick given, sending the input to the target.
ct::LevelEntity sendAt(int tick, std::string target, std::string input) {
    const float seconds = static_cast<float>(tick) / static_cast<float>(ct::kTickRate);
    return entity("timer",
                  {{"origin", "0 0 0"},
                   {"interval", std::format("{}", seconds)},
                   {"repeat", "0"},
                   {"start_on", "1"}},
                  {{"on_timer", std::move(target), std::move(input), 0.0f}});
}

// The encounter (level entity 0), its three counters (1 to 3), then the rest.
ct::Level arenaLevel(ct::LevelEntity encounter, std::vector<ct::LevelEntity> more) {
    std::vector<ct::LevelEntity> entities{std::move(encounter), counter("started"),
                                          counter("waves"), counter("cleared")};
    for (ct::LevelEntity& other : more) {
        entities.push_back(std::move(other));
    }
    return {{arenaFloor()}, std::move(entities)};
}

// A charger with no attack, so the player outlives every test.
ct::GameSettings withCharger() {
    ct::GameSettings settings;
    ct::EnemySettings charger;
    charger.name = "charger";
    charger.reactionTime = 0.1f;
    charger.corpseTime = 0.5f;
    charger.movement = ct::enemyMovement(charger, settings.movement);
    settings.enemies = {charger};
    return settings;
}

std::uint32_t countOf(const SimHarness& sim, std::uint32_t levelEntity) {
    return sim.state().world.counters.get(sim.level().entity(levelEntity)->id)->value;
}

// The living enemies spawners sent.
std::vector<ct::EntityId> sentAlive(const SimHarness& sim) {
    std::vector<ct::EntityId> sent;
    const ct::World& world = sim.state().world;
    for (std::size_t i = 0; i < world.enemies.size(); ++i) {
        const ct::EntityId id = world.enemies.entities()[i];
        if (world.enemies.components()[i].spawner != ct::kNoEntity &&
            !ct::isDead(*world.healths.get(id))) {
            sent.push_back(id);
        }
    }
    return sent;
}

void kill(SimHarness& sim, std::size_t howMany = 1000) {
    std::vector<ct::EntityId> sent = sentAlive(sim);
    sent.resize(std::min(sent.size(), howMany));
    for (const ct::EntityId id : sent) {
        ct::kill(sim.state(), id, ct::DamageKind::Generic);
    }
}

ct::EncounterPhase phaseOf(const SimHarness& sim) {
    return ct::encounterPhase(*sim.state().world.encounters.get(sim.level().entity(0)->id));
}

} // namespace

TEST_CASE("an encounter sends its waves in turn, and is cleared when the last is killed") {
    SimHarness sim(
        arenaLevel(
            arena({{"wave_delay", "0.5"}}),
            {sendAt(2, "arena", "start"), spawnerAt(-5.0f, 0.0f, {{"encounter", "arena"}}),
             spawnerAt(5.0f, 0.0f, {{"encounter", "arena"}}),
             // A gap in the numbers is no wave: this is the second.
             spawnerAt(0.0f, -5.0f, {{"encounter", "arena"}, {"wave", "3"}, {"count", "2"}})}),
        withCharger());
    CHECK(sim.level().warnings().empty());
    const ct::EntityId player = sim.spawn({12.0f, 12.0f, 0.0f});
    sim.run({}, 1);
    CHECK(phaseOf(sim) == ct::EncounterPhase::Waiting);
    sim.run({}, kSecond / 4);
    CHECK(countOf(sim, 1) == 1);
    CHECK(countOf(sim, 2) == 1);
    REQUIRE(sentAlive(sim).size() == 2);
    // They come for the player.
    const ct::Enemy& first = *sim.state().world.enemies.get(sentAlive(sim).front());
    CHECK(first.target == player);
    CHECK(first.spawner == 5);

    kill(sim);
    sim.run({}, kSecond / 4);
    CHECK(phaseOf(sim) == ct::EncounterPhase::Between);
    CHECK(sentAlive(sim).empty());
    sim.run({}, kSecond / 2);
    CHECK(countOf(sim, 2) == 2);
    // One at a time from the one spot, as each moves off it.
    CHECK(sentAlive(sim).size() == 1);
    sim.run({}, 2 * kSecond);
    REQUIRE(sentAlive(sim).size() == 2);
    CHECK(countOf(sim, 3) == 0);

    kill(sim);
    sim.run({}, 2);
    CHECK(countOf(sim, 3) == 1);
    CHECK(phaseOf(sim) == ct::EncounterPhase::Cleared);
    sim.run({}, kSecond);
    CHECK(sentAlive(sim).empty());
    CHECK(countOf(sim, 1) == 1);
    CHECK(countOf(sim, 2) == 2);
    // The corpses are gone after their time.
    CHECK(sim.state().world.enemies.size() == 0);
}

TEST_CASE("an encounter keeps to its most alive, and starts the next wave with a few left") {
    SimHarness sim(arenaLevel(arena({{"start_on", "1"},
                                     {"max_alive", "2"},
                                     {"next_wave_at", "1"},
                                     {"wave_delay", "0"}}),
                              {spawnerAt(-5.0f, 0.0f, {{"encounter", "arena"}}),
                               spawnerAt(5.0f, 0.0f, {{"encounter", "arena"}}),
                               spawnerAt(0.0f, 5.0f, {{"encounter", "arena"}}),
                               spawnerAt(0.0f, -5.0f, {{"encounter", "arena"}, {"wave", "2"}})}),
                   withCharger());
    sim.spawn({12.0f, 12.0f, 0.0f});
    sim.run({}, kSecond / 4);
    CHECK(countOf(sim, 1) == 1);
    CHECK(sentAlive(sim).size() == 2);
    // A death makes room for the third.
    kill(sim, 1);
    sim.run({}, 2);
    CHECK(sentAlive(sim).size() == 2);
    CHECK(countOf(sim, 2) == 1);
    // One left of the first wave: the second comes.
    kill(sim, 1);
    sim.run({}, 2);
    CHECK(countOf(sim, 2) == 2);
    CHECK(sentAlive(sim).size() == 2);
    CHECK(phaseOf(sim) == ct::EncounterPhase::Running);
}

TEST_CASE("a spawner sends an enemy for each spawn input, and its enemies fire its events") {
    SimHarness sim(
        arenaLevel(
            arena(),
            {spawnerAt(
                 -5.0f, 0.0f, {{"name", "pit"}, {"hunt", "0"}, {"yaw", "180"}},
                 {{"on_spawn", "started", "add", 0.0f}, {"on_death", "cleared", "add", 0.0f}}),
             entity("timer",
                    {{"origin", "0 0 0"}, {"interval", "0.1"}, {"repeat", "0"}, {"start_on", "1"}},
                    {{"on_timer", "pit", "spawn", 0.0f}, {"on_timer", "pit", "spawn", 0.0f}})}),
        withCharger());
    // The arena has no spawners: that is worth a warning, and it does nothing.
    CHECK(sim.level().warnings().size() == 1);
    sim.spawn({12.0f, 12.0f, 0.0f});
    sim.run({}, kSecond);
    // The first stands where it appeared, not hunting, so the second waits.
    REQUIRE(sentAlive(sim).size() == 1);
    CHECK(countOf(sim, 1) == 1);
    const ct::Enemy& calm = *sim.state().world.enemies.get(sentAlive(sim).front());
    CHECK(ct::enemyState(calm) == ct::EnemyState::Idle);
    CHECK(sim.state().world.spawners.get(sim.level().entity(4)->id)->waiting == 1);

    kill(sim);
    sim.run({}, 2);
    CHECK(countOf(sim, 3) == 1);
    CHECK(sentAlive(sim).size() == 1);
    CHECK(countOf(sim, 1) == 2);
    CHECK(sim.state().world.spawners.get(sim.level().entity(4)->id)->waiting == 0);
}

TEST_CASE("a stopped encounter sends no more") {
    SimHarness sim(arenaLevel(arena({{"start_on", "1"}, {"max_alive", "1"}}),
                              {spawnerAt(-5.0f, 0.0f, {{"encounter", "arena"}, {"count", "3"}}),
                               sendAt(kSecond / 2, "arena", "stop")}),
                   withCharger());
    sim.spawn({12.0f, 12.0f, 0.0f});
    sim.run({}, kSecond);
    CHECK(sentAlive(sim).size() == 1);
    CHECK(phaseOf(sim) == ct::EncounterPhase::Stopped);
    kill(sim);
    sim.run({}, kSecond);
    CHECK(sentAlive(sim).empty());
    CHECK(countOf(sim, 3) == 0);
}

TEST_CASE("spawners that name something other than an encounter are warned about") {
    const ct::GameLevel level(
        arenaLevel(arena(), {spawnerAt(0.0f, 0.0f, {{"encounter", "waves"}}),
                             spawnerAt(1.0f, 0.0f, {{"encounter", "nowhere"}})}),
        {}, withCharger());
    const std::span<const std::string> warnings = level.warnings();
    REQUIRE(warnings.size() == 3);
    CHECK(std::count_if(warnings.begin(), warnings.end(), [](const std::string& warning) {
              return warning.find("is not an encounter") != std::string::npos;
          }) == 1);
    CHECK(std::count_if(warnings.begin(), warnings.end(), [](const std::string& warning) {
              return warning.find("no entity is named 'nowhere'") != std::string::npos;
          }) == 1);
}

TEST_CASE("encounters play out the same every time") {
    const auto play = [] {
        SimHarness sim(arenaLevel(arena({{"start_on", "1"}, {"max_alive", "2"}}),
                                  {spawnerAt(-5.0f, 0.0f, {{"encounter", "arena"}, {"count", "3"}}),
                                   spawnerAt(5.0f, 0.0f, {{"encounter", "arena"}, {"wave", "2"}})}),
                       withCharger());
        sim.spawn({12.0f, 12.0f, 0.0f});
        for (int i = 0; i < 6; ++i) {
            sim.run({}, kSecond / 2);
            kill(sim, 1);
        }
        return ct::hashState(sim.state());
    };
    CHECK(play() == play());
}

TEST_CASE("the encounter test level shuts the way in, and opens it and the way on when cleared") {
    std::vector<std::string> importWarnings;
    std::string error;
    const std::optional<ct::Level> level = ct::loadAnyLevelFile(
        CROFTECH_CONTENT_DIR "/levels/encounter_test.level", importWarnings, error);
    REQUIRE_MESSAGE(level, error);
    ct::GameSettings settings = withCharger();
    ct::EnemySettings gunner = settings.enemies[0];
    gunner.name = "gunner";
    settings.enemies.push_back(gunner);
    SimHarness sim(*level, settings);
    const auto door = [&](const char* name) {
        for (std::uint32_t i = 0; i < level->entities.size(); ++i) {
            const std::string* found = ct::findProperty(level->entities[i], "name");
            if (found != nullptr && *found == name) {
                return ct::MoverPhase(
                    sim.state().world.movers.get(sim.level().entity(i)->id)->phase);
            }
        }
        FAIL("no door " << name);
        return ct::MoverPhase::AtStart;
    };
    sim.spawn({0.0f, -27.0f, 0.0f}, 16384);
    sim.run({}, kSecond / 2);
    CHECK(door("way_in") == ct::MoverPhase::AtEnd);
    CHECK(sentAlive(sim).empty());

    // Into the arena: the way in shuts behind.
    sim.place({0.0f, -9.0f, 0.0f});
    sim.run({}, 2 * kSecond);
    CHECK(door("way_in") == ct::MoverPhase::AtStart);
    CHECK(door("way_on") == ct::MoverPhase::AtStart);
    // Every enemy dies as soon as it is sent, through all three waves.
    std::size_t killed = 0;
    for (int i = 0; i < 60 && sim.state().world.encounters.components()[0].phase !=
                                  static_cast<std::uint32_t>(ct::EncounterPhase::Cleared);
         ++i) {
        killed += sentAlive(sim).size();
        kill(sim);
        sim.run({}, kSecond / 2);
    }
    CHECK(killed == 2 + 2 + 1 + 1 + 2 + 2 + 3 + 3 + 2 + 2);
    sim.run({}, 3 * kSecond);
    CHECK(door("way_in") == ct::MoverPhase::AtEnd);
    CHECK(door("way_on") == ct::MoverPhase::AtEnd);
}
