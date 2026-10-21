#pragma once

#include "croftech/level/Level.h"
#include "croftech/sim/CollisionWorld.h"
#include "croftech/sim/World.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// The logic classes the simulation runs (level/EntityClasses.h describes them for editors).
enum class LogicClass : std::uint8_t { None, Trigger, Relay, Counter, Timer, Mover };

// Every event and input of those classes, so the wiring is resolved from names once, when the
// level is built, rather than compared as text each tick.
enum class LogicEvent : std::uint8_t {
    OnEnter,
    OnExit,
    OnTrigger,
    OnReached,
    OnTimer,
    OnLeaveStart,
    OnReachedEnd,
    OnLeaveEnd,
    OnReachedStart,
};

enum class LogicInput : std::uint8_t {
    Enable,
    Disable,
    Toggle,
    Trigger,
    Add,
    Reset,
    Start,
    Stop,
    MoveToEnd,
    MoveToStart,
    Resume,
};

std::string_view logicEventName(LogicEvent event);
std::string_view logicInputName(LogicInput input);

// Logic entities a level may have; the rest of the entity table is left for players and the
// inputs in flight between entities.
inline constexpr std::uint32_t kMaxLevelLogicEntities = kMaxEntities / 2;
// Inputs delivered in one tick at most, which stops wiring that loops with no delay from hanging
// the server.
inline constexpr std::uint32_t kMaxInputDeliveriesPerTick = 4096;

// Everything about a level that play doesn't change, built once when it loads: collision for the
// world and for each brush entity, the settings of each logic entity with its outputs resolved to
// the entities and inputs they reach, the spawn points, and the state play starts from. It is
// shared by every tick and never part of the state.
class GameLevel {
public:
    struct Target {
        std::uint32_t entity = 0; // level entity index
        LogicInput input = LogicInput::Trigger;
    };

    struct Output {
        LogicEvent event = LogicEvent::OnTrigger;
        std::uint32_t delayTicks = 0;
        std::span<const Target> targets;
    };

    struct Entity {
        LogicClass logicClass = LogicClass::None;
        EntityId id; // in the starting state; invalid if the entity isn't simulated
        std::span<const Output> outputs;
        const CollisionWorld* model = nullptr; // its brushes: triggers

        // Settings, read from the level through EntityClasses with every value clamped to its
        // range, so play never sees one the editor wouldn't allow.
        bool once = false;               // trigger
        bool startEnabled = true;        // trigger, relay
        std::uint32_t count = 1;         // counter
        std::uint32_t intervalTicks = 1; // timer
        bool repeat = true;              // timer
        bool startOn = false;            // timer
    };

    explicit GameLevel(const Level& level);
    GameLevel(const GameLevel&) = delete;
    GameLevel& operator=(const GameLevel&) = delete;

    // World geometry: world brushes and those of entities that don't move or vanish in play.
    const CollisionWorld& collision() const { return m_collision; }
    std::span<const SpawnPoint> spawns() const { return m_spawns; }
    // Null if index isn't an entity of the level.
    const Entity* entity(std::uint32_t index) const {
        return index < m_entities.size() ? &m_entities[index] : nullptr;
    }
    std::size_t entityCount() const { return m_entities.size(); }
    // Before any player joins: the logic entities with their starting values.
    const State& startState() const { return *m_start; }
    // What was wrong with the level's logic (unknown inputs, targets that don't exist, values out
    // of range). Each was skipped or clamped; the level still plays.
    std::span<const std::string> warnings() const { return m_warnings; }

private:
    void readEntity(const Level& level, std::uint32_t index);
    void resolveOutputs(const Level& level);
    void spawnLogic();

    CollisionWorld m_collision;
    std::vector<SpawnPoint> m_spawns;
    std::vector<Entity> m_entities;
    std::vector<std::unique_ptr<CollisionWorld>> m_models;
    std::vector<Output> m_outputs;
    std::vector<Target> m_targets;
    std::unique_ptr<State> m_start;
    std::vector<std::string> m_warnings;
};

} // namespace ct
