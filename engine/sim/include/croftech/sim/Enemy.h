#pragma once

#include "croftech/core/Math.h"
#include "croftech/core/Schema.h"
#include "croftech/level/Level.h"
#include "croftech/sim/BehaviourTree.h"
#include "croftech/sim/EntityId.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ct {

// What an enemy is doing. Each tick its behaviour tree (sim/BehaviourTree.h) decides, its actions
// carrying on from what the state says, and runEnemies turns that into an input command that
// moves it with the player movement code.
enum class EnemyState : std::uint32_t {
    Idle,    // standing at its post or a patrol point, watching its view cone
    Alerted, // has noticed a player, and turns to them until its reaction time is up
    Chase,   // following its path to the target
    Attack,  // a swing on its way, which lands at attackTick
    Lost,    // the target went out of sight: going to where it was last seen
    Search,  // looking about where it lost the target, from spot to spot
    Cover,   // running to a spot the target can't see, and staying there a while
    Patrol,  // walking to the next point of its patrol, or back to its post
};
inline constexpr std::uint32_t kEnemyStateCount = 8;

enum EnemyFlags : std::uint32_t {
    kEnemyAlertFired = 1u << 0, // on_alert has been sent
    kEnemyDeathFired = 1u << 1, // on_death has been sent
    kEnemyHasPath = 1u << 2,    // waypoint is the next corner of a path
    kEnemyRideStart = 1u << 3,  // the waypoint is where the ride of a lift starts
    kEnemyAmbush = 1u << 4,     // deaf: wakes only on seeing a player or being hurt
    kEnemyStrafeLeft = 1u << 5, // sidestepping to its left while it shoots
    kEnemyAtGoal = 1u << 6,     // standing at its goal, or as near as the mesh lets it get
    kEnemyGaveUp = 1u << 7,     // lost its target and stopped chasing: it searches next
    kEnemyBurstEnded = 1u << 8, // ended a burst last tick, so may run for cover
};

// An enemy's mind: its own body is the Transform, PlayerMotion and Health it also has. Enemies
// placed in the level also have a LevelEntityRef, for the wiring; those a spawner sent name it.
struct Enemy {
    std::uint32_t type = 0; // GameSettings::enemies index
    std::uint32_t state = 0;
    std::uint32_t stateTick = 0; // when it went into the state
    std::uint32_t flags = 0;
    EntityId target;
    Vec3 lastSeen;              // the middle of the target when it was last seen, or last known of
    std::uint32_t seenTick = 0; // when it last saw the target
    std::uint32_t hurtSeen = 0; // the Health::hurtTick it last reacted to
    // Where its path leads: the target's last known place, a patrol point, a spot to search or
    // hide in, or its post.
    Vec3 goal;
    Vec3 waypoint;                   // the next corner of its path
    std::uint32_t waypointArea = 0;  // the NavArea from the waypoint on
    std::uint32_t waypointMover = 0; // the door or lift from the waypoint on, as a level entity
    std::uint32_t pathTick = 0;      // when it next finds its path again
    std::uint32_t attackTick = 0;    // while attacking, when the swing lands
    std::uint32_t readyTick = 0;     // when it may start the next swing or shot
    std::uint32_t burstLeft = 0;     // shots left in the burst it is firing
    std::uint32_t strafeTick = 0;    // when it next changes the way it sidesteps
    Vec3 home;                       // its post: where its feet were put...
    std::uint32_t homeYaw = 0;       // ...and the way it faced there
    // The level entity of the patrol point it is walking to or standing at, or kNoEntity.
    std::uint32_t patrolPoint = kNoEntity;
    std::uint32_t waitTick = 0; // standing at a patrol point, search spot or cover: when it goes on
    std::uint32_t lookYaw = 0;  // standing with no target in sight, the way it turns to face
    std::uint32_t coverTick = 0; // when it may next run for cover
    // The level entity of the spawner that sent it, or kNoEntity for one placed in the level. Its
    // events go to the spawner's wiring, and it counts towards the spawner's encounter.
    std::uint32_t spawner = kNoEntity;
    std::uint32_t random = 0; // counter for core/Random.h
};

template <>
struct Schema<Enemy> {
    static constexpr std::array<std::string_view, 9> kFlagNames{
        "alert_fired", "death_fired", "has_path", "ride_start", "ambush",
        "strafe_left", "at_goal",     "gave_up",  "burst_ended"};
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Enemy, type).withDescription("Which of the game's enemies it is"),
        CT_FIELD(Enemy, state)
            .withRange(0, kEnemyStateCount - 1)
            .withDescription(
                "0 idle, 1 alerted, 2 chase, 3 attack, 4 lost, 5 search, 6 cover, 7 patrol"),
        CT_FIELD(Enemy, stateTick).withDescription("Tick it went into the state"),
        CT_FIELD(Enemy, flags).withFlagNames(kFlagNames),
        CT_FIELD(Enemy, target).withDescription("The player it is after"),
        CT_FIELD(Enemy, lastSeen)
            .withHint(FieldHint::Distance)
            .withDescription("The middle of the target when last seen or known of"),
        CT_FIELD(Enemy, seenTick).withDescription("Tick it last saw the target"),
        CT_FIELD(Enemy, hurtSeen).withDescription("The hurt tick it last reacted to"),
        CT_FIELD(Enemy, goal).withHint(FieldHint::Distance).withDescription("Where its path leads"),
        CT_FIELD(Enemy, waypoint)
            .withHint(FieldHint::Distance)
            .withDescription("Next corner of its path"),
        CT_FIELD(Enemy, waypointArea)
            .withRange(0, 3)
            .withDescription("From the waypoint on: 0 ground, 1 door, 2 lift, 3 hazard"),
        CT_FIELD(Enemy, waypointMover)
            .withDescription("The door or lift from the waypoint on (level entity)"),
        CT_FIELD(Enemy, pathTick).withDescription("Tick it finds its path again"),
        CT_FIELD(Enemy, attackTick).withDescription("While attacking, tick the swing lands"),
        CT_FIELD(Enemy, readyTick).withDescription("Tick it may start its next swing or shot"),
        CT_FIELD(Enemy, burstLeft).withDescription("Shots left in its burst"),
        CT_FIELD(Enemy, strafeTick).withDescription("Tick it changes the way it sidesteps"),
        CT_FIELD(Enemy, home).withHint(FieldHint::Distance).withDescription("Its post"),
        CT_FIELD(Enemy, homeYaw).withDescription("The way it faced at its post (angle units)"),
        CT_FIELD(Enemy, patrolPoint)
            .withDescription("Patrol point it is going to or standing at (level entity)"),
        CT_FIELD(Enemy, waitTick).withDescription("Standing still: tick it goes on"),
        CT_FIELD(Enemy, lookYaw)
            .withDescription("Standing still: the way it turns to (angle units)"),
        CT_FIELD(Enemy, coverTick).withDescription("Tick it may next run for cover"),
        CT_FIELD(Enemy, spawner)
            .withDescription("The spawner that sent it (level entity); none if placed"),
        CT_FIELD(Enemy, random).withDescription("Random number counter"),
    };
    static constexpr TypeSchema value{"enemy", sizeof(Enemy), kFields};
};
static_assert(describesEveryByte(Schema<Enemy>::value));

// An enemy's place in its behaviour tree: the nodes it reached last tick, so a node knows whether
// it carries on or starts afresh, and the values nodes that remember keep (BehaviourNode::memory).
// Every enemy has one.
struct EnemyBehaviour {
    // A bit for each node, by its index in the tree.
    std::array<std::uint32_t, kMaxBehaviourNodes / 32> reached{};
    std::array<std::uint32_t, kMaxBehaviourNodes / 32> active{}; // reached and didn't fail
    std::array<std::uint32_t, kMaxBehaviourMemory> memory{};
};

template <>
struct Schema<EnemyBehaviour> {
    static constexpr FieldInfo kFields[] = {
        CT_FIELD_ELEMENT(EnemyBehaviour, reached, 0), CT_FIELD_ELEMENT(EnemyBehaviour, reached, 1),
        CT_FIELD_ELEMENT(EnemyBehaviour, reached, 2), CT_FIELD_ELEMENT(EnemyBehaviour, reached, 3),
        CT_FIELD_ELEMENT(EnemyBehaviour, active, 0),  CT_FIELD_ELEMENT(EnemyBehaviour, active, 1),
        CT_FIELD_ELEMENT(EnemyBehaviour, active, 2),  CT_FIELD_ELEMENT(EnemyBehaviour, active, 3),
        CT_FIELD_ELEMENT(EnemyBehaviour, memory, 0),  CT_FIELD_ELEMENT(EnemyBehaviour, memory, 1),
        CT_FIELD_ELEMENT(EnemyBehaviour, memory, 2),  CT_FIELD_ELEMENT(EnemyBehaviour, memory, 3),
        CT_FIELD_ELEMENT(EnemyBehaviour, memory, 4),  CT_FIELD_ELEMENT(EnemyBehaviour, memory, 5),
        CT_FIELD_ELEMENT(EnemyBehaviour, memory, 6),  CT_FIELD_ELEMENT(EnemyBehaviour, memory, 7),
        CT_FIELD_ELEMENT(EnemyBehaviour, memory, 8),  CT_FIELD_ELEMENT(EnemyBehaviour, memory, 9),
        CT_FIELD_ELEMENT(EnemyBehaviour, memory, 10), CT_FIELD_ELEMENT(EnemyBehaviour, memory, 11),
        CT_FIELD_ELEMENT(EnemyBehaviour, memory, 12), CT_FIELD_ELEMENT(EnemyBehaviour, memory, 13),
        CT_FIELD_ELEMENT(EnemyBehaviour, memory, 14), CT_FIELD_ELEMENT(EnemyBehaviour, memory, 15),
    };
    static constexpr TypeSchema value{"enemy_behaviour", sizeof(EnemyBehaviour), kFields};
};
static_assert(kMaxBehaviourNodes == 128 && kMaxBehaviourMemory == 16,
              "one schema field per element");
static_assert(describesEveryByte(Schema<EnemyBehaviour>::value));

// Settings of its type that this one enemy has values of its own for, as the enemy or spawner in
// the level sets them: a calmer gunner here, a faster charger there. An enemy with none has no
// EnemyOverrides.
inline constexpr std::uint32_t kMaxEnemyOverrides = 16;
struct EnemyOverrides {
    std::uint32_t count = 0;
    std::array<std::uint8_t, kMaxEnemyOverrides> settings{}; // places in enemySettings()
    std::array<float, kMaxEnemyOverrides> values{};          // as setEnemySetting takes them
};

template <>
struct Schema<EnemyOverrides> {
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(EnemyOverrides, count)
            .withRange(0, kMaxEnemyOverrides)
            .withDescription("Settings of its type it has its own values for"),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 0),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 1),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 2),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 3),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 4),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 5),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 6),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 7),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 8),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 9),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 10),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 11),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 12),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 13),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 14),
        CT_FIELD_ELEMENT(EnemyOverrides, settings, 15),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 0),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 1),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 2),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 3),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 4),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 5),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 6),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 7),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 8),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 9),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 10),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 11),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 12),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 13),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 14),
        CT_FIELD_ELEMENT(EnemyOverrides, values, 15),
    };
    static constexpr TypeSchema value{"enemy_overrides", sizeof(EnemyOverrides), kFields};
};
static_assert(kMaxEnemyOverrides == 16, "one schema field per override");
static_assert(describesEveryByte(Schema<EnemyOverrides>::value));

inline EnemyState enemyState(const Enemy& enemy) {
    return enemy.state < kEnemyStateCount ? static_cast<EnemyState>(enemy.state) : EnemyState::Idle;
}

} // namespace ct
