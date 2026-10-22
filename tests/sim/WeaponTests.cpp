#include "SimHarness.h"

#include "croftech/core/Angle.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/Damage.h"
#include "croftech/sim/Firing.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using ct::test::floorBrush;
using ct::test::SimHarness;

namespace {

// Four shots a second, one at a time, straight.
ct::WeaponSettings rifle() {
    ct::WeaponSettings weapon;
    weapon.damage = 10;
    weapon.range = 100.0f;
    weapon.fireInterval = 0.25f;
    weapon.raiseTime = 0.25f;
    weapon.knockback = 2.0f;
    weapon.startAmmo = 5;
    weapon.maxAmmo = 10;
    weapon.given = true;
    return weapon;
}

// Ten pellets a shot, up to 10 degrees off.
ct::WeaponSettings scatter() {
    ct::WeaponSettings weapon;
    weapon.damage = 5;
    weapon.pellets = 10;
    weapon.spread = 10.0f;
    weapon.fireInterval = 1.0f;
    weapon.raiseTime = 0.5f;
    weapon.startAmmo = 3;
    weapon.given = true;
    return weapon;
}

ct::WeaponSettings notGiven() {
    ct::WeaponSettings weapon = rifle();
    weapon.given = false;
    return weapon;
}

ct::GameSettings armed(std::vector<ct::WeaponSettings> weapons) {
    ct::GameSettings settings;
    settings.weapons = std::move(weapons);
    return settings;
}

ct::Level floorWith(std::vector<ct::Brush> more = {}) {
    ct::Level level{{floorBrush()}, {}};
    for (ct::Brush& brush : more) {
        level.brushes.push_back(std::move(brush));
    }
    return level;
}

ct::UserCmd firing() {
    ct::UserCmd cmd;
    cmd.buttons = ct::kButtonAttack;
    return cmd;
}

ct::UserCmd choosing(std::uint8_t weapon, std::uint16_t buttons = 0) {
    ct::UserCmd cmd;
    cmd.weapon = weapon;
    cmd.buttons = buttons;
    return cmd;
}

// Waits for the weapon in hand to be raised after spawning.
void ready(SimHarness& sim) { sim.run({}, ct::kTickRate); }

const ct::Health& healthOf(const SimHarness& sim, ct::EntityId id) {
    return *sim.state().world.healths.get(id);
}

} // namespace

TEST_CASE("players spawn carrying the weapons given, with their starting ammo, the first in hand") {
    SimHarness sim(floorWith(), armed({notGiven(), rifle(), scatter()}));
    sim.spawn({0.0f, 0.0f, 0.0f});
    const ct::Weapons& weapons = sim.weapons();
    CHECK(weapons.owned == 0b110u);
    CHECK(weapons.current == 1);
    CHECK(weapons.ammo[0] == 0);
    CHECK(weapons.ammo[1] == 5);
    CHECK(weapons.ammo[2] == 3);
}

TEST_CASE("holding fire shoots at the weapon's rate until the ammo runs out") {
    SimHarness sim(floorWith(), armed({rifle()}));
    const ct::EntityId shooter = sim.spawn({0.0f, 0.0f, 0.0f});
    const ct::EntityId target = sim.spawn({5.0f, 0.0f, 0.0f}, 0, 1);
    ready(sim);

    sim.run(firing(), 1);
    const ct::Health& hurt = healthOf(sim, target);
    CHECK(hurt.health == 90);
    CHECK(hurt.hurtKind == static_cast<std::uint32_t>(ct::DamageKind::Weapon));
    CHECK(hurt.attacker == shooter);
    // Knocked along the shot.
    CHECK(sim.state().world.playerMotions.get(target)->velocity.x == doctest::Approx(2.0f));
    CHECK(sim.weapons().ammo[0] == 4);
    CHECK(sim.weapons().firedTick == sim.state().tick);

    // A quarter of a second is 32 ticks.
    sim.run(firing(), 31);
    CHECK(healthOf(sim, target).health == 90);
    sim.run(firing(), 1);
    CHECK(healthOf(sim, target).health == 80);

    sim.run(firing(), 4 * ct::kTickRate);
    CHECK(sim.weapons().ammo[0] == 0);
    CHECK(healthOf(sim, target).health == 50);
}

TEST_CASE("shots stop at walls") {
    const ct::Brush wall = ct::makeBoxBrush({2.0f, -2.0f, 0.0f}, {2.5f, 2.0f, 3.0f});
    SimHarness sim(floorWith({wall}), armed({rifle()}));
    sim.spawn({0.0f, 0.0f, 0.0f});
    const ct::EntityId target = sim.spawn({5.0f, 0.0f, 0.0f}, 0, 1);
    ready(sim);
    sim.run(firing(), 1);
    CHECK(sim.weapons().ammo[0] == 4);
    CHECK(healthOf(sim, target).health == 100);
}

TEST_CASE("a shot hits the nearest living target's box, never the shooter's") {
    SimHarness sim(floorWith(), armed({}));
    const ct::EntityId shooter = sim.spawn({0.0f, 0.0f, 0.0f});
    const ct::EntityId nearer = sim.spawn({5.0f, 0.0f, 0.0f}, 0, 1);
    const ct::EntityId further = sim.spawn({8.0f, 0.0f, 0.0f}, 0, 2);
    const ct::CollisionScene scene(sim.level(), sim.state().world);
    const ct::Vec3 start{0.0f, 0.0f, 1.0f};
    const ct::Vec3 end{20.0f, 0.0f, 1.0f};

    ct::ShotHit hit = ct::traceShot(scene, sim.state().world, sim.movement(), start, end, shooter);
    CHECK(hit.target == nearer);
    // The near face of its box: 5 m less half the 0.8 m width.
    CHECK(hit.position.x == doctest::Approx(4.6f));
    CHECK(hit.fraction == doctest::Approx(4.6f / 20.0f));

    ct::kill(sim.state(), nearer, ct::DamageKind::Generic);
    hit = ct::traceShot(scene, sim.state().world, sim.movement(), start, end, shooter);
    CHECK(hit.target == further);

    // Passing over everyone's heads, it reaches the end.
    hit = ct::traceShot(scene, sim.state().world, sim.movement(), {0.0f, 0.0f, 3.0f},
                        {20.0f, 0.0f, 3.0f}, shooter);
    CHECK_FALSE(hit.target.isValid());
    CHECK(hit.fraction == 1.0f);

    // Straight down, it stops at the floor, facing up.
    hit = ct::traceShot(scene, sim.state().world, sim.movement(), {-3.0f, 0.0f, 1.0f},
                        {-3.0f, 0.0f, -5.0f}, shooter);
    CHECK_FALSE(hit.target.isValid());
    CHECK(hit.position.z == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(hit.normal.z == doctest::Approx(1.0f));
}

TEST_CASE("pellets stray up to the spread from the aim, all around it") {
    ct::Transform aim;
    aim.yaw = 12000;
    aim.pitch = -3000;
    const ct::Vec3 forward = ct::viewForward(aim);
    const float cosSpread = ct::sinCos(static_cast<std::uint16_t>(10 * 65536 / 360)).cos;
    std::uint32_t random = 7;
    int wide = 0;
    float meanUp = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        const ct::Vec3 direction = ct::pelletDirection(aim, 10.0f, random);
        REQUIRE(ct::length(direction) == doctest::Approx(1.0f));
        REQUIRE(ct::dot(direction, forward) >= cosSpread - 1e-5f);
        wide += ct::dot(direction, forward) < ct::sinCos(std::uint16_t{5 * 65536 / 360}).cos;
        meanUp += direction.z - forward.z;
    }
    CHECK(random == 7 + 2 * 2000);
    // Even over the circle's area, three quarters land beyond half the spread.
    CHECK(wide > 1350);
    CHECK(wide < 1650);
    CHECK(meanUp / 2000.0f == doctest::Approx(0.0f).epsilon(0.01));

    std::uint32_t unchanged = 3;
    CHECK(ct::pelletDirection(aim, 0.0f, unchanged) == forward);
    CHECK(unchanged == 3);
}

TEST_CASE("pellets spread at random, and the same inputs repeat the same spread") {
    const auto shoot = [] {
        SimHarness sim(floorWith(), armed({scatter()}));
        sim.spawn({0.0f, 0.0f, 0.0f});
        const ct::EntityId target = sim.spawn({5.0f, 0.0f, 0.0f}, 0, 1);
        ready(sim);
        sim.run(firing(), 1);
        return healthOf(sim, target).health;
    };
    const std::int32_t health = shoot();
    // At 5 m the pellets cover a circle 1.8 m wide; the target is 0.8 m wide.
    CHECK(health > 50);
    CHECK(health < 100);
    CHECK(shoot() == health);
}

TEST_CASE("choosing a weapon raises it before it fires; ones not carried or empty are refused") {
    SimHarness sim(floorWith(), armed({rifle(), scatter(), notGiven()}));
    sim.spawn({0.0f, 0.0f, 0.0f});
    ready(sim);

    sim.run(choosing(2, ct::kButtonAttack), 1);
    CHECK(sim.weapons().current == 1);
    CHECK(sim.weapons().raisedTick == sim.state().tick);
    // Half a second to raise: 64 ticks.
    sim.run(firing(), 63);
    CHECK(sim.weapons().ammo[1] == 3);
    sim.run(firing(), 1);
    CHECK(sim.weapons().ammo[1] == 2);

    // Not carried.
    sim.run(choosing(3), 1);
    CHECK(sim.weapons().current == 1);
    // Already in hand: nothing changes, so repeating a choice is harmless.
    const ct::TickIndex raised = sim.weapons().raisedTick;
    sim.run(choosing(2), 5);
    CHECK(sim.weapons().raisedTick == raised);
    // Empty.
    sim.weapons().ammo[0] = 0;
    sim.run(choosing(1), 1);
    CHECK(sim.weapons().current == 1);
}

TEST_CASE("firing an empty weapon changes to the best one with ammo") {
    ct::WeaponSettings last = rifle();
    last.startAmmo = 1;
    SimHarness sim(floorWith(), armed({scatter(), last, notGiven()}));
    sim.spawn({0.0f, 0.0f, 0.0f});
    ready(sim);
    sim.run(choosing(2), 1);
    sim.run(firing(), ct::kTickRate / 4);
    CHECK(sim.weapons().current == 1);
    CHECK(sim.weapons().ammo[1] == 0);
    // Its next shot finds it empty, and the scatter gun, the best left, comes out and fires.
    sim.run(firing(), ct::kTickRate);
    CHECK(sim.weapons().current == 0);
    CHECK(sim.weapons().ammo[0] == 2);
}

TEST_CASE("weapons that never run out keep firing") {
    ct::WeaponSettings blaster = rifle();
    blaster.ammoPerShot = 0;
    blaster.startAmmo = 0;
    SimHarness sim(floorWith(), armed({blaster}));
    sim.spawn({0.0f, 0.0f, 0.0f});
    const ct::EntityId target = sim.spawn({5.0f, 0.0f, 0.0f}, 0, 1);
    ready(sim);
    sim.run(firing(), 4 * 32);
    CHECK(healthOf(sim, target).health == 60);
}

TEST_CASE("the dead don't shoot, and come back armed as at spawn") {
    ct::RulesSettings rules;
    rules.respawnDelay = 0.0f;
    ct::GameSettings settings = armed({rifle()});
    settings.rules = rules;
    ct::Level level = floorWith();
    level.entities.push_back(ct::makePlayerSpawn({-20.0f, 0.0f, 0.0f}, 0.0f));
    SimHarness sim(level, settings);
    const ct::EntityId shooter = sim.spawn({0.0f, 0.0f, 0.0f});
    const ct::EntityId target = sim.spawn({5.0f, 0.0f, 0.0f}, 0, 1);
    ready(sim);
    sim.run(firing(), 1);
    sim.run({}, 1);
    CHECK(sim.weapons().ammo[0] == 4);

    ct::kill(sim.state(), shooter, ct::DamageKind::Generic);
    // Pressing fire respawns; holding it then waits for the weapon to be raised.
    sim.run(firing(), 1);
    CHECK_FALSE(ct::isDead(sim.health()));
    CHECK(sim.weapons().ammo[0] == 5);
    CHECK(sim.weapons().readyTick == sim.state().tick + 32);
    CHECK(healthOf(sim, target).health == 90);
}

TEST_CASE("two players shooting each other give the same result every run") {
    const auto duel = [] {
        SimHarness sim(floorWith(), armed({rifle(), scatter()}));
        sim.spawn({0.0f, 0.0f, 0.0f});
        sim.spawn({6.0f, 1.0f, 0.0f}, 32768, 1);
        ct::InputSet inputs;
        std::vector<std::uint64_t> hashes;
        for (int tick = 0; tick < 3 * static_cast<int>(ct::kTickRate); ++tick) {
            inputs.commands[0] = choosing(tick > 200 ? 2 : 0, ct::kButtonAttack);
            inputs.commands[0].yaw = static_cast<std::uint16_t>(500 * (tick % 5));
            inputs.commands[0].side = tick % 64 < 32 ? 127 : -127;
            inputs.commands[1] = choosing(2, ct::kButtonAttack);
            inputs.commands[1].yaw = 32768;
            sim.runInputs(inputs, 1);
            hashes.push_back(ct::hashState(sim.state()));
        }
        return hashes;
    };
    CHECK(duel() == duel());
}
