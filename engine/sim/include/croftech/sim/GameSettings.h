#pragma once

#include "croftech/core/Console.h"
#include "croftech/sim/PlayerMovement.h"
#include "croftech/sim/UserCmd.h"
#include "croftech/sim/Weapons.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// The rules of play a game tunes: health, armour and respawning. Like movement, each is a console
// variable, "rules.<key>", set by a line of content/config/rules.cfg.
struct RulesSettings {
    std::int32_t startHealth = 100;
    std::int32_t startArmour = 0;
    float armourShare = 0.66f; // of each hit's damage that armour takes while it lasts
    float respawnDelay = 1.0f; // seconds after dying before fire respawns
    // Most that pickups top health and armour up to, unless one says otherwise (mega health).
    std::int32_t maxHealth = 100;
    std::int32_t maxArmour = 100;
    float overchargeDecay = 1.0f; // health and armour lost a second while above the maximum
};

// One weapon. Each is a file, weapons/<name>.cfg, whose lines are the variables
// "weapon.<name>.<key>"; a game lists its weapons, in number-key order, in its game.cfg. With no
// projectile speed its pellets are traced at once (hitscan); otherwise each is a projectile that
// explodes, on hitting something or when its fuse runs out, hurting what is near.
struct WeaponSettings {
    std::string name;             // its file's name, as game.cfg lists it and pickups name it
    std::int32_t damage = 10;     // each pellet or projectile that hits
    std::int32_t pellets = 1;     // traces per shot
    float spread = 0.0f;          // degrees a pellet may stray from the aim
    float range = 200.0f;         // m
    float fireInterval = 0.5f;    // seconds between shots while fire is held
    float raiseTime = 0.25f;      // seconds after switching to it before it fires
    float knockback = 0.0f;       // m/s each pellet that hits adds to the target, along the shot
    std::int32_t ammoPerShot = 1; // 0 never runs out
    std::int32_t startAmmo = 0;   // carried at spawn
    std::int32_t maxAmmo = 100;
    std::int32_t pickupAmmo = 10; // in an ammo or weapon pickup that doesn't say
    bool given = false;           // carried from spawn

    float projectileSpeed = 0.0f;   // m/s; 0 for hitscan
    float projectileGravity = 0.0f; // share of movement gravity that pulls it down
    float bounce = 0.0f;            // speed kept bouncing off the world; 0 explodes on it
    float fuse = 10.0f;             // seconds before it explodes by itself
    std::int32_t splashDamage = 0;  // at the centre of the explosion, less with distance
    float splashRadius = 0.0f;      // m
    float splashKnockback = 0.0f;   // m/s at the centre, away from it
    float selfDamage = 0.5f;        // share of its splash damage the shooter takes
};

// Kinds of enemy a game can have.
inline constexpr std::uint32_t kMaxEnemyTypes = 64;

// One kind of enemy. Each is a file, enemies/<name>.cfg, whose lines are the variables
// "enemy.<name>.<key>"; a game lists its enemies in its game.cfg. Enemies move with the player
// movement code, using the game's movement settings with their own size and speed.
struct EnemySettings {
    std::string name; // its file's name, as game.cfg lists it and levels name it
    std::int32_t health = 100;
    float width = 0.8f;           // of its box, m
    float height = 1.8f;          // m
    float eyeHeight = 1.6f;       // above its feet, where it sees from
    float speed = 6.0f;           // m/s running
    float acceleration = 10.0f;   // as movement's ground_acceleration
    float turnSpeed = 540.0f;     // degrees a second
    float sightRange = 40.0f;     // m
    float sightAngle = 140.0f;    // degrees across its view, until it is alerted
    float reactionTime = 0.3f;    // seconds from noticing a player to going after them
    std::int32_t meleeDamage = 0; // each swing that lands; 0 has no melee attack
    float meleeRange = 0.6f;      // m from its box to the target's that a swing reaches
    float meleeWindup = 0.3f;     // seconds from starting a swing to the hit
    float meleeInterval = 1.0f;   // seconds between the starts of swings
    float meleeKnockback = 3.0f;  // m/s a swing that lands adds to the target, away from it
    float corpseTime = 10.0f;     // seconds its body stays after death; -1 stays for good
    // The game's movement with the size, eye height, speed and acceleration above.
    MovementSettings movement;
};

// Everything a game tunes that the simulation reads. It may only change between ticks.
struct GameSettings {
    MovementSettings movement;
    RulesSettings rules;
    std::vector<WeaponSettings> weapons; // at most kMaxWeapons, in number-key order
    std::vector<EnemySettings> enemies;  // at most kMaxEnemyTypes
};

inline constexpr std::string_view kRulesCVarPrefix = "rules.";

// "weapon.<name>.": the variables a weapon's file sets.
std::string weaponCVarPrefix(std::string_view weapon);
// "enemy.<name>.": the variables an enemy's file sets.
std::string enemyCVarPrefix(std::string_view enemy);
// The game's movement with the enemy's size, eye height, speed and acceleration
// (EnemySettings::movement); for enemies set up in code, such as in tests.
MovementSettings enemyMovement(const EnemySettings& enemy, const MovementSettings& movement);

// A file of game settings, relative to the content folders, and the variables it sets.
struct GameConfigFile {
    std::string path;
    std::string prefix;
};

// The files a game's settings come from: movement, rules, and each weapon's and enemy's file.
// Weapons past kMaxWeapons and enemies past kMaxEnemyTypes are left out here and below.
std::vector<GameConfigFile> gameConfigFiles(std::span<const std::string> weapons,
                                            std::span<const std::string> enemies = {});

// Adds the movement, rules, weapon and enemy variables with the built-in defaults.
void registerGameCVars(Console& console, std::span<const std::string> weapons = {},
                       std::span<const std::string> enemies = {});

// Weapons and enemies as for registerGameCVars.
GameSettings gameSettingsFromCVars(const Console& console,
                                   std::span<const std::string> weapons = {},
                                   std::span<const std::string> enemies = {});

} // namespace ct
