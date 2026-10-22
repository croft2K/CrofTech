#include "SimHarness.h"

#include "croftech/level/EntityClasses.h"
#include "croftech/sim/Damage.h"

#include <doctest/doctest.h>

#include <ostream> // doctest prints string_view through operator<<
#include <string>
#include <utility>
#include <vector>

using ct::test::floorBrush;
using ct::test::SimHarness;

namespace {

ct::LevelEntity pickup(std::vector<ct::EntityProperty> properties,
                       std::vector<ct::EntityOutput> outputs = {}) {
    return {std::string(ct::kPickupClass), std::move(properties), std::move(outputs)};
}

ct::Level floorWith(std::vector<ct::LevelEntity> entities) {
    return {{floorBrush()}, std::move(entities)};
}

ct::UserCmd forward() {
    ct::UserCmd cmd;
    cmd.forward = 127;
    return cmd;
}

const ct::Pickup& pickupOf(const SimHarness& sim, std::uint32_t levelEntity) {
    return *sim.state().world.pickups.get(sim.level().entity(levelEntity)->id);
}

bool taken(const SimHarness& sim, std::uint32_t levelEntity) {
    return (pickupOf(sim, levelEntity).flags & ct::kPickupTaken) != 0;
}

// Two weapons: a pistol carried from spawn and a cannon to find.
ct::GameSettings armed() {
    ct::WeaponSettings pistol;
    pistol.name = "pistol";
    pistol.given = true;
    pistol.startAmmo = 10;
    pistol.maxAmmo = 30;
    pistol.pickupAmmo = 12;
    ct::WeaponSettings cannon = pistol;
    cannon.name = "cannon";
    cannon.given = false;
    cannon.pickupAmmo = 4;
    ct::GameSettings settings;
    settings.weapons = {pistol, cannon};
    return settings;
}

} // namespace

TEST_CASE("health pickups heal up to the maximum, and are left for whoever needs them") {
    SimHarness sim(floorWith({pickup({{"origin", "3 0 0"}}), pickup({{"origin", "6 0 0"}})}));
    CHECK(sim.level().warnings().empty());
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.health().health = 60;
    sim.run(forward(), ct::kTickRate / 2);
    CHECK(sim.health().health == 85);
    CHECK(taken(sim, 0));
    CHECK(pickupOf(sim, 0).takenBy == sim.state().world.playerControls.entities()[0]);
    sim.run(forward(), ct::kTickRate);
    // The second tops up to 100 and no more.
    CHECK(sim.health().health == 100);
    CHECK(taken(sim, 1));

    SimHarness full(floorWith({pickup({{"origin", "3 0 0"}})}));
    full.spawn({0.0f, 0.0f, 0.0f});
    full.run(forward(), ct::kTickRate);
    CHECK(full.health().health == 100);
    CHECK_FALSE(taken(full, 0));
}

TEST_CASE("mega health and heavy armour go over the maximum, and the extra drains away") {
    SimHarness sim(floorWith(
        {pickup({{"origin", "2 0 0"}, {"amount", "100"}, {"limit", "200"}}),
         pickup({{"origin", "4 0 0"}, {"item", "armour"}, {"amount", "100"}, {"limit", "200"}})}));
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(forward(), ct::kTickRate);
    CHECK(sim.health().health >= 199);
    CHECK(sim.health().armour >= 99);
    const std::int32_t health = sim.health().health;
    const std::int32_t armour = sim.health().armour;
    sim.run({}, 10 * static_cast<int>(ct::kTickRate));
    // One a second while above the maximum of 100.
    CHECK(sim.health().health == health - 10);
    CHECK(sim.health().armour == armour);
}

TEST_CASE("taken pickups come back after their respawn time, or never") {
    SimHarness sim(floorWith({pickup({{"origin", "0 0 0"}, {"respawn", "2"}}),
                              pickup({{"origin", "0 0 0"}, {"respawn", "-1"}})}));
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.health().health = 10;
    sim.run({}, 1);
    CHECK(taken(sim, 0));
    CHECK(taken(sim, 1));
    CHECK(sim.health().health == 60);
    sim.run({}, 2 * static_cast<int>(ct::kTickRate) - 2);
    CHECK(taken(sim, 0));
    // Back, and taken again at once by the player standing on it, still hurt.
    sim.run({}, 2);
    CHECK(sim.health().health == 85);
    CHECK(taken(sim, 0));
    sim.run({}, 10 * static_cast<int>(ct::kTickRate));
    CHECK(taken(sim, 1));
    CHECK(pickupOf(sim, 1).respawnTick == 0);
}

TEST_CASE("ammo and weapon pickups give the weapons they name") {
    SimHarness sim(
        floorWith(
            {pickup({{"origin", "2 0 0"}, {"item", "ammo"}, {"weapon", "pistol"}}),
             pickup({{"origin", "4 0 0"}, {"item", "weapon"}, {"weapon", "cannon"}}),
             pickup(
                 {{"origin", "6 0 0"}, {"item", "ammo"}, {"weapon", "pistol"}, {"amount", "100"}}),
             pickup({{"origin", "8 0 0"},
                     {"item", "ammo"},
                     {"weapon", "pistol"},
                     {"amount", "100"}})}),
        armed());
    CHECK(sim.level().warnings().empty());
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.run(forward(), ct::kTickRate * 2);
    const ct::Weapons& weapons = sim.weapons();
    // 10 + the pistol's pickup_ammo of 12, then topped up to its most, 30.
    CHECK(weapons.ammo[0] == 30);
    CHECK(ct::carries(weapons, 1));
    CHECK(weapons.ammo[1] == 4);
    // A stronger weapon comes out as soon as it is picked up.
    CHECK(weapons.current == 1);
    CHECK(taken(sim, 0));
    CHECK(taken(sim, 1));
    CHECK(taken(sim, 2));
    // The pistol was full by then.
    CHECK_FALSE(taken(sim, 3));
}

TEST_CASE("on_pickup sets off the wiring, and switched-off pickups can't be taken") {
    SimHarness sim(floorWith(
        {pickup({{"origin", "1.5 0 0"}, {"item", "armour"}}, {{"on_pickup", "prize", "enable"}}),
         pickup({{"origin", "4 0 0"}, {"name", "prize"}, {"start_disabled", "1"}})}));
    sim.spawn({0.0f, 0.0f, 0.0f});
    sim.health().health = 50;
    sim.run({}, 1);
    CHECK((pickupOf(sim, 1).flags & ct::kLogicEnabled) == 0);
    sim.run(forward(), ct::kTickRate);
    CHECK(taken(sim, 0));
    CHECK(taken(sim, 1));
    CHECK(sim.health().health == 75);

    SimHarness hidden(floorWith({pickup({{"origin", "2 0 0"}, {"start_disabled", "1"}})}));
    hidden.spawn({0.0f, 0.0f, 0.0f});
    hidden.health().health = 50;
    hidden.run(forward(), ct::kTickRate);
    CHECK_FALSE(taken(hidden, 0));
    CHECK(hidden.health().health == 50);
}

TEST_CASE("the dead pick nothing up") {
    SimHarness sim(floorWith({pickup({{"origin", "0 0 0"}})}));
    const ct::EntityId player = sim.spawn({0.0f, 0.0f, 0.0f});
    ct::kill(sim.state(), player, ct::DamageKind::Generic);
    sim.run({}, ct::kTickRate);
    CHECK_FALSE(taken(sim, 0));
}

TEST_CASE("pickups that name no weapon, or one the game doesn't have, are reported") {
    const ct::GameLevel level(
        floorWith({pickup({{"origin", "0 0 0"}, {"item", "ammo"}}),
                   pickup({{"origin", "0 0 0"}, {"item", "weapon"}, {"weapon", "bfg"}}),
                   pickup({{"origin", "0 0 0"}, {"item", "weapon"}, {"weapon", "cannon"}})}),
        {}, armed().weapons);
    REQUIRE(level.warnings().size() == 2);
    CHECK(level.warnings()[0].find("needs a weapon") != std::string::npos);
    CHECK(level.warnings()[1].find("'bfg'") != std::string::npos);
    CHECK(level.entity(2)->weapon == 1);
}
