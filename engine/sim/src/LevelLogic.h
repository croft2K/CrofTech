#pragma once

#include "croftech/sim/GameLevel.h"
#include "croftech/sim/GameSettings.h"
#include "croftech/sim/LogicComponents.h"
#include "croftech/sim/PlayerMovement.h"
#include "croftech/sim/World.h"

#include <array>
#include <cstdint>

namespace ct {

// Runs the level's logic entities for one tick of a state: delivers inputs, fires events and
// passes them along the wiring. Made on the stack for each tick, so it holds no state of its own
// between ticks.
//
// Outputs without a delay are delivered within the tick, in the order they were sent; each phase
// below delivers everything its events set off before returning. Delayed outputs become
// PendingInput entities. A tick delivers at most kMaxInputDeliveriesPerTick inputs.
class LevelLogic {
public:
    LevelLogic(const GameLevel& level, State& state) : m_level(level), m_state(state) {}

    // Inputs whose delay ends this tick, oldest first.
    void deliverPendingInputs();
    void runTimers();
    // Movers set off, travel and arrive, pushing players out of the way and carrying the ones
    // standing on them. Before players move, so they move against where movers now are.
    void runMovers(const MovementSettings& movement);
    // Players walking into doors and standing on lifts, after they have moved.
    void activateMovers(const MovementSettings& movement);
    // Players going into and out of triggers and reaching checkpoints, after they have moved.
    void updateTriggers(const MovementSettings& movement);
    // Hurt volumes hurt what is inside them, each thing at most once per interval.
    void runHurtVolumes(const GameSettings& settings);
    // Taken pickups come back when their time is up, and living players take the ones they touch
    // and need, in input-slot order.
    void runPickups(const GameSettings& settings);

private:
    struct Queued {
        std::uint32_t target = 0; // level entity index
        LogicInput input = LogicInput::Trigger;
        EntityId activator;
    };

    // The level entity behind a simulated entity, if its LevelEntityRef names one of that class.
    const GameLevel::Entity* levelEntityOf(EntityId id, LogicClass logicClass,
                                           std::uint32_t& index) const;
    void fire(std::uint32_t source, LogicEvent event, EntityId activator);
    void send(std::uint32_t source, std::uint32_t output, EntityId activator);
    void queue(const GameLevel::Output& output, EntityId activator);
    void deliverQueued();
    void apply(const Queued& input);
    // What the pickup gives, if the player needs it; whether it was taken.
    bool give(const GameLevel::Entity& pickup, const GameSettings& settings, EntityId player);

    // Movers (Movers.cpp).
    void applyMoverInput(std::uint32_t index, const GameLevel::Entity& entity, Mover& mover,
                         LogicInput input, EntityId activator);
    void setOff(std::uint32_t index, Mover& mover, bool toEnd, EntityId activator);
    void arrive(std::uint32_t index, const GameLevel::Entity& entity, Mover& mover, bool atEnd);
    // Waiting at the end starts again, as when a player keeps a door open.
    void extendWait(const GameLevel::Entity& entity, Mover& mover) const;
    std::uint32_t departureTick(const GameLevel::Entity& entity, bool atEnd) const;
    bool moveMover(EntityId id, const GameLevel::Entity& entity, Mover& mover, float distance,
                   const MovementSettings& movement);

    const GameLevel& m_level;
    State& m_state;
    std::array<Queued, 1024> m_queue{}; // a ring
    std::uint32_t m_queueHead = 0;
    std::uint32_t m_queueCount = 0;
    std::uint32_t m_delivered = 0;
    std::uint32_t m_sentOrder = 0;
};

} // namespace ct
