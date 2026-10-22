#pragma once

#include "croftech/level/Level.h"
#include "croftech/nav/NavMesh.h"
#include "croftech/sim/CollisionWorld.h"
#include "croftech/sim/GameSettings.h"
#include "croftech/sim/World.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// The logic classes the simulation runs (level/EntityClasses.h describes them for editors).
enum class LogicClass : std::uint8_t {
    None,
    Trigger,
    Checkpoint,
    Hurt,
    Relay,
    Counter,
    Timer,
    Mover,
    Pickup,
    Enemy,
};

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
    OnPickup,
    OnAlert,
    OnDeath,
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
    Alert,
};

// What sets a mover going besides its inputs (the mover class's "activate").
enum class MoverActivation : std::uint8_t { Input, Touch, Stand };

// What a pickup gives (the pickup class's "item").
enum class PickupItem : std::uint8_t { Health, Armour, Ammo, Weapon };

inline constexpr std::uint32_t kNoWeapon = 0xFFFFFFFFu;
inline constexpr std::uint32_t kNoEnemyType = 0xFFFFFFFFu;

// What health and armour pickups give when their amount is 0.
inline constexpr std::int32_t kUsualPickupHealth = 25;
inline constexpr std::int32_t kUsualPickupArmour = 50;

std::string_view logicEventName(LogicEvent event);
std::string_view logicInputName(LogicInput input);
LogicClass logicClassOf(std::string_view className);

// The player's box, step and slopes, as the agent of a navigation mesh: enemies the player's size
// walk the mesh built for it.
NavAgent navAgent(const MovementSettings& movement);
// The level as a navigation mesh sees it: world geometry, hurt volumes to keep out of, and movers
// as doors, lifts or obstacles (the mover class's "nav").
NavInput navInput(const Level& level);

// Logic entities a level may have; the rest of the entity table is left for players and the
// inputs in flight between entities.
inline constexpr std::uint32_t kMaxLevelLogicEntities = kMaxEntities / 2;
// Inputs delivered in one tick at most, which stops wiring that loops with no delay from hanging
// the server.
inline constexpr std::uint32_t kMaxInputDeliveriesPerTick = 4096;
// Anything this far below the lowest point of the level has fallen out of the world.
inline constexpr float kOutOfWorldDepth = 64.0f;

// Everything about a level that play doesn't change, built once when it loads: collision for the
// world and for each brush entity, the settings of each logic entity with its outputs resolved to
// the entities and inputs they reach, the spawn points, and the state play starts from. It is
// shared by every tick and never part of the state.
//
// A start point replaces the level's spawns, for play from here in the editor. The navigation
// mesh is built the first time something asks for it.
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
        const CollisionWorld* model = nullptr; // its brushes: volumes and movers

        // Settings, read from the level through EntityClasses with every value clamped to its
        // range, so play never sees one the editor wouldn't allow.
        bool once = false;               // trigger
        bool startEnabled = true;        // trigger, checkpoint, hurt, relay
        std::uint32_t count = 1;         // counter
        std::uint32_t intervalTicks = 1; // timer; hurt, between hits
        std::int32_t damage = 0;         // hurt, each hit
        bool repeat = true;              // timer
        bool startOn = false;            // timer

        // Movers.
        Vec3 move;      // from the start to the end
        Vec3 direction; // of the move, unit length; zero if it doesn't move
        float length = 0.0f;
        float speed = 0.0f; // metres per tick
        MoverActivation activate = MoverActivation::Input;
        std::int32_t waitTicks = 0; // at the end before coming back; -1 stays there
        bool loop = false;
        bool startMoving = false;
        bool startAtEnd = false;

        // Pickups and enemies.
        Vec3 position; // the middle of the box that takes a pickup; an enemy's feet
        PickupItem item = PickupItem::Health;
        std::int32_t amount = 0;          // 0 is the usual for the item
        std::int32_t limit = 0;           // 0 is the rules' maximum
        std::uint32_t weapon = kNoWeapon; // GameSettings::weapons index
        std::int32_t respawnTicks = -1;   // -1 never comes back

        // Enemies.
        std::uint32_t enemyType = kNoEnemyType; // GameSettings::enemies index
        std::uint16_t yaw = 0;

        // Where a mover's brushes are when it has travelled the distance: exactly its move at
        // the end.
        Vec3 moverOffset(float distance) const {
            return distance >= length ? move : direction * distance;
        }
    };

    // Pickups name weapons, which are found among the game's weapons by name; the navigation mesh
    // is built for the player's size.
    explicit GameLevel(const Level& level, std::optional<SpawnPoint> start = {},
                       const GameSettings& settings = {});
    GameLevel(const GameLevel&) = delete;
    GameLevel& operator=(const GameLevel&) = delete;

    // World geometry: world brushes and those of entities that don't move or vanish in play.
    // Movers have their own models, which CollisionScene places.
    const CollisionWorld& collision() const { return m_collision; }
    std::span<const SpawnPoint> spawns() const { return m_spawns; }
    // Feet below this have fallen out of the world.
    float killHeight() const { return m_killHeight; }
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
    // The navigation mesh for agents the player's size, and for agents of any other size. Each is
    // built on the first call that asks for it, from any thread, except that the meshes for the
    // kinds of enemy placed in the level are built with the level. Their own warnings are in
    // NavMesh::warnings.
    const NavMesh& navMesh() const { return navMesh(m_navAgent); }
    const NavMesh& navMesh(const NavAgent& agent) const;

private:
    void readEntity(const Level& level, std::uint32_t index, const GameSettings& settings);
    void resolveOutputs(const Level& level);
    void spawnLogic(const GameSettings& settings);
    void findKillHeight(const Level& level);

    CollisionWorld m_collision;
    std::vector<SpawnPoint> m_spawns;
    float m_killHeight = 0.0f;
    std::vector<Entity> m_entities;
    std::vector<std::unique_ptr<CollisionWorld>> m_models;
    std::vector<Output> m_outputs;
    std::vector<Target> m_targets;
    std::unique_ptr<State> m_start;
    std::vector<std::string> m_warnings;
    NavInput m_navInput;
    NavAgent m_navAgent;
    mutable std::mutex m_navMutex;
    mutable std::vector<std::unique_ptr<NavMesh>> m_navMeshes;
};

} // namespace ct
