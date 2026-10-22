#include "SimHarness.h"

#include "croftech/level/EntityClasses.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/Damage.h"
#include "croftech/sim/Enemies.h"
#include "croftech/sim/Firing.h"

#include <doctest/doctest.h>

#include <cmath>
#include <format>
#include <ostream> // doctest prints string_view through operator<<
#include <string>
#include <utility>
#include <vector>

using ct::test::SimHarness;

namespace {

// A 30 x 30 m floor whose top is at z = 0: small, so its navigation mesh is quick to build.
ct::Brush arenaFloor() { return ct::makeBoxBrush({-15.0f, -15.0f, -1.0f}, {15.0f, 15.0f, 0.0f}); }

ct::LevelEntity entity(std::string className, std::vector<ct::EntityProperty> properties,
                       std::vector<ct::EntityOutput> outputs = {}) {
    return {std::move(className), std::move(properties), std::move(outputs)};
}

// An enemy standing at (x, y) on the floor, facing yaw degrees, wired to count its alert and
// death on the counters "alerted" and "died".
ct::LevelEntity enemyAt(float x, float y, float yaw, std::string name = "enemy") {
    return entity(std::string(ct::kEnemyClass),
                  {{"origin", std::format("{} {} 0", x, y)},
                   {"yaw", std::format("{}", yaw)},
                   {"name", std::move(name)}},
                  {{"on_alert", "alerted", "add", 0.0f}, {"on_death", "died", "add", 0.0f}});
}

ct::LevelEntity counter(std::string name) {
    return entity("counter", {{"origin", "0 0 0"}, {"name", std::move(name)}, {"count", "65535"}});
}

// Sends alert to the enemy once, at the tick given.
ct::LevelEntity alertAt(int tick, std::string target = "enemy") {
    const float seconds = static_cast<float>(tick) / static_cast<float>(ct::kTickRate);
    return entity("timer",
                  {{"origin", "0 0 0"},
                   {"interval", std::format("{}", seconds)},
                   {"repeat", "0"},
                   {"start_on", "1"}},
                  {{"on_timer", std::move(target), "alert", 0.0f}});
}

// A level with the enemy (level entity 0), the two counters (1 and 2), anything else given, and
// the brushes.
ct::Level levelWith(std::vector<ct::Brush> brushes, ct::LevelEntity enemy,
                    std::vector<ct::LevelEntity> more = {}) {
    std::vector<ct::LevelEntity> entities{std::move(enemy), counter("alerted"), counter("died")};
    for (ct::LevelEntity& other : more) {
        entities.push_back(std::move(other));
    }
    return {std::move(brushes), std::move(entities)};
}

// One kind of enemy: a charger that runs at players and hits them.
ct::GameSettings withCharger() {
    ct::GameSettings settings;
    ct::EnemySettings charger;
    charger.name = "charger";
    charger.health = 60;
    charger.speed = 6.0f;
    charger.reactionTime = 0.25f;
    charger.meleeDamage = 20;
    charger.meleeRange = 0.6f;
    charger.meleeWindup = 0.25f;
    charger.meleeInterval = 1.0f;
    charger.corpseTime = 1.0f;
    charger.movement = ct::enemyMovement(charger, settings.movement);
    settings.enemies = {charger};
    return settings;
}

// The charger and a gunner with a hitscan gun that never misses, as its second kind.
ct::GameSettings withGunner() {
    ct::GameSettings settings = withCharger();
    ct::WeaponSettings gun;
    gun.name = "gun";
    gun.damage = 5;
    gun.fireInterval = 0.1f;
    gun.ammoPerShot = 1;
    settings.weapons = {gun};
    ct::EnemySettings gunner;
    gunner.name = "gunner";
    gunner.health = 40;
    gunner.reactionTime = 0.25f;
    gunner.weapon = "gun";
    gunner.weaponIndex = 0;
    gunner.attackRange = 25.0f;
    gunner.keepDistance = 8.0f;
    gunner.aimError = 0.0f;
    gunner.burst = 3;
    gunner.burstPause = 0.5f;
    gunner.strafeTime = 0.5f;
    gunner.movement = ct::enemyMovement(gunner, settings.movement);
    settings.enemies.push_back(gunner);
    return settings;
}

ct::LevelEntity gunnerAt(float x, float y, float yaw) {
    ct::LevelEntity enemy = enemyAt(x, y, yaw);
    enemy.properties.push_back({"type", "gunner"});
    return enemy;
}

// A player that holds the trigger, with a gun to fire.
ct::GameSettings armedPlayer(ct::GameSettings settings) {
    ct::WeaponSettings pistol;
    pistol.name = "pistol";
    pistol.given = true;
    pistol.startAmmo = 50;
    pistol.fireInterval = 0.25f;
    settings.weapons.insert(settings.weapons.begin(), pistol);
    for (ct::EnemySettings& enemy : settings.enemies) {
        if (enemy.weaponIndex != 0xFFFFFFFFu) {
            ++enemy.weaponIndex;
        }
    }
    return settings;
}

const ct::Enemy& enemyOf(const SimHarness& sim) {
    return *sim.state().world.enemies.get(sim.level().entity(0)->id);
}

const ct::Transform& enemyTransform(const SimHarness& sim) {
    return *sim.state().world.transforms.get(sim.level().entity(0)->id);
}

std::uint32_t countOf(const SimHarness& sim, std::uint32_t levelEntity) {
    return sim.state().world.counters.get(sim.level().entity(levelEntity)->id)->value;
}

constexpr int kSecond = static_cast<int>(ct::kTickRate);

} // namespace

TEST_CASE("enemies stand where they are put, as the level and their kind say") {
    const ct::GameSettings settings = withCharger();
    SimHarness sim(levelWith({arenaFloor()}, enemyAt(3.0f, 4.0f, 90.0f)), settings);
    CHECK(sim.level().warnings().empty());
    sim.run({}, 1);
    const ct::Transform& transform = enemyTransform(sim);
    CHECK(transform.yaw == 16384);
    CHECK(std::fabs(transform.position.x - 3.0f) < 1e-4f);
    CHECK(std::fabs(transform.position.z - 0.9f) < 0.01f);
    const ct::EntityId id = sim.level().entity(0)->id;
    CHECK(sim.state().world.healths.get(id)->health == 60);
    CHECK(ct::movementOf(sim.state().world, id, settings).maxGroundSpeed == 6.0f);
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
}

TEST_CASE("an idle enemy notices a player in front of it, not behind it or behind a wall") {
    const ct::GameSettings settings = withCharger();
    {
        SimHarness sim(levelWith({arenaFloor()}, enemyAt(0.0f, 0.0f, 0.0f)), settings);
        const ct::EntityId player = sim.spawn({10.0f, 2.0f, 0.0f});
        sim.run({}, 2);
        CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Alerted);
        CHECK(enemyOf(sim).target == player);
        CHECK(countOf(sim, 1) == 1);
    }
    {
        SimHarness sim(levelWith({arenaFloor()}, enemyAt(0.0f, 0.0f, 0.0f)), settings);
        sim.spawn({-10.0f, 0.0f, 0.0f});
        sim.run({}, kSecond);
        CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
        CHECK(countOf(sim, 1) == 0);
    }
    {
        SimHarness sim(
            levelWith({arenaFloor(), ct::makeBoxBrush({5.0f, -15.0f, 0.0f}, {6.0f, 15.0f, 4.0f})},
                      enemyAt(0.0f, 0.0f, 0.0f)),
            settings);
        sim.spawn({10.0f, 0.0f, 0.0f});
        sim.run({}, kSecond);
        CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
    }
}

TEST_CASE("an enemy chases the player and strikes them when in reach") {
    SimHarness sim(levelWith({arenaFloor()}, enemyAt(-6.0f, 0.0f, 0.0f)), withCharger());
    const ct::EntityId player = sim.spawn({6.0f, 0.0f, 0.0f});
    sim.run({}, 3 * kSecond);
    CHECK(sim.health().health < 100);
    CHECK(sim.health().hurtKind == static_cast<std::uint32_t>(ct::DamageKind::Melee));
    CHECK(sim.health().attacker == sim.level().entity(0)->id);
    // Each swing does its damage; at most one a second.
    CHECK((100 - sim.health().health) % 20 == 0);
    CHECK(100 - sim.health().health <= 60);
    // It stops at the player rather than pushing into them.
    const ct::Vec3 between = sim.transform().position - enemyTransform(sim).position;
    CHECK(std::sqrt(between.x * between.x + between.y * between.y) < 2.0f);
    CHECK(std::sqrt(between.x * between.x + between.y * between.y) > 0.75f);
    CHECK(enemyOf(sim).target == player);
}

TEST_CASE("an enemy told to attack finds its way round a wall") {
    // A wall across the floor at x = 0 with a gap at its north end.
    SimHarness sim(
        levelWith({arenaFloor(), ct::makeBoxBrush({-0.5f, -15.0f, 0.0f}, {0.5f, 6.0f, 3.0f})},
                  enemyAt(-6.0f, 0.0f, 0.0f), {alertAt(2)}),
        withCharger());
    sim.spawn({6.0f, 0.0f, 0.0f});
    sim.run({}, kSecond / 2);
    CHECK(countOf(sim, 1) == 1);
    CHECK(ct::enemyState(enemyOf(sim)) != ct::EnemyState::Idle);
    sim.run({}, 6 * kSecond);
    CHECK(enemyTransform(sim).position.x > 0.0f);
    CHECK(sim.health().health < 100);
}

TEST_CASE("a hurt enemy turns on whoever hurt it") {
    SimHarness sim(levelWith({arenaFloor()}, enemyAt(0.0f, 0.0f, 0.0f)), withCharger());
    const ct::EntityId player = sim.spawn({-8.0f, 0.0f, 0.0f});
    sim.run({}, kSecond / 2);
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
    ct::Damage damage;
    damage.amount = 10;
    damage.kind = ct::DamageKind::Weapon;
    damage.attacker = player;
    ct::applyDamage(sim.state(), sim.settings().rules, sim.level().entity(0)->id, damage);
    sim.run({}, kSecond / 2);
    CHECK(enemyOf(sim).target == player);
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Chase);
    // It turned round to face them.
    CHECK(std::abs(static_cast<std::int16_t>(enemyTransform(sim).yaw - 32768)) < 1000);
}

TEST_CASE("an enemy that is killed sends on_death, falls, and is taken away after a while") {
    SimHarness sim(levelWith({arenaFloor()}, enemyAt(0.0f, 0.0f, 0.0f)), withCharger());
    const ct::EntityId player = sim.spawn({-8.0f, 0.0f, 0.0f});
    const ct::EntityId id = sim.level().entity(0)->id;
    ct::kill(sim.state(), id, ct::DamageKind::Weapon, player);
    sim.run({}, 2);
    CHECK(countOf(sim, 2) == 1);
    // Being killed doesn't count as noticing anyone.
    CHECK(countOf(sim, 1) == 0);
    sim.run({}, kSecond);
    CHECK_FALSE(sim.state().world.entities.isAlive(id));
    CHECK(sim.state().world.enemies.size() == 0);
    CHECK(countOf(sim, 2) == 1);
}

TEST_CASE("shots hit an enemy's own box, whatever its size") {
    ct::GameSettings settings = withCharger();
    settings.enemies[0].width = 3.0f;
    settings.enemies[0].height = 3.0f;
    settings.enemies[0].movement = ct::enemyMovement(settings.enemies[0], settings.movement);
    SimHarness sim(levelWith({arenaFloor()}, enemyAt(0.0f, 0.0f, 0.0f)), settings);
    sim.run({}, 1);
    const ct::CollisionScene scene(sim.level(), sim.state().world);
    // 1.2 m to the side of its middle: past a player-sized box, inside this one.
    const ct::ShotHit hit = ct::traceShot(scene, sim.state().world, sim.settings(),
                                          {-5.0f, 1.2f, 1.0f}, {5.0f, 1.2f, 1.0f}, {});
    CHECK(hit.target == sim.level().entity(0)->id);
    CHECK(std::fabs(hit.position.x + 1.5f) < 0.01f);
}

TEST_CASE("enemies open doors they walk into") {
    // A wall across the floor at y = 0..0.5 with a door that opens to a touch.
    ct::Brush door = ct::makeBoxBrush({-1.0f, 0.125f, 0.0f}, {1.0f, 0.375f, 3.0f});
    door.entity = 3;
    const ct::LevelEntity mover =
        entity("mover", {{"move", "0 0 3"}, {"activate", "touch"}, {"speed", "4"}, {"wait", "2"}});
    SimHarness sim(
        levelWith({arenaFloor(), ct::makeBoxBrush({-15.0f, 0.0f, 0.0f}, {-1.0f, 0.5f, 4.0f}),
                   ct::makeBoxBrush({1.0f, 0.0f, 0.0f}, {15.0f, 0.5f, 4.0f}),
                   ct::makeBoxBrush({-1.0f, 0.0f, 3.0f}, {1.0f, 0.5f, 4.0f}), door},
                  enemyAt(0.0f, -6.0f, 90.0f), {mover, alertAt(2)}),
        withCharger());
    sim.spawn({0.0f, 6.0f, 0.0f});
    sim.run({}, 6 * kSecond);
    CHECK(enemyTransform(sim).position.y > 0.5f);
    CHECK(sim.health().health < 100);
}

TEST_CASE("enemies wait for a lift, ride it, and walk off at the top") {
    // The floor south of y = 0, ground 4 m higher to the north of y = 2, and a lift between that
    // rises when stood on and comes back down after a second.
    ct::Brush lift = ct::makeBoxBrush({-1.0f, 0.0f, -0.25f}, {1.0f, 2.0f, 0.0f});
    lift.entity = 3;
    const ct::LevelEntity mover =
        entity("mover", {{"move", "0 0 4"}, {"activate", "stand"}, {"speed", "2"}, {"wait", "1"}});
    SimHarness sim(levelWith({ct::makeBoxBrush({-15.0f, -15.0f, -1.0f}, {15.0f, 0.0f, 0.0f}),
                              ct::makeBoxBrush({-15.0f, 2.0f, -1.0f}, {15.0f, 15.0f, 4.0f}),
                              ct::makeBoxBrush({-15.0f, 0.0f, -1.0f}, {-1.0f, 2.0f, 4.0f}),
                              ct::makeBoxBrush({1.0f, 0.0f, -1.0f}, {15.0f, 2.0f, 4.0f}), lift},
                             enemyAt(0.0f, -6.0f, 90.0f), {mover, alertAt(2)}),
                   withCharger());
    sim.spawn({0.0f, 8.0f, 4.0f});
    sim.run({}, 12 * kSecond);
    CHECK(enemyTransform(sim).position.z > 4.0f);
    CHECK(enemyTransform(sim).position.y > 2.0f);
    CHECK(sim.health().health < 100);
}

TEST_CASE("a lift carries a waiting enemy back down, since only players hold it at the top") {
    // As above, with the enemy up top: it calls the lift up when it notices the player below.
    ct::Brush lift = ct::makeBoxBrush({-1.0f, 0.0f, -0.25f}, {1.0f, 2.0f, 0.0f});
    lift.entity = 3;
    const ct::LevelEntity mover = entity("mover", {{"name", "lift"},
                                                   {"move", "0 0 4"},
                                                   {"activate", "stand"},
                                                   {"speed", "2"},
                                                   {"wait", "1"}});
    ct::LevelEntity enemy = enemyAt(0.0f, 8.0f, -90.0f);
    enemy.properties[0].value = "0 8 4";
    enemy.outputs.push_back({"on_alert", "lift", "move_to_end", 0.0f});
    SimHarness sim(levelWith({ct::makeBoxBrush({-15.0f, -15.0f, -1.0f}, {15.0f, 0.0f, 0.0f}),
                              ct::makeBoxBrush({-15.0f, 2.0f, -1.0f}, {15.0f, 15.0f, 4.0f}),
                              ct::makeBoxBrush({-15.0f, 0.0f, -1.0f}, {-1.0f, 2.0f, 4.0f}),
                              ct::makeBoxBrush({1.0f, 0.0f, -1.0f}, {15.0f, 2.0f, 4.0f}), lift},
                             enemy, {mover}),
                   withCharger());
    sim.spawn({0.0f, -8.0f, 0.0f});
    sim.run({}, 12 * kSecond);
    CHECK(enemyTransform(sim).position.z < 2.0f);
    CHECK(sim.health().health < 100);
}

TEST_CASE("players and enemies don't walk through each other") {
    // Facing away and deaf, so it stays put while the player walks into its back.
    ct::LevelEntity enemy = enemyAt(3.0f, 0.0f, 0.0f);
    enemy.properties.push_back({"ambush", "1"});
    SimHarness sim(levelWith({arenaFloor()}, enemy), withCharger());
    sim.spawn({0.0f, 0.0f, 0.0f});
    ct::UserCmd forward;
    forward.forward = 127;
    sim.run(forward, kSecond);
    const float playerX = sim.transform().position.x;
    const float enemyX = enemyTransform(sim).position.x;
    CHECK(enemyX - playerX >= 0.8f - 0.01f);
    CHECK(enemyX - playerX < 0.9f);
}

TEST_CASE("enemies hear shots and running nearby, unless they lie in ambush") {
    const ct::GameSettings settings = armedPlayer(withCharger());
    // Fired away from the enemy, so it is heard rather than felt.
    ct::UserCmd shoot;
    shoot.yaw = 32768;
    shoot.buttons = ct::kButtonAttack;
    {
        // Facing away from the player, 10 m off: it hears the shot, fired the other way.
        SimHarness sim(levelWith({arenaFloor()}, enemyAt(0.0f, 0.0f, 0.0f)), settings);
        sim.spawn({-10.0f, 0.0f, 0.0f}, 32768);
        sim.run({}, kSecond / 2);
        CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
        sim.run(shoot, 4);
        CHECK(ct::enemyState(enemyOf(sim)) != ct::EnemyState::Idle);
        CHECK(countOf(sim, 1) == 1);
    }
    {
        ct::LevelEntity deaf = enemyAt(0.0f, 0.0f, 0.0f);
        deaf.properties.push_back({"ambush", "1"});
        SimHarness sim(levelWith({arenaFloor()}, deaf), settings);
        sim.spawn({-10.0f, 0.0f, 0.0f}, 32768);
        sim.run(shoot, kSecond);
        CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
    }
    {
        // Running up behind it: heard within 6 m, not before.
        SimHarness sim(levelWith({arenaFloor()}, enemyAt(0.0f, 0.0f, 0.0f)), settings);
        sim.spawn({-12.0f, 0.0f, 0.0f});
        ct::UserCmd run;
        run.forward = 127;
        sim.run(run, kSecond / 4);
        CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
        sim.run(run, kSecond);
        CHECK(ct::enemyState(enemyOf(sim)) != ct::EnemyState::Idle);
    }
}

TEST_CASE("a gunner shoots from a distance, keeps it, and sidesteps as it fires") {
    SimHarness sim(levelWith({arenaFloor()}, gunnerAt(-12.0f, 0.0f, 0.0f)), withGunner());
    const ct::EntityId gunner = sim.level().entity(0)->id;
    CHECK(sim.state().world.weapons.has(gunner));
    sim.spawn({6.0f, 0.0f, 0.0f});
    float sideways = 0.0f;
    for (int i = 0; i < 4 * kSecond; ++i) {
        sim.run({}, 1);
        sideways = std::max(sideways, std::fabs(enemyTransform(sim).position.y));
    }
    CHECK(sim.health().health < 100);
    CHECK(sim.health().hurtKind == static_cast<std::uint32_t>(ct::DamageKind::Weapon));
    CHECK(sim.health().attacker == gunner);
    // Shots of 5 come in bursts of three.
    CHECK((100 - sim.health().health) % 5 == 0);
    const ct::Vec3 between = sim.transform().position - enemyTransform(sim).position;
    CHECK(std::sqrt(between.x * between.x + between.y * between.y) > 7.0f);
    CHECK(sideways > 0.3f);
    // Its gun never runs dry.
    CHECK(sim.state().world.weapons.get(gunner)->ammo[0] == ct::kMaxAmmo);
}

TEST_CASE("enemies think the same way every time") {
    const auto run = [] {
        SimHarness sim(
            levelWith({arenaFloor(), ct::makeBoxBrush({-0.5f, -15.0f, 0.0f}, {0.5f, 6.0f, 3.0f})},
                      enemyAt(-6.0f, 0.0f, 0.0f), {alertAt(2), gunnerAt(-8.0f, 8.0f, 0.0f)}),
            withGunner());
        sim.spawn({6.0f, 0.0f, 0.0f});
        std::vector<std::uint64_t> hashes;
        for (int i = 0; i < 4 * kSecond; ++i) {
            sim.run({}, 1);
            hashes.push_back(ct::hashState(sim.state()));
        }
        return hashes;
    };
    CHECK(run() == run());
}

TEST_CASE("enemies of kinds the game doesn't have are reported, and left out") {
    const ct::GameLevel noEnemies(levelWith({arenaFloor()}, enemyAt(0.0f, 0.0f, 0.0f)), {}, {});
    REQUIRE(noEnemies.warnings().size() == 1);
    CHECK(noEnemies.warnings()[0].find("no enemies") != std::string::npos);

    ct::LevelEntity unknown = enemyAt(0.0f, 0.0f, 0.0f);
    unknown.properties.push_back({"type", "dragon"});
    const ct::GameLevel level(levelWith({arenaFloor()}, unknown), {}, withCharger());
    REQUIRE(level.warnings().size() == 1);
    CHECK(level.warnings()[0].find("'dragon'") != std::string::npos);
    CHECK(level.startState().world.enemies.size() == 0);

    ct::LevelEntity named = enemyAt(0.0f, 0.0f, 0.0f);
    named.properties.push_back({"type", "charger"});
    const ct::GameLevel good(levelWith({arenaFloor()}, named), {}, withCharger());
    CHECK(good.warnings().empty());
    CHECK(good.startState().world.enemies.size() == 1);
}
