#pragma once

#include "croftech/core/Math.h"
#include "croftech/sim/GameLevel.h"
#include "croftech/sim/GameSettings.h"
#include "croftech/sim/World.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace ct {

// Things that happened in the game, for sounds and effects. The server never sends events: a
// client works them out from what changed between the states it has been sent, so nothing extra
// crosses the network and a lost snapshot loses nothing that changed state (a jump, a hit, a
// door setting off). Presentation only; never rewound.
enum class GameEventKind : std::uint8_t {
    Footstep,
    Jump,
    Land, // strength: speed of the fall, m/s
    Hurt, // strength: health lost
    Death,
    Respawn,
    MoverStart,
    MoverStop,
    CheckpointReached,
    Explosion,     // a rocket or grenade going off; entity is the projectile
    Fire,          // a shot, from the eye; weapon says which
    Impact,        // where a hitscan pellet stopped; strength 1 if it hit someone, else 0
    WeaponSwitch,  // weapon says which is now in hand
    Pickup,        // taken; entity is who took it, levelEntity the pickup
    PickupRespawn, // back again; levelEntity is the pickup
    EnemyAlert,    // an enemy noticed a player, or found the one it was searching for; at its head
    EnemySwing,    // an enemy started a swing
    EnemySpawn,    // a spawner sent an enemy; at its feet
};

struct GameEvent {
    GameEventKind kind = GameEventKind::Footstep;
    EntityId entity; // who or what it happened to; for shots, the shooter
    Vec3 position;
    float strength = 0.0f;
    std::uint32_t levelEntity = 0; // movers, checkpoints and pickups
    std::uint32_t weapon = 0;      // shots and switches: GameSettings::weapons index
    Vec3 origin;                   // impacts: where the pellet came from
    // When entity is an enemy, its kind (GameSettings::enemies index), so it can sound like one.
    std::uint32_t enemyType = kNoEnemyType;
};

class GameEvents {
public:
    // A footstep every this many metres walked or run on the ground.
    static constexpr float kStride = 2.0f;
    // Landings slower than this make no sound.
    static constexpr float kMinLandingSpeed = 4.0f;

    GameEvents();
    ~GameEvents();

    // Once a frame with the client's newest state (null before the first): appends what happened
    // since the last state it was given. A new timeline (a loaded save) starts afresh, with no
    // events for the jump back in time. Where hitscan pellets went is worked out again from the
    // shooter's random counter, as the server did, but from where the shooter is in this state,
    // which may be a tick or two after the shot.
    void update(const State* state, std::uint32_t timeline, const GameLevel& level,
                const GameSettings& settings, std::vector<GameEvent>& out);
    // Forgets the last state, as when another level starts.
    void reset() { m_known = false; }

private:
    void compare(const State& state, const GameLevel& level, const GameSettings& settings,
                 std::vector<GameEvent>& out);
    void compareWeapons(const State& state, const GameLevel& level, const GameSettings& settings,
                        std::vector<GameEvent>& out) const;

    std::unique_ptr<State> m_last; // the state the events so far were worked out to
    bool m_known = false;
    std::uint32_t m_timeline = 0;
    std::array<float, kMaxEntities> m_walked{}; // metres since each player's last footstep
};

// Where a mover's sound comes from: the middle of its brushes, wherever they have moved to.
Vec3 moverCentre(const GameLevel& level, std::uint32_t levelEntity, const World& world);

} // namespace ct
