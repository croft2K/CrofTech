#include "croftech/sim/GameSettings.h"

#include "croftech/core/Assert.h"
#include "croftech/sim/MovementConfig.h"
#include "croftech/sim/World.h"

#include <algorithm>
#include <string>

namespace ct {
namespace {

// Large enough for any game, small enough that adding damage can't overflow.
constexpr int kMaxHealthSetting = 100000;

struct WeaponInt {
    std::string_view key;
    std::int32_t WeaponSettings::* field;
    int min;
    int max;
    std::string_view description;
};

struct WeaponFloat {
    std::string_view key;
    float WeaponSettings::* field;
    float min;
    float max;
    std::string_view description;
};

constexpr WeaponInt kWeaponInts[] = {
    {"damage", &WeaponSettings::damage, 0, kMaxHealthSetting,
     "each pellet or projectile that hits"},
    {"pellets", &WeaponSettings::pellets, 1, 64, "traces per shot"},
    {"ammo_per_shot", &WeaponSettings::ammoPerShot, 0, kMaxAmmo, "0 never runs out"},
    {"start_ammo", &WeaponSettings::startAmmo, 0, kMaxAmmo, "ammo carried at spawn"},
    {"max_ammo", &WeaponSettings::maxAmmo, 0, kMaxAmmo, "most ammo it holds"},
    {"pickup_ammo", &WeaponSettings::pickupAmmo, 0, kMaxAmmo,
     "ammo in an ammo or weapon pickup that doesn't say how much"},
    {"splash_damage", &WeaponSettings::splashDamage, 0, kMaxHealthSetting,
     "at the centre of a projectile's explosion, less with distance"},
};

constexpr WeaponFloat kWeaponFloats[] = {
    {"spread", &WeaponSettings::spread, 0.0f, 45.0f, "degrees a pellet may stray from the aim"},
    {"range", &WeaponSettings::range, 1.0f, 1000.0f, "m"},
    {"fire_interval", &WeaponSettings::fireInterval, kTickSeconds, 10.0f,
     "seconds between shots while fire is held"},
    {"raise_time", &WeaponSettings::raiseTime, 0.0f, 5.0f,
     "seconds after switching to it before it fires"},
    {"knockback", &WeaponSettings::knockback, 0.0f, 100.0f,
     "m/s each pellet that hits adds to the target, along the shot"},
    {"projectile_speed", &WeaponSettings::projectileSpeed, 0.0f, 200.0f,
     "m/s; 0 traces each pellet at once (hitscan)"},
    {"projectile_gravity", &WeaponSettings::projectileGravity, 0.0f, 10.0f,
     "share of movement gravity that pulls a projectile down"},
    {"bounce", &WeaponSettings::bounce, 0.0f, 1.0f,
     "share of its speed a projectile keeps bouncing off the world; 0 explodes on it"},
    {"fuse", &WeaponSettings::fuse, 0.1f, 60.0f, "seconds before a projectile explodes by itself"},
    {"splash_radius", &WeaponSettings::splashRadius, 0.0f, 50.0f, "m"},
    {"splash_knockback", &WeaponSettings::splashKnockback, 0.0f, 100.0f,
     "m/s at the centre of the explosion, away from it"},
    {"self_damage", &WeaponSettings::selfDamage, 0.0f, 1.0f,
     "share of its splash damage the shooter takes"},
};

struct EnemyInt {
    std::string_view key;
    std::int32_t EnemySettings::* field;
    int min;
    int max;
    std::string_view description;
};

struct EnemyFloat {
    std::string_view key;
    float EnemySettings::* field;
    float min;
    float max;
    std::string_view description;
};

constexpr EnemyInt kEnemyInts[] = {
    {"health", &EnemySettings::health, 1, kMaxHealthSetting, "health it starts with"},
    {"melee_damage", &EnemySettings::meleeDamage, 0, kMaxHealthSetting,
     "each swing that lands; 0 has no melee attack"},
    {"burst", &EnemySettings::burst, 1, 100,
     "shots in a row, each after its weapon's fire_interval"},
};

constexpr EnemyFloat kEnemyFloats[] = {
    {"width", &EnemySettings::width, 0.2f, 8.0f, "of its box, m"},
    {"height", &EnemySettings::height, 0.2f, 8.0f, "of its box, m"},
    {"eye_height", &EnemySettings::eyeHeight, 0.0f, 8.0f, "above its feet, where it sees from"},
    {"speed", &EnemySettings::speed, 0.0f, 50.0f, "m/s running"},
    {"acceleration", &EnemySettings::acceleration, 0.0f, 100.0f,
     "as movement's ground_acceleration"},
    {"turn_speed", &EnemySettings::turnSpeed, 10.0f, 3600.0f, "degrees a second"},
    {"sight_range", &EnemySettings::sightRange, 0.0f, 1000.0f, "m"},
    {"sight_angle", &EnemySettings::sightAngle, 1.0f, 360.0f,
     "degrees across its view, until it is alerted"},
    {"reaction_time", &EnemySettings::reactionTime, 0.0f, 10.0f,
     "seconds from noticing a player to going after them"},
    {"hearing_range", &EnemySettings::hearingRange, 0.0f, 1000.0f,
     "m within which it hears a player's shots"},
    {"footstep_range", &EnemySettings::footstepRange, 0.0f, 1000.0f,
     "m within which it hears a player running"},
    {"attack_range", &EnemySettings::attackRange, 0.0f, 1000.0f,
     "m within which it shoots at targets it can see"},
    {"keep_distance", &EnemySettings::keepDistance, 0.0f, 1000.0f,
     "m it stops closing in at while it can shoot"},
    {"aim_error", &EnemySettings::aimError, 0.0f, 45.0f, "degrees its aim may be off by"},
    {"burst_pause", &EnemySettings::burstPause, 0.0f, 30.0f, "seconds between bursts"},
    {"strafe_time", &EnemySettings::strafeTime, 0.0f, 10.0f,
     "seconds it sidesteps one way while shooting; 0 stands still"},
    {"melee_range", &EnemySettings::meleeRange, 0.0f, 10.0f,
     "m from its box to the target's that a swing reaches"},
    {"melee_windup", &EnemySettings::meleeWindup, 0.0f, 5.0f,
     "seconds from starting a swing to the hit"},
    {"melee_interval", &EnemySettings::meleeInterval, kTickSeconds, 10.0f,
     "seconds between the starts of swings"},
    {"melee_knockback", &EnemySettings::meleeKnockback, 0.0f, 100.0f,
     "m/s a swing that lands adds to the target, away from it"},
    {"walk_speed", &EnemySettings::walkSpeed, 0.0f, 50.0f,
     "m/s on patrol and going back to its post"},
    {"search_time", &EnemySettings::searchTime, 0.0f, 600.0f,
     "seconds it searches where it lost its target before going back; 0 goes straight back"},
    {"search_radius", &EnemySettings::searchRadius, 0.0f, 100.0f,
     "m around where it lost its target that it searches"},
    {"cover_chance", &EnemySettings::coverChance, 0.0f, 1.0f,
     "chance it runs for cover each time it is hurt or ends a burst; 0 never does"},
    {"cover_time", &EnemySettings::coverTime, 0.0f, 30.0f,
     "seconds it stays in cover before coming out again"},
    {"cover_range", &EnemySettings::coverRange, 0.0f, 50.0f, "m it looks for cover within"},
    {"corpse_time", &EnemySettings::corpseTime, -1.0f, 600.0f,
     "seconds its body stays after death; -1 stays for good"},
};

std::string cvarName(std::string_view key) { return std::string(kRulesCVarPrefix).append(key); }

const CVar& findCVar(const Console& console, const std::string& name) {
    const CVar* cvar = console.find(name);
    CT_ASSERT(cvar != nullptr);
    return *cvar;
}

std::span<const std::string> usable(std::span<const std::string> weapons) {
    return weapons.first(std::min<std::size_t>(weapons.size(), kMaxWeapons));
}

std::span<const std::string> usableEnemies(std::span<const std::string> enemies) {
    return enemies.first(std::min<std::size_t>(enemies.size(), kMaxEnemyTypes));
}

void registerEnemyCVars(Console& console, std::string_view enemy) {
    const std::string prefix = enemyCVarPrefix(enemy);
    const EnemySettings defaults;
    for (const EnemyInt& setting : kEnemyInts) {
        console.add(CVar::makeInt(prefix + std::string(setting.key), defaults.*setting.field,
                                  setting.min, setting.max, std::string(setting.description)));
    }
    for (const EnemyFloat& setting : kEnemyFloats) {
        console.add(CVar::makeFloat(prefix + std::string(setting.key), defaults.*setting.field,
                                    setting.min, setting.max, std::string(setting.description)));
    }
    console.add(CVar::makeString(prefix + "weapon", defaults.weapon,
                                 "the game's weapon it shoots; empty has no ranged attack"));
}

EnemySettings enemyFromCVars(const Console& console, std::string_view enemy,
                             const MovementSettings& movement,
                             std::span<const WeaponSettings> weapons) {
    const std::string prefix = enemyCVarPrefix(enemy);
    EnemySettings settings;
    settings.name = std::string(enemy);
    for (const EnemyInt& setting : kEnemyInts) {
        settings.*setting.field = findCVar(console, prefix + std::string(setting.key)).asInt();
    }
    for (const EnemyFloat& setting : kEnemyFloats) {
        settings.*setting.field = findCVar(console, prefix + std::string(setting.key)).asFloat();
    }
    settings.weapon = findCVar(console, prefix + "weapon").text();
    for (std::uint32_t i = 0; i < weapons.size(); ++i) {
        if (!settings.weapon.empty() && weapons[i].name == settings.weapon) {
            settings.weaponIndex = i;
        }
    }
    settings.movement = enemyMovement(settings, movement);
    return settings;
}

void registerWeaponCVars(Console& console, std::string_view weapon) {
    const std::string prefix = weaponCVarPrefix(weapon);
    const WeaponSettings defaults;
    for (const WeaponInt& setting : kWeaponInts) {
        console.add(CVar::makeInt(prefix + std::string(setting.key), defaults.*setting.field,
                                  setting.min, setting.max, std::string(setting.description)));
    }
    for (const WeaponFloat& setting : kWeaponFloats) {
        console.add(CVar::makeFloat(prefix + std::string(setting.key), defaults.*setting.field,
                                    setting.min, setting.max, std::string(setting.description)));
    }
    console.add(CVar::makeBool(prefix + "given", defaults.given, "1 = carried from spawn"));
}

WeaponSettings weaponFromCVars(const Console& console, std::string_view weapon) {
    const std::string prefix = weaponCVarPrefix(weapon);
    WeaponSettings settings;
    settings.name = std::string(weapon);
    for (const WeaponInt& setting : kWeaponInts) {
        settings.*setting.field = findCVar(console, prefix + std::string(setting.key)).asInt();
    }
    for (const WeaponFloat& setting : kWeaponFloats) {
        settings.*setting.field = findCVar(console, prefix + std::string(setting.key)).asFloat();
    }
    settings.given = findCVar(console, prefix + "given").asBool();
    return settings;
}

} // namespace

std::string weaponCVarPrefix(std::string_view weapon) {
    return std::string("weapon.").append(weapon).append(".");
}

std::string enemyCVarPrefix(std::string_view enemy) {
    return std::string("enemy.").append(enemy).append(".");
}

MovementSettings enemyMovement(const EnemySettings& enemy, const MovementSettings& movement) {
    MovementSettings result = movement;
    result.halfExtents = {enemy.width * 0.5f, enemy.width * 0.5f, enemy.height * 0.5f};
    result.eyeHeight = std::min(enemy.eyeHeight, enemy.height);
    result.maxGroundSpeed = enemy.speed;
    result.groundAcceleration = enemy.acceleration;
    result.autoJump = false;
    return result;
}

std::vector<GameConfigFile> gameConfigFiles(std::span<const std::string> weapons,
                                            std::span<const std::string> enemies) {
    std::vector<GameConfigFile> files{
        {"config/movement.cfg", std::string(kMovementCVarPrefix)},
        {"config/rules.cfg", std::string(kRulesCVarPrefix)},
    };
    for (const std::string& weapon : usable(weapons)) {
        files.push_back({"weapons/" + weapon + ".cfg", weaponCVarPrefix(weapon)});
    }
    for (const std::string& enemy : usableEnemies(enemies)) {
        files.push_back({"enemies/" + enemy + ".cfg", enemyCVarPrefix(enemy)});
    }
    return files;
}

void registerGameCVars(Console& console, std::span<const std::string> weapons,
                       std::span<const std::string> enemies) {
    registerMovementCVars(console);
    const RulesSettings defaults;
    console.add(CVar::makeInt(cvarName("start_health"), defaults.startHealth, 1, kMaxHealthSetting,
                              "health a player spawns with"));
    console.add(CVar::makeInt(cvarName("start_armour"), defaults.startArmour, 0, kMaxHealthSetting,
                              "armour a player spawns with"));
    console.add(CVar::makeFloat(cvarName("armour_share"), defaults.armourShare, 0.0f, 1.0f,
                                "share of each hit's damage armour takes while it lasts"));
    console.add(CVar::makeFloat(cvarName("respawn_delay"), defaults.respawnDelay, 0.0f, 60.0f,
                                "seconds after dying before fire respawns"));
    console.add(CVar::makeInt(cvarName("max_health"), defaults.maxHealth, 1, kMaxHealthSetting,
                              "most health pickups give, unless one says otherwise"));
    console.add(CVar::makeInt(cvarName("max_armour"), defaults.maxArmour, 0, kMaxHealthSetting,
                              "most armour pickups give, unless one says otherwise"));
    console.add(CVar::makeFloat(cvarName("overcharge_decay"), defaults.overchargeDecay, 0.0f,
                                100.0f, "health and armour lost a second while above the maximum"));
    for (const std::string& weapon : usable(weapons)) {
        registerWeaponCVars(console, weapon);
    }
    for (const std::string& enemy : usableEnemies(enemies)) {
        registerEnemyCVars(console, enemy);
    }
}

GameSettings gameSettingsFromCVars(const Console& console, std::span<const std::string> weapons,
                                   std::span<const std::string> enemies) {
    GameSettings settings;
    settings.movement = movementSettingsFromCVars(console);
    settings.rules.startHealth = findCVar(console, cvarName("start_health")).asInt();
    settings.rules.startArmour = findCVar(console, cvarName("start_armour")).asInt();
    settings.rules.armourShare = findCVar(console, cvarName("armour_share")).asFloat();
    settings.rules.respawnDelay = findCVar(console, cvarName("respawn_delay")).asFloat();
    settings.rules.maxHealth = findCVar(console, cvarName("max_health")).asInt();
    settings.rules.maxArmour = findCVar(console, cvarName("max_armour")).asInt();
    settings.rules.overchargeDecay = findCVar(console, cvarName("overcharge_decay")).asFloat();
    for (const std::string& weapon : usable(weapons)) {
        settings.weapons.push_back(weaponFromCVars(console, weapon));
    }
    for (const std::string& enemy : usableEnemies(enemies)) {
        settings.enemies.push_back(
            enemyFromCVars(console, enemy, settings.movement, settings.weapons));
    }
    return settings;
}

} // namespace ct
