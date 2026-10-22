#include "croftech/sim/GameLevel.h"

#include "croftech/core/Angle.h"
#include "croftech/level/EntityClasses.h"
#include "croftech/sim/Enemies.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>

namespace ct {
namespace {

constexpr std::string_view kEventNames[] = {
    "on_enter",         "on_exit",        "on_trigger",     "on_reached",
    "on_timer",         "on_leave_start", "on_reached_end", "on_leave_end",
    "on_reached_start", "on_pickup",      "on_alert",       "on_death",
};
static_assert(std::size(kEventNames) == static_cast<std::size_t>(LogicEvent::OnDeath) + 1);

constexpr std::string_view kInputNames[] = {
    "enable", "disable", "toggle",      "trigger",       "add",    "reset",
    "start",  "stop",    "move_to_end", "move_to_start", "resume", "alert",
};
static_assert(std::size(kInputNames) == static_cast<std::size_t>(LogicInput::Alert) + 1);

template <typename Enum, std::size_t N>
std::optional<Enum> findByName(const std::string_view (&names)[N], std::string_view name) {
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == name) {
            return static_cast<Enum>(i);
        }
    }
    return std::nullopt;
}

} // namespace

LogicClass logicClassOf(std::string_view className) {
    if (className == "trigger") {
        return LogicClass::Trigger;
    }
    if (className == "checkpoint") {
        return LogicClass::Checkpoint;
    }
    if (className == "hurt") {
        return LogicClass::Hurt;
    }
    if (className == "relay") {
        return LogicClass::Relay;
    }
    if (className == "counter") {
        return LogicClass::Counter;
    }
    if (className == "timer") {
        return LogicClass::Timer;
    }
    if (className == "mover") {
        return LogicClass::Mover;
    }
    if (className == kPickupClass) {
        return LogicClass::Pickup;
    }
    if (className == kEnemyClass) {
        return LogicClass::Enemy;
    }
    return LogicClass::None;
}

namespace {

// Classes whose brushes are a model of their own rather than world geometry: triggers,
// checkpoints and hurt volumes are volumes players walk through, and movers collide where they
// have moved to.
bool hasModel(LogicClass logicClass) {
    return logicClass == LogicClass::Trigger || logicClass == LogicClass::Checkpoint ||
           logicClass == LogicClass::Hurt || logicClass == LogicClass::Mover;
}

bool isStatic(const Level& level, const Brush& brush) {
    return brush.entity >= level.entities.size() ||
           !hasModel(logicClassOf(level.entities[brush.entity].className));
}

std::vector<Brush> staticBrushes(const Level& level) {
    std::vector<Brush> brushes;
    for (const Brush& brush : level.brushes) {
        if (isStatic(level, brush)) {
            brushes.push_back(brush);
        }
    }
    return brushes;
}

std::uint32_t secondsToTicks(float seconds) {
    return static_cast<std::uint32_t>(seconds * static_cast<float>(kTickRate) + 0.5f);
}

// The property's value held to the range the editor allows; a value that doesn't parse becomes
// the minimum.
float clampedNumber(const LevelEntity& entity, const EntityClassInfo& entityClass,
                    std::string_view key) {
    const PropertyInfo* property = findPropertyInfo(entityClass, key);
    const float value = numberProperty(entity, entityClass, key);
    return std::max(property->minimum, std::min(value, property->maximum));
}

std::uint32_t clampedCount(const LevelEntity& entity, const EntityClassInfo& entityClass,
                           std::string_view key) {
    const PropertyInfo* property = findPropertyInfo(entityClass, key);
    return std::clamp(countProperty(entity, entityClass, key),
                      static_cast<std::uint32_t>(property->minimum),
                      static_cast<std::uint32_t>(property->maximum));
}

std::string describe(const Level& level, std::uint32_t index) {
    const LevelEntity& entity = level.entities[index];
    const std::string* name = findProperty(entity, "name");
    if (name != nullptr && !name->empty()) {
        return std::format("{} '{}' (entity {})", entity.className, *name, index);
    }
    return std::format("{} (entity {})", entity.className, index);
}

// The mover class's "nav", with auto worked out from how it is set going.
NavMoverKind moverNavKind(const LevelEntity& entity, const EntityClassInfo& moverClass) {
    switch (choiceProperty(entity, moverClass, "nav")) {
    case 1:
        return NavMoverKind::Door;
    case 2:
        return NavMoverKind::Lift;
    case 3:
        return NavMoverKind::Solid;
    default:
        break;
    }
    const Vec3 move = vectorProperty(entity, moverClass, "move");
    if (dot(move, move) == 0.0f) {
        return NavMoverKind::Solid;
    }
    switch (static_cast<MoverActivation>(choiceProperty(entity, moverClass, "activate"))) {
    case MoverActivation::Touch:
        return NavMoverKind::Door;
    case MoverActivation::Stand:
        return NavMoverKind::Lift;
    case MoverActivation::Input:
        break;
    }
    return flagProperty(entity, moverClass, "loop") ? NavMoverKind::Lift : NavMoverKind::Door;
}

} // namespace

NavAgent navAgent(const MovementSettings& movement) {
    NavAgent agent;
    agent.radius = std::max(movement.halfExtents.x, movement.halfExtents.y);
    agent.height = movement.halfExtents.z * 2.0f;
    agent.climb = movement.stepHeight;
    agent.minGroundNormalZ = movement.minGroundNormalZ;
    return agent;
}

NavInput navInput(const Level& level) {
    NavInput input;
    input.solid = staticBrushes(level);
    std::vector<std::uint32_t> movers(level.entities.size(), kNoNavMover);
    for (std::uint32_t i = 0; i < level.entities.size(); ++i) {
        const LevelEntity& entity = level.entities[i];
        const EntityClassInfo* entityClass = findEntityClass(entity.className);
        if (logicClassOf(entity.className) == LogicClass::Mover && entityClass != nullptr) {
            movers[i] = static_cast<std::uint32_t>(input.movers.size());
            NavMover& mover = input.movers.emplace_back();
            mover.id = i;
            mover.kind = moverNavKind(entity, *entityClass);
            mover.move = vectorProperty(entity, *entityClass, "move");
        }
    }
    for (const Brush& brush : level.brushes) {
        if (brush.entity >= level.entities.size()) {
            continue;
        }
        if (movers[brush.entity] != kNoNavMover) {
            input.movers[movers[brush.entity]].brushes.push_back(brush);
        } else if (logicClassOf(level.entities[brush.entity].className) == LogicClass::Hurt) {
            input.hazards.push_back(brush);
        }
    }
    return input;
}

std::string_view logicEventName(LogicEvent event) {
    return kEventNames[static_cast<std::size_t>(event)];
}

std::string_view logicInputName(LogicInput input) {
    return kInputNames[static_cast<std::size_t>(input)];
}

GameLevel::GameLevel(const Level& level, std::optional<SpawnPoint> start,
                     const GameSettings& settings)
    : m_collision(staticBrushes(level)), m_entities(level.entities.size()),
      m_navInput(navInput(level)), m_navAgent(navAgent(settings.movement)) {
    std::string error;
    if (start) {
        m_spawns.push_back(*start);
    } else if (std::optional<std::vector<SpawnPoint>> spawns = readSpawnPoints(level, error)) {
        m_spawns = std::move(*spawns);
    } else {
        m_warnings.push_back(error);
    }

    // Each brush entity's brushes, gathered in one pass.
    std::vector<std::vector<Brush>> owned(level.entities.size());
    for (const Brush& brush : level.brushes) {
        if (!isStatic(level, brush)) {
            owned[brush.entity].push_back(brush);
        }
    }
    for (std::uint32_t i = 0; i < level.entities.size(); ++i) {
        readEntity(level, i, settings);
        if (hasModel(m_entities[i].logicClass)) {
            if (owned[i].empty()) {
                m_warnings.push_back(std::format("{} has no brushes", describe(level, i)));
            } else {
                m_models.push_back(std::make_unique<CollisionWorld>(owned[i]));
                m_entities[i].model = m_models.back().get();
            }
        }
    }
    resolveOutputs(level);
    spawnLogic(settings);
    findKillHeight(level);
    // While the level loads, rather than on an enemy's first thought.
    for (const Entity& entity : m_entities) {
        if (entity.logicClass == LogicClass::Enemy && entity.enemyType < settings.enemies.size()) {
            navMesh(navAgent(settings.enemies[entity.enemyType].movement));
        }
    }
}

const NavMesh& GameLevel::navMesh(const NavAgent& agent) const {
    const std::scoped_lock lock(m_navMutex);
    for (const std::unique_ptr<NavMesh>& mesh : m_navMeshes) {
        if (mesh->agent() == agent) {
            return *mesh;
        }
    }
    return *m_navMeshes.emplace_back(std::make_unique<NavMesh>(m_navInput, agent));
}

void GameLevel::findKillHeight(const Level& level) {
    float lowest = 0.0f;
    bool any = false;
    const auto lower = [&](float z) {
        lowest = any ? std::min(lowest, z) : z;
        any = true;
    };
    for (const Brush& brush : level.brushes) {
        // Movers can carry their brushes lower than they are built.
        float drop = 0.0f;
        if (brush.entity < m_entities.size() &&
            m_entities[brush.entity].logicClass == LogicClass::Mover) {
            drop = std::min(m_entities[brush.entity].move.z, 0.0f);
        }
        for (const Vec3& corner : brush.vertices) {
            lower(corner.z + drop);
        }
    }
    for (const SpawnPoint& spawn : m_spawns) {
        lower(spawn.feetPosition.z);
    }
    m_killHeight = lowest - kOutOfWorldDepth;
}

void GameLevel::readEntity(const Level& level, std::uint32_t index, const GameSettings& settings) {
    const std::span<const WeaponSettings> weapons = settings.weapons;
    const LevelEntity& levelEntity = level.entities[index];
    Entity& entity = m_entities[index];
    entity.logicClass = logicClassOf(levelEntity.className);
    const EntityClassInfo* entityClass = findEntityClass(levelEntity.className);
    if (entity.logicClass == LogicClass::None || entityClass == nullptr) {
        return;
    }
    // Outputs are checked as they are resolved.
    for (const PropertyInfo& property : entityClass->properties) {
        std::string problem;
        if (!checkPropertyValue(property, propertyValue(levelEntity, property), problem)) {
            m_warnings.push_back(std::format("{}: {}", describe(level, index), problem));
        }
    }
    switch (entity.logicClass) {
    case LogicClass::Trigger:
        entity.once = flagProperty(levelEntity, *entityClass, "once");
        entity.startEnabled = !flagProperty(levelEntity, *entityClass, "start_disabled");
        break;
    case LogicClass::Checkpoint:
    case LogicClass::Relay:
        entity.startEnabled = !flagProperty(levelEntity, *entityClass, "start_disabled");
        break;
    case LogicClass::Hurt:
        entity.startEnabled = !flagProperty(levelEntity, *entityClass, "start_disabled");
        entity.damage =
            static_cast<std::int32_t>(clampedCount(levelEntity, *entityClass, "damage"));
        entity.intervalTicks =
            std::max(secondsToTicks(clampedNumber(levelEntity, *entityClass, "interval")), 1u);
        break;
    case LogicClass::Counter:
        entity.count = clampedCount(levelEntity, *entityClass, "count");
        break;
    case LogicClass::Timer:
        entity.intervalTicks =
            std::max(secondsToTicks(clampedNumber(levelEntity, *entityClass, "interval")), 1u);
        entity.repeat = flagProperty(levelEntity, *entityClass, "repeat");
        entity.startOn = flagProperty(levelEntity, *entityClass, "start_on");
        break;
    case LogicClass::Mover: {
        entity.move = vectorProperty(levelEntity, *entityClass, "move");
        entity.length = length(entity.move);
        entity.direction = entity.length > 0.0f ? entity.move / entity.length : Vec3{};
        entity.speed = clampedNumber(levelEntity, *entityClass, "speed") * kTickSeconds;
        entity.activate =
            static_cast<MoverActivation>(choiceProperty(levelEntity, *entityClass, "activate"));
        const float wait = clampedNumber(levelEntity, *entityClass, "wait");
        entity.waitTicks = wait < 0.0f ? -1 : static_cast<std::int32_t>(secondsToTicks(wait));
        entity.loop = flagProperty(levelEntity, *entityClass, "loop");
        entity.startMoving = flagProperty(levelEntity, *entityClass, "start_moving");
        entity.startAtEnd = flagProperty(levelEntity, *entityClass, "start_at_end");
        break;
    }
    case LogicClass::Pickup: {
        entity.startEnabled = !flagProperty(levelEntity, *entityClass, "start_disabled");
        entity.position = vectorProperty(levelEntity, *entityClass, "origin") +
                          Vec3{0.0f, 0.0f, kPickupHalfExtents.z};
        entity.item = static_cast<PickupItem>(choiceProperty(levelEntity, *entityClass, "item"));
        entity.amount =
            static_cast<std::int32_t>(clampedCount(levelEntity, *entityClass, "amount"));
        entity.limit = static_cast<std::int32_t>(clampedCount(levelEntity, *entityClass, "limit"));
        const float respawn = clampedNumber(levelEntity, *entityClass, "respawn");
        entity.respawnTicks =
            respawn < 0.0f ? -1 : static_cast<std::int32_t>(std::max(secondsToTicks(respawn), 1u));
        const std::string_view weapon = textProperty(levelEntity, *entityClass, "weapon");
        for (std::uint32_t i = 0; i < weapons.size() && i < kMaxWeapons; ++i) {
            if (weapons[i].name == weapon) {
                entity.weapon = i;
            }
        }
        const bool needsWeapon =
            entity.item == PickupItem::Ammo || entity.item == PickupItem::Weapon;
        if (needsWeapon && weapon.empty()) {
            m_warnings.push_back(std::format("{}: an {} pickup needs a weapon",
                                             describe(level, index),
                                             entity.item == PickupItem::Ammo ? "ammo" : "weapon"));
        } else if (needsWeapon && entity.weapon == kNoWeapon) {
            m_warnings.push_back(
                std::format("{}: the game has no weapon '{}'", describe(level, index), weapon));
        }
        break;
    }
    case LogicClass::Enemy: {
        entity.position = vectorProperty(levelEntity, *entityClass, "origin");
        const double degrees = angleProperty(levelEntity, *entityClass, "yaw");
        entity.yaw = static_cast<std::uint16_t>(
            std::llround(degrees / 360.0 * static_cast<double>(kAngleUnitsPerTurn)) & 0xFFFF);
        entity.ambush = flagProperty(levelEntity, *entityClass, "ambush");
        const std::string_view type = textProperty(levelEntity, *entityClass, "type");
        if (settings.enemies.empty()) {
            m_warnings.push_back(std::format("{}: the game has no enemies (enemies in game.cfg)",
                                             describe(level, index)));
            break;
        }
        if (type.empty()) {
            entity.enemyType = 0;
        }
        for (std::uint32_t i = 0; i < settings.enemies.size(); ++i) {
            if (settings.enemies[i].name == type) {
                entity.enemyType = i;
            }
        }
        if (entity.enemyType == kNoEnemyType) {
            m_warnings.push_back(
                std::format("{}: the game has no enemy '{}'", describe(level, index), type));
        }
        break;
    }
    case LogicClass::None:
        break;
    }
}

void GameLevel::resolveOutputs(const Level& level) {
    // Entities by name, sorted, to find what each output targets.
    std::vector<std::pair<std::string_view, std::uint32_t>> names;
    for (std::uint32_t i = 0; i < level.entities.size(); ++i) {
        const std::string* name = findProperty(level.entities[i], "name");
        if (name != nullptr && !name->empty()) {
            names.emplace_back(*name, i);
        }
    }
    std::sort(names.begin(), names.end());

    // Outputs and targets are gathered as ranges first and turned into spans once both vectors
    // have stopped growing.
    struct Range {
        std::size_t first = 0;
        std::size_t count = 0;
    };
    std::vector<Range> outputRanges(level.entities.size());
    std::vector<Range> targetRanges;

    for (std::uint32_t source = 0; source < level.entities.size(); ++source) {
        const LevelEntity& levelEntity = level.entities[source];
        const EntityClassInfo* entityClass = findEntityClass(levelEntity.className);
        if (m_entities[source].logicClass == LogicClass::None || entityClass == nullptr) {
            continue;
        }
        outputRanges[source].first = m_outputs.size();
        for (const EntityOutput& levelOutput : levelEntity.outputs) {
            const std::optional<LogicEvent> event =
                findByName<LogicEvent>(kEventNames, levelOutput.event);
            if (!event || findSignal(entityClass->events, levelOutput.event) == nullptr) {
                m_warnings.push_back(std::format("{} has no event '{}'", describe(level, source),
                                                 levelOutput.event));
                continue;
            }
            Range targets{m_targets.size(), 0};
            auto it = std::lower_bound(
                names.begin(), names.end(), std::string_view(levelOutput.target),
                [](const auto& entry, std::string_view name) { return entry.first < name; });
            if (it == names.end() || it->first != levelOutput.target) {
                m_warnings.push_back(std::format("{} {}: no entity is named '{}'",
                                                 describe(level, source), levelOutput.event,
                                                 levelOutput.target));
            }
            for (; it != names.end() && it->first == levelOutput.target; ++it) {
                const std::uint32_t target = it->second;
                const EntityClassInfo* targetClass =
                    findEntityClass(level.entities[target].className);
                const std::optional<LogicInput> input =
                    findByName<LogicInput>(kInputNames, levelOutput.input);
                if (m_entities[target].logicClass == LogicClass::None || targetClass == nullptr ||
                    !input || findSignal(targetClass->inputs, levelOutput.input) == nullptr) {
                    m_warnings.push_back(std::format("{} {}: {} has no input '{}'",
                                                     describe(level, source), levelOutput.event,
                                                     describe(level, target), levelOutput.input));
                    continue;
                }
                m_targets.push_back({target, *input});
                ++targets.count;
            }
            m_outputs.push_back({*event, secondsToTicks(levelOutput.delay), {}});
            targetRanges.push_back(targets);
            ++outputRanges[source].count;
        }
    }

    for (std::size_t i = 0; i < m_outputs.size(); ++i) {
        m_outputs[i].targets =
            std::span(m_targets).subspan(targetRanges[i].first, targetRanges[i].count);
    }
    for (std::size_t i = 0; i < m_entities.size(); ++i) {
        m_entities[i].outputs =
            std::span(m_outputs).subspan(outputRanges[i].first, outputRanges[i].count);
    }
}

void GameLevel::spawnLogic(const GameSettings& settings) {
    m_start = std::make_unique<State>();
    World& world = m_start->world;
    std::uint32_t spawned = 0;
    for (std::uint32_t index = 0; index < m_entities.size(); ++index) {
        Entity& entity = m_entities[index];
        if (entity.logicClass == LogicClass::None) {
            continue;
        }
        if (spawned == kMaxLevelLogicEntities) {
            m_warnings.push_back(
                std::format("the level has more than {} logic entities; the rest do nothing",
                            kMaxLevelLogicEntities));
            break;
        }
        ++spawned;
        entity.id = world.entities.create();
        world.levelEntities.add(entity.id, {index});
        const std::uint32_t enabled = entity.startEnabled ? kLogicEnabled : 0u;
        switch (entity.logicClass) {
        case LogicClass::Trigger:
            world.triggers.add(entity.id, {enabled, 0});
            break;
        case LogicClass::Checkpoint:
            world.checkpoints.add(entity.id, {enabled, 0});
            break;
        case LogicClass::Hurt:
            world.hurts.add(entity.id, {enabled});
            break;
        case LogicClass::Relay:
            world.relays.add(entity.id, {enabled});
            break;
        case LogicClass::Counter:
            world.counters.add(entity.id, {});
            break;
        case LogicClass::Timer:
            world.timers.add(entity.id,
                             entity.startOn ? Timer{kLogicEnabled, entity.intervalTicks} : Timer{});
            break;
        case LogicClass::Mover: {
            Mover mover;
            if (entity.startAtEnd) {
                mover.distance = entity.length;
                mover.offset = entity.move;
                mover.phase = static_cast<std::uint32_t>(MoverPhase::AtEnd);
            }
            mover.departTick = entity.startMoving ? 1u : 0u;
            world.movers.add(entity.id, mover);
            break;
        }
        case LogicClass::Pickup:
            world.pickups.add(entity.id, {enabled, 0, {}});
            break;
        case LogicClass::Enemy:
            if (entity.enemyType < settings.enemies.size()) {
                makeEnemy(world, entity.id, settings, entity.enemyType, entity.position,
                          entity.yaw);
                if (entity.ambush) {
                    world.enemies.get(entity.id)->flags |= kEnemyAmbush;
                }
            }
            break;
        case LogicClass::None:
            break;
        }
    }
}

} // namespace ct
