#pragma once

#include "croftech/core/Console.h"
#include "croftech/script/ScriptProgram.h"
#include "croftech/sim/BehaviourTree.h"
#include "croftech/sim/PlayerMovement.h"
#include "croftech/sim/UserCmd.h"
#include "croftech/sim/Weapons.h"

#include <cstdint>
#include <optional>
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

    // For the client and the editor; the simulation ignores them.
    std::string description; // a line for level designers
    // It fires with sounds/<set>_fire.wav; empty is its own name. A weapon made from another
    // keeps the other's sound this way.
    std::string sounds;

    // The name its sound files start with.
    std::string_view soundSet() const { return sounds.empty() ? name : sounds; }
};

// Kinds of enemy a game can have.
inline constexpr std::uint32_t kMaxEnemyTypes = 64;
inline constexpr std::uint32_t kNoBehaviour = 0xFFFFFFFFu;

// What an enemy plays by: the numbers of its type (EnemySettings), with any that the one enemy
// has of its own (EnemyOverrides, sim/Enemy.h) in their place. Plain data, so working one out
// each tick allocates nothing.
struct EnemyTuning {
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
    float hearingRange = 30.0f;   // m within which it hears a player's shots
    float footstepRange = 6.0f;   // m within which it hears a player running
    std::int32_t meleeDamage = 0; // each swing that lands; 0 has no melee attack
    float meleeRange = 0.6f;      // m from its box to the target's that a swing reaches
    float meleeWindup = 0.3f;     // seconds from starting a swing to the hit
    float meleeInterval = 1.0f;   // seconds between the starts of swings
    float meleeKnockback = 3.0f;  // m/s a swing that lands adds to the target, away from it
    // A ranged attack with one of the game's weapons, which never runs out: it shoots in bursts,
    // with its aim off by up to aimError degrees, at targets it can see within attackRange.
    std::uint32_t weaponIndex = 0xFFFFFFFFu; // in GameSettings::weapons; none past the end
    float attackRange = 25.0f;               // m
    float keepDistance = 8.0f;               // m it stops closing in at while it can shoot
    float aimError = 3.0f;                   // degrees
    std::int32_t burst = 3;                  // shots in a row, each after the weapon's interval
    float burstPause = 1.0f;                 // seconds between bursts
    float strafeTime = 0.0f; // seconds it sidesteps one way while shooting; 0 stands still
    float walkSpeed = 2.5f;  // m/s on patrol and going back to its post
    // After losing its target it goes to where it last knew it to be, then searches about there
    // (within searchRadius) for searchTime before going back.
    float searchTime = 8.0f;   // seconds; 0 goes straight back
    float searchRadius = 8.0f; // m
    // Each time it is hurt or ends a burst, the chance it runs to a spot within coverRange that
    // the target can't see, where it stays for coverTime.
    float coverChance = 0.0f;
    float coverTime = 2.0f;   // seconds
    float coverRange = 8.0f;  // m
    float corpseTime = 10.0f; // seconds its body stays after death; -1 stays for good
    // The game's movement with the size, eye height, speed and acceleration above.
    MovementSettings movement;
};

// One kind of enemy. Each is a file, enemies/<name>.cfg, whose lines are the variables
// "enemy.<name>.<key>"; a game lists its enemies in its game.cfg. Enemies move with the player
// movement code, using the game's movement settings with their own size and speed.
struct EnemySettings : EnemyTuning {
    std::string name; // its file's name, as game.cfg lists it and levels name it
    // How it looks and sounds, for the client and the editor; the simulation ignores them.
    std::string description; // a line for level designers
    // Its sounds are sounds/<set>_alert.wav and the like; empty is its own name. A type made from
    // another keeps the other's sounds this way.
    std::string sounds;
    // The colour of its box, red, green and blue from 0 to 1 as a colour picker shows them (sRGB);
    // nullopt picks one by its place in the game's list.
    std::optional<Vec3> colour;
    std::string weapon; // the name of its weapon (weaponIndex); empty has no ranged attack
    // The tree it decides by, behaviours/<name>.behaviour; empty is the standard one. Found by
    // loadBehaviourTrees, which sets its place in GameSettings::behaviours (kNoBehaviour for the
    // standard tree).
    std::string behaviour;
    std::uint32_t behaviourIndex = kNoBehaviour;

    // The name its sound files start with.
    std::string_view soundSet() const { return sounds.empty() ? name : sounds; }
};

// A setting of an enemy type that one enemy may have a value of its own for: a whole number, a
// number, or (with neither field) its weapon. An enemy or spawner in a level sets it as a
// property with the same key as the type's file.
struct EnemySetting {
    std::string_view key;
    std::int32_t EnemyTuning::* wholeField = nullptr;
    float EnemyTuning::* numberField = nullptr;
    float minimum = 0.0f;
    float maximum = 0.0f;
    std::string_view description;
};

// Every one, in an order that only grows at the end: EnemyOverrides holds places in it.
std::span<const EnemySetting> enemySettings();
// Its place in enemySettings(), or nullopt.
std::optional<std::uint32_t> findEnemySetting(std::string_view key);
// The setting's value as a float (the weapon as its index), and setting it; a whole number is
// rounded and every value is clamped to the setting's range. Setting the weapon to an index past
// the game's weapons takes the ranged attack away.
float enemySettingValue(const EnemyTuning& tuning, std::uint32_t setting);
void setEnemySetting(EnemyTuning& tuning, std::uint32_t setting, float value);

// Everything a game tunes that the simulation reads. It may only change between ticks.
struct GameSettings {
    MovementSettings movement;
    RulesSettings rules;
    std::vector<WeaponSettings> weapons;   // at most kMaxWeapons, in number-key order
    std::vector<EnemySettings> enemies;    // at most kMaxEnemyTypes
    std::vector<BehaviourTree> behaviours; // the trees enemy types name (loadBehaviourTrees)
    // The scripts those trees' script nodes name, sorted by name (loadBehaviourScripts).
    std::vector<ScriptProgram> behaviourScripts;
};

inline constexpr std::string_view kRulesCVarPrefix = "rules.";

// "weapon.<name>.": the variables a weapon's file sets.
std::string weaponCVarPrefix(std::string_view weapon);
// "enemy.<name>.": the variables an enemy's file sets.
std::string enemyCVarPrefix(std::string_view enemy);
// The game's movement with the enemy's size, eye height, speed and acceleration
// (EnemySettings::movement); for enemies set up in code, such as in tests.
MovementSettings enemyMovement(const EnemyTuning& enemy, const MovementSettings& movement);

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
