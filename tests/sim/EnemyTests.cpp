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

// A point of a patrol, with its next point and wait, firing on_arrive into the counter "arrived".
ct::LevelEntity patrolPoint(std::string name, float x, float y, std::string next,
                            float wait = 0.0f) {
    return entity(std::string(ct::kPatrolPointClass),
                  {{"origin", std::format("{} {} 0", x, y)},
                   {"name", std::move(name)},
                   {"next", std::move(next)},
                   {"wait", std::format("{}", wait)}},
                  {{"on_arrive", "arrived", "add", 0.0f}});
}

float flatDistance(ct::Vec3 a, ct::Vec3 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

// A closed room, a box of walls 3 m high, with its inside from min to max.
std::vector<ct::Brush> closedRoom(float minX, float minY, float maxX, float maxY) {
    return {ct::makeBoxBrush({minX - 0.5f, minY - 0.5f, 0.0f}, {maxX + 0.5f, minY, 3.0f}),
            ct::makeBoxBrush({minX - 0.5f, maxY, 0.0f}, {maxX + 0.5f, maxY + 0.5f, 3.0f}),
            ct::makeBoxBrush({minX - 0.5f, minY, 0.0f}, {minX, maxY, 3.0f}),
            ct::makeBoxBrush({maxX, minY, 0.0f}, {maxX + 0.5f, maxY, 3.0f})};
}

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

TEST_CASE("enemies and spawns put a little inside the floor stand on it instead") {
    // The floor's top is off the grid, at 0.125; both were put at 0, inside it.
    const ct::Brush floor = ct::makeBoxBrush({-15.0f, -15.0f, -1.0f}, {15.0f, 15.0f, 0.125f});
    ct::Level level = levelWith({floor}, enemyAt(0.0f, 0.0f, 0.0f));
    level.entities.push_back(ct::makePlayerSpawn({6.0f, 0.0f, 0.0f}, 0.0f));
    SimHarness sim(level, withCharger());
    REQUIRE(sim.level().spawns().size() == 1);
    CHECK(sim.level().spawns()[0].feetPosition.z >= 0.125f);
    CHECK(sim.level().spawns()[0].feetPosition.z < 0.13f);
    sim.spawn(sim.level().spawns()[0].feetPosition);
    const float startX = enemyTransform(sim).position.x;
    sim.run({}, kSecond);
    CHECK(enemyTransform(sim).position.x > startX + 1.0f);
}

TEST_CASE("an enemy walks its patrol from point to point, waiting where it is told to") {
    // Out to the east point, where it waits a second, to the west point, and round again.
    ct::LevelEntity guard = enemyAt(0.0f, -6.0f, 0.0f);
    guard.properties.push_back({"patrol", "east"});
    ct::GameSettings settings = withCharger();
    settings.enemies[0].walkSpeed = 2.0f;
    SimHarness sim(levelWith({arenaFloor()}, guard,
                             {patrolPoint("east", 8.0f, -6.0f, "west", 1.0f),
                              patrolPoint("west", -8.0f, -6.0f, "east"), counter("arrived")}),
                   settings);
    CHECK(sim.level().warnings().empty());
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Patrol);
    // It walks rather than runs.
    sim.run({}, kSecond);
    CHECK(enemyTransform(sim).position.x > 1.5f);
    CHECK(enemyTransform(sim).position.x < 2.5f);
    // At the east point after about 4 s, it stands there for its wait, facing the way it came.
    sim.run({}, 3 * kSecond + kSecond / 2);
    CHECK(countOf(sim, 5) == 1);
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
    CHECK(flatDistance(enemyTransform(sim).position, {8.0f, -6.0f, 0.0f}) < 0.6f);
    sim.run({}, kSecond / 2);
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
    // Then on to the west point, 16 m away, and on round.
    float westmost = 8.0f;
    for (int i = 0; i < 12 * kSecond; ++i) {
        sim.run({}, 1);
        westmost = std::min(westmost, enemyTransform(sim).position.x);
    }
    CHECK(westmost < -7.4f);
    CHECK(countOf(sim, 5) == 2);
    CHECK(enemyTransform(sim).position.x > -6.0f);
}

TEST_CASE("an enemy that loses its target searches where it was last seen, then goes back") {
    // The player is shut in a room the enemy can neither see into nor reach.
    std::vector<ct::Brush> brushes = closedRoom(9.0f, 9.0f, 13.0f, 13.0f);
    brushes.push_back(arenaFloor());
    ct::GameSettings settings = withCharger();
    settings.enemies[0].searchTime = 5.0f;
    settings.enemies[0].searchRadius = 5.0f;
    SimHarness sim(levelWith(brushes, enemyAt(-8.0f, -8.0f, 45.0f)), settings);
    const ct::EntityId player = sim.spawn({11.0f, 11.0f, 0.0f});
    sim.run({}, kSecond);
    REQUIRE(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
    // As if it had just lost sight of the player at the middle of the floor, a second ago.
    ct::Enemy& enemy = *sim.state().world.enemies.get(sim.level().entity(0)->id);
    enemy.target = player;
    enemy.state = static_cast<std::uint32_t>(ct::EnemyState::Lost);
    enemy.lastSeen = {0.0f, 0.0f, 0.9f};
    enemy.seenTick = sim.state().tick - static_cast<std::uint32_t>(kSecond);
    // It goes there, and starts searching on arriving.
    int ticks = 0;
    for (; ticks < 4 * kSecond && ct::enemyState(enemyOf(sim)) == ct::EnemyState::Lost; ++ticks) {
        sim.run({}, 1);
    }
    REQUIRE(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Search);
    CHECK(flatDistance(enemyTransform(sim).position, {}) < 1.0f);
    // It looks about, going from spot to spot near there.
    float furthest = 0.0f;
    bool searched = true;
    for (int i = 0; i < 4 * kSecond; ++i) {
        sim.run({}, 1);
        const float away = flatDistance(enemyTransform(sim).position, {});
        furthest = std::max(furthest, away);
        searched = searched && away < 7.0f;
    }
    CHECK(searched);
    CHECK(furthest > 1.5f);
    // Then gives up and walks back to its post, where it faces the way it did.
    sim.run({}, 2 * kSecond);
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Patrol);
    CHECK_FALSE(enemyOf(sim).target.isValid());
    sim.run({}, 10 * kSecond);
    CHECK(ct::enemyState(enemyOf(sim)) == ct::EnemyState::Idle);
    CHECK(flatDistance(enemyTransform(sim).position, {-8.0f, -8.0f, 0.0f}) < 0.6f);
    CHECK(std::abs(static_cast<std::int16_t>(enemyTransform(sim).yaw - 8192)) < 200);
}

TEST_CASE("a gunner runs for cover between bursts, and comes out again to shoot") {
    // A pillar a few metres north of the gunner, which hides it from the player once it is behind
    // the pillar's west side.
    ct::GameSettings settings = withGunner();
    settings.enemies[1].coverChance = 1.0f;
    settings.enemies[1].coverTime = 1.5f;
    settings.enemies[1].coverRange = 8.0f;
    settings.enemies[1].strafeTime = 0.0f;
    const ct::Brush pillar = ct::makeBoxBrush({-8.0f, 2.0f, 0.0f}, {-7.0f, 4.0f, 3.0f});
    SimHarness sim(levelWith({arenaFloor(), pillar}, gunnerAt(-10.0f, 0.0f, 0.0f)), settings);
    sim.spawn({8.0f, 0.0f, 0.0f});
    bool tookCover = false;
    bool hidden = false;
    bool cameOut = false;
    int hurtWhileHidden = 0;
    for (int i = 0; i < 8 * kSecond; ++i) {
        const std::int32_t before = sim.health().health;
        sim.run({}, 1);
        const ct::EnemyState state = ct::enemyState(enemyOf(sim));
        const ct::Vec3 at = enemyTransform(sim).position;
        if (state == ct::EnemyState::Cover) {
            tookCover = true;
            if ((enemyOf(sim).flags & ct::kEnemyAtGoal) != 0) {
                hidden = true;
                // Behind the pillar, from the player's side.
                CHECK(at.x < -8.0f);
                CHECK(at.y > 1.5f);
                hurtWhileHidden += before != sim.health().health ? 1 : 0;
            }
        } else if (hidden && state == ct::EnemyState::Chase) {
            cameOut = true;
        }
    }
    CHECK(tookCover);
    CHECK(hidden);
    CHECK(cameOut);
    CHECK(hurtWhileHidden == 0);
    CHECK(sim.health().health < 100);
}

TEST_CASE("patrols naming what isn't a patrol point are reported") {
    ct::LevelEntity lost = enemyAt(0.0f, 0.0f, 0.0f);
    lost.properties.push_back({"patrol", "nowhere"});
    const ct::GameLevel missing(levelWith({arenaFloor()}, lost), {}, withCharger());
    REQUIRE(missing.warnings().size() == 1);
    CHECK(missing.warnings()[0].find("no entity is named 'nowhere'") != std::string::npos);
    CHECK(ct::enemyState(*missing.startState().world.enemies.get(missing.entity(0)->id)) ==
          ct::EnemyState::Idle);

    ct::LevelEntity confused = enemyAt(0.0f, 0.0f, 0.0f);
    confused.properties.push_back({"patrol", "alerted"});
    const ct::GameLevel wrong(levelWith({arenaFloor()}, confused), {}, withCharger());
    REQUIRE(wrong.warnings().size() == 1);
    CHECK(wrong.warnings()[0].find("is not a patrol point") != std::string::npos);
}

TEST_CASE("enemies think the same way every time") {
    const auto run = [] {
        ct::GameSettings settings = withGunner();
        settings.enemies[1].coverChance = 0.5f;
        ct::LevelEntity guard = enemyAt(8.0f, -10.0f, 180.0f, "guard");
        guard.properties.push_back({"patrol", "a"});
        SimHarness sim(
            levelWith({arenaFloor(), ct::makeBoxBrush({-0.5f, -15.0f, 0.0f}, {0.5f, 6.0f, 3.0f})},
                      enemyAt(-6.0f, 0.0f, 0.0f),
                      {alertAt(2), gunnerAt(-8.0f, 8.0f, 0.0f), guard,
                       patrolPoint("a", 8.0f, -4.0f, "b"), patrolPoint("b", 12.0f, -10.0f, "a"),
                       counter("arrived")}),
            settings);
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

TEST_CASE("an enemy can have settings of its own, and keeps its kind's for the rest") {
    ct::GameSettings settings = withGunner();
    ct::LevelEntity own = enemyAt(3.0f, 4.0f, 0.0f);
    own.properties.push_back({"speed", "9"});
    own.properties.push_back({"health", "250"});
    own.properties.push_back({"height", "3"});
    own.properties.push_back({"weapon", "gun"});
    SimHarness sim(levelWith({arenaFloor()}, own), settings);
    CHECK(sim.level().warnings().empty());
    sim.run({}, 1);
    const ct::World& world = sim.state().world;
    const ct::EntityId id = sim.level().entity(0)->id;
    REQUIRE(world.enemyOverrides.get(id) != nullptr);
    CHECK(world.enemyOverrides.get(id)->count == 4);
    CHECK(world.healths.get(id)->health == 250);
    const ct::MovementSettings movement = ct::movementOf(world, id, settings);
    CHECK(movement.maxGroundSpeed == 9.0f);
    CHECK(movement.halfExtents.z == 1.5f);
    CHECK(std::fabs(enemyTransform(sim).position.z - 1.5f) < 0.01f);
    // The charger has no gun of its own; this one has the gunner's.
    REQUIRE(world.weapons.get(id) != nullptr);
    CHECK(world.weapons.get(id)->current == 0);

    // Its kind changing changes the rest, but not its own.
    settings.enemies[0].sightRange = 12.0f;
    settings.enemies[0].speed = 3.0f;
    const ct::EnemyTuning tuning = *ct::enemyTuning(world, id, settings);
    CHECK(tuning.sightRange == 12.0f);
    CHECK(tuning.speed == 9.0f);

    // Others of its kind are as the kind says.
    SimHarness plain(levelWith({arenaFloor()}, enemyAt(3.0f, 4.0f, 0.0f)), withGunner());
    plain.run({}, 1);
    CHECK(plain.state().world.enemyOverrides.size() == 0);
    CHECK(plain.state().world.weapons.get(plain.level().entity(0)->id) == nullptr);
}

TEST_CASE("an enemy's own settings that won't do are reported, and out of range ones held in it") {
    ct::LevelEntity own = enemyAt(0.0f, 0.0f, 0.0f);
    own.properties.push_back({"speed", "fast"});
    own.properties.push_back({"weapon", "laser"});
    own.properties.push_back({"sight_range", "5000"});
    const ct::GameLevel level(levelWith({arenaFloor()}, own), {}, withGunner());
    REQUIRE(level.warnings().size() == 2);
    CHECK(level.warnings()[0].find("speed 'fast'") != std::string::npos);
    CHECK(level.warnings()[1].find("'laser'") != std::string::npos);
    const ct::World& world = level.startState().world;
    const ct::EntityId id = level.entity(0)->id;
    REQUIRE(world.enemyOverrides.get(id) != nullptr);
    CHECK(world.enemyOverrides.get(id)->count == 1);
    CHECK(ct::enemyTuning(world, id, withGunner())->sightRange == 1000.0f);
}
