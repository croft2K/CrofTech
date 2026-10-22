#pragma once

#include "croftech/core/Math.h"
#include "croftech/core/Schema.h"
#include "croftech/sim/EntityId.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ct {

// What an enemy is doing. Each tick its AI (runEnemies, sim/Enemies.h) reads the state, looks
// around, and turns it into an input command that moves it with the player movement code.
enum class EnemyState : std::uint32_t {
    Idle,    // where it was put, watching its view cone
    Alerted, // has noticed a player, and turns to them until its reaction time is up
    Chase,   // following its path to the target
    Attack,  // a swing on its way, which lands at attackTick
    Lost,    // the target went out of sight: going to where it was last seen
};
inline constexpr std::uint32_t kEnemyStateCount = 5;

enum EnemyFlags : std::uint32_t {
    kEnemyAlertFired = 1u << 0, // on_alert has been sent
    kEnemyDeathFired = 1u << 1, // on_death has been sent
    kEnemyHasPath = 1u << 2,    // waypoint is the next corner of a path
    kEnemyRideStart = 1u << 3,  // the waypoint is where the ride of a lift starts
};

// An enemy's mind: its own body is the Transform, PlayerMotion and Health it also has. Enemies
// placed in the level also have a LevelEntityRef, for the wiring.
struct Enemy {
    std::uint32_t type = 0; // GameSettings::enemies index
    std::uint32_t state = 0;
    std::uint32_t stateTick = 0; // when it went into the state
    std::uint32_t flags = 0;
    EntityId target;
    Vec3 lastSeen;                   // the target's feet when it was last seen
    std::uint32_t seenTick = 0;      // when that was
    std::uint32_t hurtSeen = 0;      // the Health::hurtTick it last reacted to
    Vec3 waypoint;                   // the next corner of its path
    std::uint32_t waypointArea = 0;  // the NavArea from the waypoint on
    std::uint32_t waypointMover = 0; // the door or lift from the waypoint on, as a level entity
    std::uint32_t pathTick = 0;      // when it next finds its path again
    std::uint32_t attackTick = 0;    // while attacking, when the swing lands
    std::uint32_t readyTick = 0;     // when it may start the next swing
    std::uint32_t random = 0;        // counter for core/Random.h
};

template <>
struct Schema<Enemy> {
    static constexpr std::array<std::string_view, 4> kFlagNames{"alert_fired", "death_fired",
                                                                "has_path", "ride_start"};
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Enemy, type).withDescription("Which of the game's enemies it is"),
        CT_FIELD(Enemy, state)
            .withRange(0, kEnemyStateCount - 1)
            .withDescription("0 idle, 1 alerted, 2 chase, 3 attack, 4 lost"),
        CT_FIELD(Enemy, stateTick).withDescription("Tick it went into the state"),
        CT_FIELD(Enemy, flags).withFlagNames(kFlagNames),
        CT_FIELD(Enemy, target).withDescription("The player it is after"),
        CT_FIELD(Enemy, lastSeen)
            .withHint(FieldHint::Distance)
            .withDescription("The target's feet when last seen"),
        CT_FIELD(Enemy, seenTick).withDescription("Tick it last saw the target"),
        CT_FIELD(Enemy, hurtSeen).withDescription("The hurt tick it last reacted to"),
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
        CT_FIELD(Enemy, readyTick).withDescription("Tick it may start its next swing"),
        CT_FIELD(Enemy, random).withDescription("Random number counter"),
    };
    static constexpr TypeSchema value{"enemy", sizeof(Enemy), kFields};
};
static_assert(describesEveryByte(Schema<Enemy>::value));

inline EnemyState enemyState(const Enemy& enemy) {
    return enemy.state < kEnemyStateCount ? static_cast<EnemyState>(enemy.state) : EnemyState::Idle;
}

} // namespace ct
