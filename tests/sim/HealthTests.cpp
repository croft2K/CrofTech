#include "SimHarness.h"

#include "croftech/sim/Damage.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using ct::test::floorBrush;
using ct::test::SimHarness;

namespace {

ct::Brush owned(ct::Vec3 min, ct::Vec3 max, std::uint32_t entity) {
    ct::Brush brush = ct::makeBoxBrush(min, max);
    brush.entity = entity;
    return brush;
}

ct::UserCmd forward() {
    ct::UserCmd cmd;
    cmd.forward = 127;
    return cmd;
}

ct::UserCmd pressing(std::uint16_t buttons) {
    ct::UserCmd cmd;
    cmd.buttons = buttons;
    return cmd;
}

ct::Damage hit(std::int32_t amount) {
    ct::Damage damage;
    damage.amount = amount;
    return damage;
}

// The floor with a hurt volume between x = 2 and 4, entity 0.
ct::Level hurtLevel(std::vector<ct::EntityProperty> properties,
                    std::vector<ct::LevelEntity> more = {}) {
    properties.push_back({"name", "lava"});
    ct::Level level{{floorBrush(), owned({2.0f, -1.0f, 0.0f}, {4.0f, 1.0f, 1.0f}, 0)},
                    {{"hurt", std::move(properties), {}}}};
    for (ct::LevelEntity& entity : more) {
        level.entities.push_back(std::move(entity));
    }
    return level;
}

} // namespace

TEST_CASE("players spawn with the starting health and armour") {
    ct::RulesSettings rules;
    rules.startHealth = 150;
    rules.startArmour = 25;
    SimHarness sim({floorBrush()}, {}, rules);
    sim.spawn({0.0f, 0.0f, 0.0f});
    CHECK(sim.health().health == 150);
    CHECK(sim.health().armour == 25);
    CHECK_FALSE(ct::isDead(sim.health()));
}

TEST_CASE("armour takes its share of damage while it lasts, and health running out kills") {
    SimHarness sim({floorBrush()});
    const ct::EntityId player = sim.spawn({0.0f, 0.0f, 0.0f});
    sim.health().armour = 50;
    const ct::RulesSettings& rules = sim.settings().rules;

    // Armour takes 66% of 60, rounded up: 40.
    CHECK_FALSE(ct::applyDamage(sim.state(), rules, player, hit(60)));
    CHECK(sim.health().armour == 10);
    CHECK(sim.health().health == 80);
    CHECK(sim.health().hurtTick == sim.state().tick);

    // Armour runs out, and health takes the rest.
    CHECK(ct::applyDamage(sim.state(), rules, player, hit(100)));
    CHECK(sim.health().armour == 0);
    CHECK(sim.health().health == -10);
    CHECK(ct::isDead(sim.health()));
    CHECK(sim.health().diedTick == sim.state().tick);

    // The dead take no more.
    CHECK_FALSE(ct::applyDamage(sim.state(), rules, player, hit(10)));
    CHECK(sim.health().health == -10);
}

TEST_CASE("damage pushes and remembers who did it") {
    SimHarness sim({floorBrush()});
    const ct::EntityId player = sim.spawn({0.0f, 0.0f, 0.0f});
    const ct::EntityId other = sim.spawn({5.0f, 0.0f, 0.0f}, 0, 1);
    ct::Damage damage = hit(10);
    damage.attacker = other;
    damage.push = {0.0f, 0.0f, 8.0f};
    ct::applyDamage(sim.state(), sim.settings().rules, player, damage);
    CHECK(sim.health().attacker == other);
    CHECK(sim.motion().velocity.z == doctest::Approx(8.0f));
    sim.run({}, 16);
    CHECK(sim.feetZ() > 0.5f);
}

TEST_CASE("damage from untrusted states can't overflow") {
    SimHarness sim({floorBrush()});
    const ct::EntityId player = sim.spawn({0.0f, 0.0f, 0.0f});
    sim.health().health = -2'000'000'000;
    sim.health().armour = -5;
    CHECK(ct::applyDamage(sim.state(), sim.settings().rules, player, hit(2'000'000'000)));
    CHECK(sim.health().health == std::numeric_limits<std::int32_t>::min());
    CHECK(sim.health().armour == -5);
}

TEST_CASE("a hurt volume hurts what stays inside it once per interval") {
    SimHarness sim(hurtLevel({{"damage", "10"}, {"interval", "0.5"}}));
    CHECK(sim.level().warnings().empty());
    sim.spawn({3.0f, 0.0f, 0.0f});
    sim.run({}, 1);
    CHECK(sim.health().health == 90);
    CHECK(sim.health().hurtKind == static_cast<std::uint32_t>(ct::DamageKind::Hazard));
    CHECK_FALSE(sim.health().attacker.isValid());
    sim.run({}, ct::kTickRate / 2 - 1);
    CHECK(sim.health().health == 90);
    sim.run({}, 1);
    CHECK(sim.health().health == 80);

    // Out of it, nothing more.
    sim.place({10.0f, 0.0f, 0.0f});
    sim.run({}, ct::kTickRate);
    CHECK(sim.health().health == 80);
}

TEST_CASE("the wiring switches hurt volumes on and off") {
    SimHarness sim(
        hurtLevel({{"start_disabled", "1"}},
                  {{"timer",
                    {{"origin", "0 0 0"}, {"interval", "1"}, {"repeat", "0"}, {"start_on", "1"}},
                    {{"on_timer", "lava", "enable", 0.0f}}}}));
    CHECK(sim.level().warnings().empty());
    sim.spawn({3.0f, 0.0f, 0.0f});
    sim.run({}, ct::kTickRate - 1);
    CHECK(sim.health().health == 100);
    sim.run({}, 1);
    CHECK(sim.health().health == 90);
}

TEST_CASE("a kill volume kills whatever walks in") {
    SimHarness sim(hurtLevel({{"damage", "100000"}}));
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.health().armour = 200;
    sim.run(forward(), ct::kTickRate);
    CHECK(ct::isDead(sim.health()));
    CHECK(sim.health().hurtKind == static_cast<std::uint32_t>(ct::DamageKind::Hazard));
}

TEST_CASE("falling out of the world kills, and the body stops there") {
    SimHarness sim({ct::makeBoxBrush({-2.0f, -2.0f, -1.0f}, {2.0f, 2.0f, 0.0f})});
    CHECK(sim.level().killHeight() == doctest::Approx(-1.0f - ct::kOutOfWorldDepth));
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(forward(), 4 * ct::kTickRate);
    REQUIRE(ct::isDead(sim.health()));
    CHECK(sim.health().hurtKind == static_cast<std::uint32_t>(ct::DamageKind::OutOfWorld));
    const float stopped = sim.feetZ();
    CHECK(stopped < sim.level().killHeight());
    CHECK(stopped > sim.level().killHeight() - 1.0f);
    sim.run(forward(), ct::kTickRate);
    CHECK(sim.feetZ() == stopped);
}

TEST_CASE("the dead don't move or set off triggers") {
    ct::Level level{{floorBrush(), owned({2.0f, -1.0f, 0.0f}, {4.0f, 1.0f, 2.0f}, 0)},
                    {{"trigger", {{"name", "zone"}}, {{"on_enter", "count", "add", 0.0f}}},
                     {"counter", {{"origin", "0 0 0"}, {"name", "count"}, {"count", "100"}}, {}}}};
    SimHarness sim(level);
    const ct::EntityId player = sim.spawn({0.0f, 0.0f, 0.0f});
    ct::kill(sim.state(), player, ct::DamageKind::Generic);
    sim.run(forward(), 8);
    CHECK(sim.transform().position.x == 0.0f);
    sim.place({3.0f, 0.0f, 0.0f});
    sim.run({}, 8);
    CHECK(sim.state().world.counters.get(sim.level().entity(1)->id)->value == 0);
}

TEST_CASE("a dead player respawns on a press of fire once the delay has passed") {
    ct::Level level{{floorBrush()},
                    {ct::makePlayerSpawn({-10.0f, 0.0f, 0.0f}, 90.0f),
                     ct::makePlayerSpawn({20.0f, 0.0f, 0.0f}, 0.0f)}};
    SimHarness sim(level);
    const ct::EntityId player = sim.spawn({5.0f, 0.0f, 0.0f});
    sim.health().armour = 0;
    ct::kill(sim.state(), player, ct::DamageKind::Generic);

    // Holding fire from the moment of death isn't a press once the delay is over.
    sim.run(pressing(ct::kButtonAttack), 2 * ct::kTickRate);
    REQUIRE(ct::isDead(sim.health()));
    sim.run({}, 1);
    sim.run(pressing(ct::kButtonAttack), 1);
    CHECK_FALSE(ct::isDead(sim.health()));
    CHECK(sim.health().health == sim.settings().rules.startHealth);
    // Alone, a player respawns at the first spawn.
    CHECK(sim.transform().position.x == doctest::Approx(-10.0f));
    CHECK(sim.transform().yaw == sim.level().spawns()[0].yaw);
}

TEST_CASE("pressing fire too soon after dying does nothing") {
    SimHarness sim(ct::test::floorLevel());
    const ct::EntityId player = sim.spawn({5.0f, 0.0f, 0.0f});
    ct::kill(sim.state(), player, ct::DamageKind::Generic);
    for (int i = 0; i < 8; ++i) {
        sim.run(pressing(ct::kButtonAttack), 1);
        sim.run({}, 1);
    }
    CHECK(ct::isDead(sim.health()));
}

TEST_CASE("a jump that respawns doesn't also jump") {
    SimHarness sim(ct::test::floorLevel());
    const ct::EntityId player = sim.spawn({5.0f, 0.0f, 0.0f});
    ct::kill(sim.state(), player, ct::DamageKind::Generic);
    sim.run({}, 2 * ct::kTickRate);
    sim.run(pressing(ct::kButtonJump), 16);
    CHECK_FALSE(ct::isDead(sim.health()));
    CHECK(sim.onGround());
    CHECK(sim.feetZ() < 0.01f);
}

TEST_CASE("players respawn at the spawn point furthest from the living") {
    ct::Level level{{floorBrush()},
                    {ct::makePlayerSpawn({0.0f, 0.0f, 0.0f}, 0.0f),
                     ct::makePlayerSpawn({20.0f, 0.0f, 0.0f}, 0.0f),
                     ct::makePlayerSpawn({40.0f, 0.0f, 0.0f}, 0.0f)}};
    SimHarness sim(level);
    const ct::EntityId first = sim.spawn({10.0f, 0.0f, 0.0f});
    const ct::EntityId second = sim.spawn({38.0f, 0.0f, 0.0f}, 0, 1);
    const ct::World& world = sim.state().world;
    CHECK(ct::chooseSpawnPoint(sim.level(), world, first)->feetPosition.x == 0.0f);
    CHECK(ct::chooseSpawnPoint(sim.level(), world, second)->feetPosition.x == 40.0f);
    // The dead don't count.
    ct::kill(sim.state(), second, ct::DamageKind::Generic);
    CHECK(ct::chooseSpawnPoint(sim.level(), world, first)->feetPosition.x == 0.0f);
    CHECK(ct::chooseSpawnPoint(sim.level(), world)->feetPosition.x == 40.0f);
}

TEST_CASE("a start point replaces the level's spawns") {
    const ct::GameLevel level(ct::test::floorLevel(), ct::SpawnPoint{{7.0f, 1.0f, 0.0f}, 100});
    REQUIRE(level.spawns().size() == 1);
    CHECK(level.spawns()[0].feetPosition.x == 7.0f);
    CHECK(level.spawns()[0].yaw == 100);
}

TEST_CASE("the kill height is below where movers can go") {
    ct::Level level{{floorBrush(), owned({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 1.0f}, 0)},
                    {{"mover", {{"move", "0 0 -30"}}, {}}}};
    const ct::GameLevel game(level);
    CHECK(game.killHeight() == doctest::Approx(-30.0f - ct::kOutOfWorldDepth));
}
