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

std::string cvarName(std::string_view key) { return std::string(kRulesCVarPrefix).append(key); }

const CVar& findCVar(const Console& console, const std::string& name) {
    const CVar* cvar = console.find(name);
    CT_ASSERT(cvar != nullptr);
    return *cvar;
}

std::span<const std::string> usable(std::span<const std::string> weapons) {
    return weapons.first(std::min<std::size_t>(weapons.size(), kMaxWeapons));
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

std::vector<GameConfigFile> gameConfigFiles(std::span<const std::string> weapons) {
    std::vector<GameConfigFile> files{
        {"config/movement.cfg", std::string(kMovementCVarPrefix)},
        {"config/rules.cfg", std::string(kRulesCVarPrefix)},
    };
    for (const std::string& weapon : usable(weapons)) {
        files.push_back({"weapons/" + weapon + ".cfg", weaponCVarPrefix(weapon)});
    }
    return files;
}

void registerGameCVars(Console& console, std::span<const std::string> weapons) {
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
    for (const std::string& weapon : usable(weapons)) {
        registerWeaponCVars(console, weapon);
    }
}

GameSettings gameSettingsFromCVars(const Console& console, std::span<const std::string> weapons) {
    GameSettings settings;
    settings.movement = movementSettingsFromCVars(console);
    settings.rules.startHealth = findCVar(console, cvarName("start_health")).asInt();
    settings.rules.startArmour = findCVar(console, cvarName("start_armour")).asInt();
    settings.rules.armourShare = findCVar(console, cvarName("armour_share")).asFloat();
    settings.rules.respawnDelay = findCVar(console, cvarName("respawn_delay")).asFloat();
    for (const std::string& weapon : usable(weapons)) {
        settings.weapons.push_back(weaponFromCVars(console, weapon));
    }
    return settings;
}

} // namespace ct
