#include "SimHarness.h"

#include "croftech/level/EntityClasses.h"
#include "croftech/sim/BehaviourTree.h"
#include "croftech/sim/Damage.h"
#include "croftech/sim/Enemies.h"

#include <doctest/doctest.h>

#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ct::test::SimHarness;

namespace {

constexpr int kSecond = static_cast<int>(ct::kTickRate);

ct::Brush arenaFloor() { return ct::makeBoxBrush({-15.0f, -15.0f, -1.0f}, {15.0f, 15.0f, 0.0f}); }

// An enemy at the origin facing +X, and a timer sending it alert at the tick given.
ct::Level alertedEnemyLevel(int alertTick) {
    const float seconds = static_cast<float>(alertTick) / static_cast<float>(ct::kTickRate);
    return {{arenaFloor()},
            {{std::string(ct::kEnemyClass), {{"origin", "0 0 0"}, {"name", "enemy"}}, {}},
             {"timer",
              {{"origin", "0 0 0"},
               {"interval", std::format("{}", seconds)},
               {"repeat", "0"},
               {"start_on", "1"}},
              {{"on_timer", "enemy", "alert", 0.0f}}}}};
}

// A charger that runs at players and hits them, deciding by the tree given.
ct::GameSettings chargerWith(std::string_view tree) {
    ct::GameSettings settings;
    ct::EnemySettings charger;
    charger.name = "charger";
    charger.health = 100;
    charger.reactionTime = 0.1f;
    charger.meleeDamage = 10;
    charger.movement = ct::enemyMovement(charger, settings.movement);
    std::string error;
    const std::optional<ct::BehaviourTree> parsed = ct::parseBehaviourTree(tree, "test", error);
    REQUIRE_MESSAGE(parsed.has_value(), error);
    settings.behaviours = {*parsed};
    charger.behaviourIndex = 0;
    settings.enemies = {charger};
    return settings;
}

ct::EntityId enemyId(const SimHarness& sim) { return sim.level().entity(0)->id; }

const ct::Enemy& enemyOf(const SimHarness& sim) {
    return *sim.state().world.enemies.get(enemyId(sim));
}

ct::Vec3 enemyPosition(const SimHarness& sim) {
    return sim.state().world.transforms.get(enemyId(sim))->position;
}

bool isActive(const SimHarness& sim, std::uint32_t node) {
    const ct::EnemyBehaviour& behaviour = *sim.state().world.enemyBehaviours.get(enemyId(sim));
    return (behaviour.active[node / 32] & (1u << (node % 32))) != 0;
}

} // namespace

TEST_CASE("an enemy whose tree only patrols goes back to its post when alerted") {
    SimHarness sim(alertedEnemyLevel(2), chargerWith("sequence\n"
                                                     "    see\n"
                                                     "    patrol\n"));
    sim.spawn({8.0f, 0.0f, 0.0f});
    sim.run({}, kSecond * 2);
    // It noticed the player in front of it, then forgot them.
    CHECK((enemyOf(sim).flags & ct::kEnemyAlertFired) != 0);
    CHECK_FALSE(enemyOf(sim).target.isValid());
    CHECK(std::fabs(enemyPosition(sim).x) < 0.1f);
}

TEST_CASE("an enemy waits as its tree says before chasing") {
    SimHarness sim(alertedEnemyLevel(2), chargerWith("fallback\n"
                                                     "    sequence\n"
                                                     "        has_target\n"
                                                     "        wait 1\n"
                                                     "        chase\n"
                                                     "    patrol\n"));
    // Behind it, so only the alert wakes it.
    sim.spawn({-8.0f, 0.0f, 0.0f});
    sim.run({}, kSecond * 3 / 4);
    CHECK(enemyOf(sim).target.isValid());
    CHECK(std::fabs(enemyPosition(sim).x) < 0.05f);
    // The wait node (3) is running, and the chase (4) not yet reached.
    CHECK(isActive(sim, 3));
    CHECK_FALSE(isActive(sim, 4));
    sim.run({}, kSecond);
    CHECK(enemyPosition(sim).x < -1.0f);
    CHECK(isActive(sim, 4));
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Chase);
}

TEST_CASE("an enemy can give up the fight when badly hurt") {
    SimHarness sim(alertedEnemyLevel(2), chargerWith("fallback\n"
                                                     "    sequence\n"
                                                     "        has_target\n"
                                                     "        invert\n"
                                                     "            health_below 0.5\n"
                                                     "        chase\n"
                                                     "    patrol\n"));
    sim.spawn({-12.0f, 0.0f, 0.0f});
    sim.run({}, kSecond / 2);
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Chase);
    ct::Damage damage;
    damage.amount = 60;
    ct::applyDamage(sim.state(), {}, enemyId(sim), damage);
    sim.run({}, 2);
    // Back to its post.
    CHECK_FALSE(enemyOf(sim).target.isValid());
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Patrol);
}

TEST_CASE("the nodes an enemy reaches and that don't fail are lit for the editor") {
    SimHarness sim(alertedEnemyLevel(10000), chargerWith(ct::standardBehaviourText()));
    sim.run({}, 2);
    // Calm: see (1), hear (2) and patrol (the last) run; has_target (5) fails.
    const std::size_t patrol = ct::standardBehaviourTree().nodes.size() - 1;
    CHECK(isActive(sim, 0));
    CHECK(isActive(sim, 1));
    CHECK(isActive(sim, 2));
    CHECK(isActive(sim, 3));
    CHECK_FALSE(isActive(sim, 4));
    CHECK_FALSE(isActive(sim, 5));
    CHECK(isActive(sim, static_cast<std::uint32_t>(patrol)));
}

TEST_CASE("a cooldown, a chance and a time limit keep to their times") {
    // A time limit stops the chase after half a second, a chance of 0 never lets it start, and a
    // quarter-second wait under a one-second cooldown runs only once each cooldown is over.
    SimHarness sim(alertedEnemyLevel(2), chargerWith("fallback\n"
                                                     "    sequence\n"
                                                     "        has_target\n"
                                                     "        chance 1\n"
                                                     "        time_limit 0.5\n"
                                                     "            chase\n"
                                                     "    patrol\n"));
    sim.spawn({-12.0f, 0.0f, 0.0f});
    sim.run({}, kSecond / 4);
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Chase);
    sim.run({}, kSecond / 2);
    // The chase was cut short; patrol took it back.
    CHECK(ct::enemyState(enemyOf(sim)) != ct::EnemyState::Chase);

    SimHarness never(alertedEnemyLevel(2), chargerWith("fallback\n"
                                                       "    sequence\n"
                                                       "        has_target\n"
                                                       "        chance 0\n"
                                                       "        chase\n"
                                                       "    patrol\n"));
    never.spawn({-12.0f, 0.0f, 0.0f});
    never.run({}, kSecond / 2);
    CHECK(ct::enemyState(enemyOf(never)) != ct::EnemyState::Chase);

    SimHarness cooled(alertedEnemyLevel(2), chargerWith("sequence\n"
                                                        "    cooldown 1\n"
                                                        "        wait 0.25\n"
                                                        "    patrol\n"));
    cooled.run({}, 1);
    const auto waitRunning = [&] { return isActive(cooled, 2); };
    CHECK(waitRunning());
    cooled.run({}, kSecond / 4 + 1);
    // Finished: the cooldown holds it off, so the sequence fails before patrol.
    CHECK_FALSE(waitRunning());
    CHECK_FALSE(isActive(cooled, 3));
    cooled.run({}, kSecond);
    CHECK(waitRunning());
}
