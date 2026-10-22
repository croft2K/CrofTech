#include "SimHarness.h"

#include "croftech/client/GameEvents.h"
#include "croftech/level/EntityClasses.h"
#include "croftech/sim/Damage.h"
#include "croftech/sim/Enemies.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using ct::GameEventKind;
using ct::test::floorBrush;
using ct::test::SimHarness;

namespace {

// Runs ticks of the command, giving the events every state, and returns what they saw.
std::vector<ct::GameEvent> watch(SimHarness& sim, ct::GameEvents& events, const ct::UserCmd& cmd,
                                 int ticks, std::uint32_t timeline = 0) {
    std::vector<ct::GameEvent> seen;
    for (int i = 0; i < ticks; ++i) {
        sim.run(cmd, 1);
        events.update(&sim.state(), timeline, sim.level(), sim.settings(), seen);
    }
    return seen;
}

const ct::GameEvent& first(const std::vector<ct::GameEvent>& events, GameEventKind kind) {
    return *std::find_if(events.begin(), events.end(),
                         [kind](const ct::GameEvent& e) { return e.kind == kind; });
}

std::size_t count(const std::vector<ct::GameEvent>& events, GameEventKind kind) {
    return static_cast<std::size_t>(std::count_if(
        events.begin(), events.end(), [kind](const ct::GameEvent& e) { return e.kind == kind; }));
}

ct::UserCmd pressing(std::uint16_t buttons, std::int8_t forward = 0) {
    ct::UserCmd cmd;
    cmd.buttons = buttons;
    cmd.forward = forward;
    return cmd;
}

} // namespace

TEST_CASE("walking makes footsteps every stride, and a jump a jump then a landing") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 0.0f});
    ct::GameEvents events;
    const std::vector<ct::GameEvent> walked = watch(sim, events, pressing(0, 127), ct::kTickRate);
    // About 7 m in a second, speeding up from a standstill.
    CHECK(count(walked, GameEventKind::Footstep) == 3);
    CHECK(count(walked, GameEventKind::Jump) == 0);

    const std::vector<ct::GameEvent> jumped =
        watch(sim, events, pressing(ct::kButtonJump), ct::kTickRate);
    CHECK(count(jumped, GameEventKind::Jump) == 1);
    REQUIRE(count(jumped, GameEventKind::Land) == 1);
    const auto land = std::find_if(jumped.begin(), jumped.end(), [](const ct::GameEvent& e) {
        return e.kind == GameEventKind::Land;
    });
    // A jump lands as fast as it took off.
    CHECK(land->strength == doctest::Approx(sim.movement().jumpSpeed).epsilon(0.05));
    CHECK(land->position.z == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("hurt, death and respawn are seen in the states") {
    SimHarness sim(ct::test::floorLevel());
    const ct::EntityId player = sim.spawn({0.0f, 0.0f, 0.0f});
    ct::GameEvents events;
    watch(sim, events, {}, 1);
    ct::Damage damage;
    damage.amount = 30;
    ct::applyDamage(sim.state(), sim.settings().rules, player, damage);
    std::vector<ct::GameEvent> seen = watch(sim, events, {}, 1);
    REQUIRE(count(seen, GameEventKind::Hurt) == 1);
    CHECK(seen.front().strength == 30.0f);
    CHECK(seen.front().entity == player);

    ct::kill(sim.state(), player, ct::DamageKind::Generic);
    seen = watch(sim, events, {}, 2 * ct::kTickRate);
    CHECK(count(seen, GameEventKind::Death) == 1);
    CHECK(count(seen, GameEventKind::Hurt) == 0);
    seen = watch(sim, events, pressing(ct::kButtonAttack), 4);
    CHECK(count(seen, GameEventKind::Respawn) == 1);
    CHECK(count(seen, GameEventKind::Footstep) == 0);
}

TEST_CASE("movers starting and stopping, and checkpoints reached, are events") {
    ct::Brush door = ct::makeBoxBrush({3.0f, -1.0f, 0.0f}, {3.25f, 1.0f, 2.0f});
    door.entity = 0;
    ct::Brush volume = ct::makeBoxBrush({-1.0f, 3.0f, 0.0f}, {1.0f, 5.0f, 2.0f});
    volume.entity = 1;
    ct::Level level{{floorBrush(), door, volume},
                    {{"mover", {{"move", "0 0 2"}, {"activate", "touch"}, {"wait", "0.5"}}},
                     {"checkpoint", {}}}};
    SimHarness sim(level);
    sim.spawn({0.0f, 0.0f, 0.0f});
    ct::GameEvents events;
    std::vector<ct::GameEvent> seen = watch(sim, events, pressing(0, 127), ct::kTickRate / 2);
    REQUIRE(count(seen, GameEventKind::MoverStart) == 1);
    const auto start = std::find_if(seen.begin(), seen.end(), [](const ct::GameEvent& e) {
        return e.kind == GameEventKind::MoverStart;
    });
    CHECK(start->levelEntity == 0);
    CHECK(start->position.x == doctest::Approx(3.125f));
    seen = watch(sim, events, {}, ct::kTickRate);
    CHECK(count(seen, GameEventKind::MoverStop) >= 1);

    sim.place({0.0f, 4.0f, 0.0f});
    seen = watch(sim, events, {}, 2);
    CHECK(count(seen, GameEventKind::CheckpointReached) == 1);
}

TEST_CASE("a new timeline starts afresh, with no events for the jump back") {
    SimHarness sim({floorBrush()});
    sim.spawn({0.0f, 0.0f, 0.0f});
    ct::GameEvents events;
    watch(sim, events, pressing(0, 127), ct::kTickRate);
    // A loaded save moves the player and puts them in the air; none of that is an event.
    sim.place({-20.0f, 0.0f, 5.0f});
    const std::vector<ct::GameEvent> seen = watch(sim, events, {}, 1, 1);
    CHECK(seen.empty());
}

TEST_CASE("a rocket going off is one explosion, where it went off") {
    ct::WeaponSettings rocket;
    rocket.given = true;
    rocket.startAmmo = 5;
    rocket.raiseTime = 0.0f;
    rocket.projectileSpeed = 20.0f;
    rocket.splashRadius = 2.0f;
    ct::GameSettings settings;
    settings.weapons = {rocket};
    SimHarness sim(
        ct::Level{{floorBrush(), ct::makeBoxBrush({6.0f, -5.0f, 0.0f}, {7.0f, 5.0f, 4.0f})}, {}},
        settings);
    sim.spawn({0.0f, 0.0f, 0.0f});
    ct::GameEvents events;
    watch(sim, events, {}, 1);
    watch(sim, events, pressing(ct::kButtonAttack), 1);
    const std::vector<ct::GameEvent> seen = watch(sim, events, {}, ct::kTickRate);
    REQUIRE(count(seen, GameEventKind::Explosion) == 1);
    const auto blast = std::find_if(seen.begin(), seen.end(), [](const ct::GameEvent& e) {
        return e.kind == GameEventKind::Explosion;
    });
    CHECK(blast->position.x == doctest::Approx(6.0f).epsilon(0.01));
    CHECK(blast->position.z == doctest::Approx(sim.movement().eyeHeight).epsilon(0.01));
}

TEST_CASE("the client finds the pellets that hit just where the server's shot put them") {
    ct::WeaponSettings scatter;
    scatter.given = true;
    scatter.startAmmo = 5;
    scatter.raiseTime = 0.0f;
    scatter.damage = 5;
    scatter.pellets = 12;
    scatter.spread = 5.0f;
    ct::WeaponSettings other = scatter;
    ct::GameSettings settings;
    settings.weapons = {scatter, other};
    const ct::Brush wall = ct::makeBoxBrush({12.0f, -10.0f, 0.0f}, {13.0f, 10.0f, 5.0f});
    SimHarness sim(ct::Level{{floorBrush(), wall}, {}}, settings);
    const ct::EntityId shooter = sim.spawn({0.0f, 0.0f, 0.0f});
    const ct::EntityId target = sim.spawn({4.0f, 0.0f, 0.0f}, 0, 1);
    ct::GameEvents events;
    watch(sim, events, {}, 1);

    std::vector<ct::GameEvent> seen = watch(sim, events, pressing(ct::kButtonAttack), 1);
    REQUIRE(count(seen, GameEventKind::Fire) == 1);
    CHECK(first(seen, GameEventKind::Fire).entity == shooter);
    CHECK(first(seen, GameEventKind::Fire).weapon == 0);
    // Every pellet stops somewhere: on the target, or on the wall behind it.
    CHECK(count(seen, GameEventKind::Impact) == 12);
    const auto hits = std::count_if(seen.begin(), seen.end(), [](const ct::GameEvent& e) {
        return e.kind == GameEventKind::Impact && e.strength > 0.0f;
    });
    const std::int32_t lost = 100 - sim.state().world.healths.get(target)->health;
    CHECK(hits > 0);
    CHECK(lost == 5 * hits);
    for (const ct::GameEvent& e : seen) {
        if (e.kind == GameEventKind::Impact && e.strength == 0.0f) {
            CHECK(e.position.x == doctest::Approx(12.0f).epsilon(0.01));
        }
    }

    ct::UserCmd choose;
    choose.weapon = 2;
    seen = watch(sim, events, choose, 1);
    REQUIRE(count(seen, GameEventKind::WeaponSwitch) == 1);
    CHECK(first(seen, GameEventKind::WeaponSwitch).weapon == 1);
}

TEST_CASE("taking a pickup and its coming back are events, credited to who took it") {
    ct::Level level{{floorBrush()},
                    {{"pickup", {{"origin", "2 0 0"}, {"item", "armour"}, {"respawn", "1"}}}}};
    SimHarness sim(level);
    const ct::EntityId player = sim.spawn({0.0f, 0.0f, 0.0f});
    ct::GameEvents events;
    std::vector<ct::GameEvent> seen = watch(sim, events, pressing(0, 127), ct::kTickRate / 2);
    REQUIRE(count(seen, GameEventKind::Pickup) == 1);
    CHECK(first(seen, GameEventKind::Pickup).entity == player);
    CHECK(first(seen, GameEventKind::Pickup).levelEntity == 0);
    CHECK(first(seen, GameEventKind::Pickup).position.x == doctest::Approx(2.0f));
    seen = watch(sim, events, {}, ct::kTickRate);
    CHECK(count(seen, GameEventKind::PickupRespawn) == 1);
}

TEST_CASE("an enemy noticing a player and swinging at them are events, marked with its kind") {
    ct::GameSettings settings;
    ct::EnemySettings charger;
    charger.name = "charger";
    charger.meleeDamage = 10;
    charger.movement = ct::enemyMovement(charger, settings.movement);
    settings.enemies = {charger};
    // Facing away from the player, until the player hurts it.
    const ct::LevelEntity enemy{std::string(ct::kEnemyClass),
                                {{"origin", "-6 0 0"}, {"yaw", "180"}}};
    SimHarness sim(
        ct::Level{{ct::makeBoxBrush({-15.0f, -15.0f, -1.0f}, {15.0f, 15.0f, 0.0f})}, {enemy}},
        settings);
    const ct::EntityId player = sim.spawn({6.0f, 0.0f, 0.0f});
    const ct::EntityId enemyId = sim.level().entity(0)->id;
    ct::GameEvents events;
    CHECK(count(watch(sim, events, {}, ct::kTickRate / 2), GameEventKind::EnemyAlert) == 0);
    ct::Damage damage;
    damage.amount = 10;
    damage.attacker = player;
    ct::applyDamage(sim.state(), sim.settings().rules, enemyId, damage);
    const std::vector<ct::GameEvent> seen = watch(sim, events, {}, 3 * ct::kTickRate);

    REQUIRE(count(seen, GameEventKind::EnemyAlert) == 1);
    const ct::GameEvent& alert = first(seen, GameEventKind::EnemyAlert);
    CHECK(alert.entity == enemyId);
    CHECK(alert.enemyType == 0);
    CHECK(alert.position.z == doctest::Approx(charger.height).epsilon(0.01));
    REQUIRE(count(seen, GameEventKind::EnemySwing) >= 1);
    CHECK(first(seen, GameEventKind::EnemySwing).entity == enemyId);
    // Its hurts and footsteps are its own; the player's are a player's.
    const auto has = [&](GameEventKind kind, ct::EntityId who, std::uint32_t type) {
        return std::any_of(seen.begin(), seen.end(), [&](const ct::GameEvent& e) {
            return e.kind == kind && e.entity == who && e.enemyType == type;
        });
    };
    CHECK(has(GameEventKind::Hurt, enemyId, 0));
    CHECK(has(GameEventKind::Footstep, enemyId, 0));
    CHECK(has(GameEventKind::Hurt, player, ct::kNoEnemyType));
}
