#pragma once

#include "croftech/core/Math.h"
#include "croftech/sim/GameLevel.h"
#include "croftech/sim/PlayerMovement.h"
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
};

struct GameEvent {
    GameEventKind kind = GameEventKind::Footstep;
    EntityId entity;
    Vec3 position;
    float strength = 0.0f;
    std::uint32_t levelEntity = 0; // movers and checkpoints
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
    // events for the jump back in time.
    void update(const State* state, std::uint32_t timeline, const GameLevel& level,
                const MovementSettings& movement, std::vector<GameEvent>& out);
    // Forgets the last state, as when another level starts.
    void reset() { m_known = false; }

private:
    void compare(const State& state, const GameLevel& level, const MovementSettings& movement,
                 std::vector<GameEvent>& out);

    std::unique_ptr<State> m_last; // the state the events so far were worked out to
    bool m_known = false;
    std::uint32_t m_timeline = 0;
    std::array<float, kMaxEntities> m_walked{}; // metres since each player's last footstep
};

// Where a mover's sound comes from: the middle of its brushes, wherever they have moved to.
Vec3 moverCentre(const GameLevel& level, std::uint32_t levelEntity, const World& world);

} // namespace ct
