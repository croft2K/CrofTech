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
    "on_enter",       "on_exit",        "on_trigger",   "on_reached",       "on_timer",
    "on_leave_start", "on_reached_end", "on_leave_end", "on_reached_start", "on_pickup",
    "on_alert",       "on_death",       "on_arrive",    "on_spawn",         "on_start",
    "on_wave",        "on_cleared",
};
static_assert(std::size(kEventNames) == static_cast<std::size_t>(LogicEvent::OnCleared) + 1);

constexpr std::string_view kInputNames[] = {
    "enable", "disable",     "toggle",        "trigger", "add",   "reset", "start",
    "stop",   "move_to_end", "move_to_start", "resume",  "alert", "spawn",
};
static_assert(std::size(kInputNames) == static_cast<std::size_t>(LogicInput::Spawn) + 1);

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
    if (className == kPatrolPointClass) {
        return LogicClass::PatrolPoint;
    }
    if (className == kSpawnerClass) {
        return LogicClass::Spawner;
    }
    if (className == kEncounterClass) {
        return LogicClass::Encounter;
    }
    if (className == kScriptClass) {
        return LogicClass::Script;
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

// A body placed with its feet a little inside the floor (on a floor off the editor's grid, say) is
// lifted out onto it, by up to a step; one placed clear of the floor stays exactly where it is.
Vec3 liftOutOfFloor(const CollisionWorld& world, Vec3 feet, Vec3 halfExtents, float climb) {
    const Vec3 centre = feet + Vec3{0.0f, 0.0f, halfExtents.z + CollisionWorld::kSurfaceEpsilon};
    if (!world.overlapsBox(centre, halfExtents, CollisionWorld::kSurfaceEpsilon * 0.5f)) {
        return feet;
    }
    const TraceResult down = world.traceBox(centre + Vec3{0.0f, 0.0f, climb}, centre, halfExtents);
    if (down.startSolid) {
        return feet;
    }
    return down.endPosition - Vec3{0.0f, 0.0f, halfExtents.z + CollisionWorld::kSurfaceEpsilon};
}

// Entities by name, sorted, with their indices, to find the entities a name stands for.
using NameTable = std::vector<std::pair<std::string_view, std::uint32_t>>;

NameTable sortedNames(const Level& level) {
    NameTable names;
    for (std::uint32_t i = 0; i < level.entities.size(); ++i) {
        const std::string* name = findProperty(level.entities[i], "name");
        if (name != nullptr && !name->empty()) {
            names.emplace_back(*name, i);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

NameTable::const_iterator firstNamed(const NameTable& names, std::string_view name) {
    return std::lower_bound(
        names.begin(), names.end(), name,
        [](const auto& entry, std::string_view wanted) { return entry.first < wanted; });
}

std::string describeEntity(const Level& level, std::uint32_t index) {
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
    const auto index = static_cast<std::size_t>(event);
    return index < std::size(kEventNames) ? kEventNames[index] : "script event";
}

std::string_view logicInputName(LogicInput input) {
    const auto index = static_cast<std::size_t>(input);
    return index < std::size(kInputNames) ? kInputNames[index] : "script input";
}

GameLevel::GameLevel(const Level& level, std::optional<SpawnPoint> start,
                     const GameSettings& settings, std::span<const ScriptSource> scripts)
    : m_collision(staticBrushes(level)), m_entities(level.entities.size()),
      m_navInput(navInput(level)), m_navAgent(navAgent(settings.movement)) {
    for (const auto& [name, index] : sortedNames(level)) {
        m_names.push_back({std::string(name), index});
    }
    loadScripts(scripts);
    std::string error;
    if (start) {
        m_spawns.push_back(*start);
    } else if (std::optional<std::vector<SpawnPoint>> spawns = readSpawnPoints(level, error)) {
        m_spawns = std::move(*spawns);
    } else {
        m_warnings.push_back(error);
    }
    for (SpawnPoint& spawn : m_spawns) {
        spawn.feetPosition =
            liftOutOfFloor(m_collision, spawn.feetPosition, settings.movement.halfExtents,
                           settings.movement.stepHeight);
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
                m_warnings.push_back(std::format("{} has no brushes", describeEntity(level, i)));
            } else {
                m_models.push_back(std::make_unique<CollisionWorld>(owned[i]));
                m_entities[i].model = m_models.back().get();
            }
        }
    }
    resolveOutputs(level);
    resolveReferences(level);
    numberWaves(level);
    spawnLogic(settings);
    findKillHeight(level);
    // While the level loads, rather than on an enemy's first thought.
    for (const Entity& entity : m_entities) {
        if ((entity.logicClass == LogicClass::Enemy || entity.logicClass == LogicClass::Spawner) &&
            entity.enemyType < settings.enemies.size()) {
            navMesh(navAgent(settings.enemies[entity.enemyType].movement));
        }
    }
}

void GameLevel::loadScripts(std::span<const ScriptSource> scripts) {
    for (const ScriptSource& source : scripts) {
        std::string error;
        std::optional<ScriptProgram> program = ScriptProgram::load(source.name, source.text, error);
        if (program) {
            m_scripts.push_back(std::make_unique<ScriptProgram>(std::move(*program)));
        } else {
            m_warnings.push_back(std::format("scripts/{}.luau: {}", source.name, error));
        }
    }
}

std::span<const GameLevel::NamedEntity> GameLevel::entitiesNamed(std::string_view name) const {
    const auto first = std::lower_bound(
        m_names.begin(), m_names.end(), name,
        [](const NamedEntity& entry, std::string_view wanted) { return entry.name < wanted; });
    auto last = first;
    while (last != m_names.end() && last->name == name) {
        ++last;
    }
    return {first, last};
}

std::optional<LogicInput> GameLevel::findInput(std::uint32_t index, std::string_view input) const {
    const Entity* target = entity(index);
    if (target == nullptr || target->logicClass == LogicClass::None ||
        target->classInfo == nullptr) {
        return std::nullopt;
    }
    if (target->classInfo->scripted) {
        if (target->script == nullptr) {
            return std::nullopt;
        }
        const std::optional<std::uint32_t> found = target->script->findInput(input);
        return found ? std::optional(scriptInput(*found)) : std::nullopt;
    }
    if (findSignal(target->classInfo->inputs, input) == nullptr) {
        return std::nullopt;
    }
    return findByName<LogicInput>(kInputNames, input);
}

std::string GameLevel::describe(std::uint32_t index) const {
    const Entity* found = entity(index);
    const std::string_view className =
        found != nullptr && found->classInfo != nullptr ? found->classInfo->name : "entity";
    if (found != nullptr && !found->name.empty()) {
        return std::format("{} '{}' (entity {})", className, found->name, index);
    }
    return std::format("{} (entity {})", className, index);
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
    entity.classInfo = entityClass;
    if (const std::string* name = findProperty(levelEntity, "name")) {
        entity.name = *name;
    }
    if (entity.logicClass == LogicClass::None || entityClass == nullptr) {
        return;
    }
    // Outputs are checked as they are resolved.
    for (const PropertyInfo& property : entityClass->properties) {
        std::string problem;
        if (!checkPropertyValue(property, propertyValue(levelEntity, property), problem)) {
            m_warnings.push_back(std::format("{}: {}", describeEntity(level, index), problem));
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
                                             describeEntity(level, index),
                                             entity.item == PickupItem::Ammo ? "ammo" : "weapon"));
        } else if (needsWeapon && entity.weapon == kNoWeapon) {
            m_warnings.push_back(std::format("{}: the game has no weapon '{}'",
                                             describeEntity(level, index), weapon));
        }
        break;
    }
    case LogicClass::Enemy:
    case LogicClass::Spawner: {
        entity.position = vectorProperty(levelEntity, *entityClass, "origin");
        const double degrees = angleProperty(levelEntity, *entityClass, "yaw");
        entity.yaw = static_cast<std::uint16_t>(
            std::llround(degrees / 360.0 * static_cast<double>(kAngleUnitsPerTurn)) & 0xFFFF);
        if (entity.logicClass == LogicClass::Enemy) {
            entity.ambush = flagProperty(levelEntity, *entityClass, "ambush");
        } else {
            // The wave as set; numberWaves turns it into the encounter's own numbering.
            entity.wave = clampedCount(levelEntity, *entityClass, "wave");
            entity.count = clampedCount(levelEntity, *entityClass, "count");
            entity.hunt = flagProperty(levelEntity, *entityClass, "hunt");
        }
        const std::string_view type = textProperty(levelEntity, *entityClass, "type");
        if (settings.enemies.empty()) {
            m_warnings.push_back(std::format("{}: the game has no enemies (enemies in game.cfg)",
                                             describeEntity(level, index)));
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
                std::format("{}: the game has no enemy '{}'", describeEntity(level, index), type));
            break;
        }
        const MovementSettings& movement = settings.enemies[entity.enemyType].movement;
        entity.position =
            liftOutOfFloor(m_collision, entity.position, movement.halfExtents, movement.stepHeight);
        break;
    }
    case LogicClass::PatrolPoint:
        entity.position = vectorProperty(levelEntity, *entityClass, "origin");
        entity.waitTicks = static_cast<std::int32_t>(
            secondsToTicks(clampedNumber(levelEntity, *entityClass, "wait")));
        break;
    case LogicClass::Encounter:
        entity.maxAlive = clampedCount(levelEntity, *entityClass, "max_alive");
        entity.nextWaveAt = clampedCount(levelEntity, *entityClass, "next_wave_at");
        entity.waveDelayTicks =
            secondsToTicks(clampedNumber(levelEntity, *entityClass, "wave_delay"));
        entity.startOn = flagProperty(levelEntity, *entityClass, "start_on");
        break;
    case LogicClass::Script: {
        const std::string_view script = textProperty(levelEntity, *entityClass, "script");
        if (script.empty()) {
            m_warnings.push_back(std::format("{}: names no script", describeEntity(level, index)));
            break;
        }
        for (const std::unique_ptr<ScriptProgram>& program : m_scripts) {
            if (program->name() == script) {
                entity.script = program.get();
            }
        }
        // One that was given but failed to compile has been warned about already.
        if (entity.script == nullptr) {
            m_warnings.push_back(std::format("{}: script '{}' isn't loaded (scripts/{}.luau)",
                                             describeEntity(level, index), script, script));
        }
        break;
    }
    case LogicClass::None:
        break;
    }
}

void GameLevel::resolveOutputs(const Level& level) {
    const NameTable names = sortedNames(level);

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
            std::optional<LogicEvent> event;
            if (!entityClass->scripted) {
                event = findSignal(entityClass->events, levelOutput.event) != nullptr
                            ? findByName<LogicEvent>(kEventNames, levelOutput.event)
                            : std::nullopt;
            } else if (const ScriptProgram* script = m_entities[source].script) {
                const std::optional<std::uint32_t> found = script->findEvent(levelOutput.event);
                event = found ? std::optional(scriptEvent(*found)) : std::nullopt;
            }
            if (!event) {
                m_warnings.push_back(std::format("{} has no event '{}'",
                                                 describeEntity(level, source), levelOutput.event));
                continue;
            }
            Range targets{m_targets.size(), 0};
            auto it = firstNamed(names, levelOutput.target);
            if (it == names.end() || it->first != levelOutput.target) {
                m_warnings.push_back(std::format("{} {}: no entity is named '{}'",
                                                 describeEntity(level, source), levelOutput.event,
                                                 levelOutput.target));
            }
            for (; it != names.end() && it->first == levelOutput.target; ++it) {
                const std::uint32_t target = it->second;
                const std::optional<LogicInput> input = findInput(target, levelOutput.input);
                if (!input) {
                    m_warnings.push_back(std::format(
                        "{} {}: {} has no input '{}'", describeEntity(level, source),
                        levelOutput.event, describeEntity(level, target), levelOutput.input));
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

void GameLevel::resolveReferences(const Level& level) {
    const NameTable names = sortedNames(level);
    for (std::uint32_t index = 0; index < m_entities.size(); ++index) {
        Entity& entity = m_entities[index];
        const LevelEntity& levelEntity = level.entities[index];
        const EntityClassInfo* entityClass = findEntityClass(levelEntity.className);
        std::string_view key;
        LogicClass named = LogicClass::PatrolPoint;
        if (entity.logicClass == LogicClass::Enemy) {
            key = "patrol";
        } else if (entity.logicClass == LogicClass::PatrolPoint) {
            key = "next";
        } else if (entity.logicClass == LogicClass::Spawner) {
            key = "encounter";
            named = LogicClass::Encounter;
        }
        if (key.empty() || entityClass == nullptr) {
            continue;
        }
        const std::string_view name = textProperty(levelEntity, *entityClass, key);
        if (name.empty()) {
            continue;
        }
        // Of several points with the name, the first.
        const auto it = firstNamed(names, name);
        if (it == names.end() || it->first != name) {
            m_warnings.push_back(std::format("{} {}: no entity is named '{}'",
                                             describeEntity(level, index), key, name));
        } else if (m_entities[it->second].logicClass != named) {
            m_warnings.push_back(
                std::format("{} {}: {} is not {}", describeEntity(level, index), key,
                            describeEntity(level, it->second),
                            named == LogicClass::Encounter ? "an encounter" : "a patrol point"));
        } else if (named == LogicClass::Encounter) {
            entity.encounter = it->second;
        } else {
            entity.next = it->second;
        }
    }
}

void GameLevel::numberWaves(const Level& level) {
    for (std::uint32_t index = 0; index < m_entities.size(); ++index) {
        Entity& encounter = m_entities[index];
        if (encounter.logicClass != LogicClass::Encounter) {
            continue;
        }
        std::vector<std::uint32_t> waves;
        for (const Entity& spawner : m_entities) {
            if (spawner.logicClass == LogicClass::Spawner && spawner.encounter == index) {
                waves.push_back(spawner.wave);
            }
        }
        std::sort(waves.begin(), waves.end());
        waves.erase(std::unique(waves.begin(), waves.end()), waves.end());
        encounter.waveCount = static_cast<std::uint32_t>(waves.size());
        if (waves.empty()) {
            m_warnings.push_back(std::format("{}: no spawner names it, so it has no waves",
                                             describeEntity(level, index)));
        }
        for (Entity& spawner : m_entities) {
            if (spawner.logicClass == LogicClass::Spawner && spawner.encounter == index) {
                spawner.wave = static_cast<std::uint32_t>(
                    std::lower_bound(waves.begin(), waves.end(), spawner.wave) - waves.begin());
            }
        }
    }
}

void GameLevel::spawnLogic(const GameSettings& settings) {
    m_start = std::make_unique<State>();
    World& world = m_start->world;
    std::uint32_t spawned = 0;
    for (std::uint32_t index = 0; index < m_entities.size(); ++index) {
        Entity& entity = m_entities[index];
        // Patrol points are only places, which nothing in play changes.
        if (entity.logicClass == LogicClass::None || entity.logicClass == LogicClass::PatrolPoint) {
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
                Enemy& enemy = *world.enemies.get(entity.id);
                if (entity.ambush) {
                    enemy.flags |= kEnemyAmbush;
                }
                if (entity.next != kNoEntity) {
                    enemy.state = static_cast<std::uint32_t>(EnemyState::Patrol);
                    enemy.patrolPoint = entity.next;
                    enemy.goal = m_entities[entity.next].position;
                }
            }
            break;
        case LogicClass::Spawner:
            world.spawners.add(entity.id, {});
            break;
        case LogicClass::Encounter: {
            Encounter encounter;
            if (entity.startOn) {
                encounter.phase = static_cast<std::uint32_t>(EncounterPhase::Between);
            }
            world.encounters.add(entity.id, encounter);
            break;
        }
        case LogicClass::Script: {
            Script script;
            if (entity.script != nullptr) {
                const std::vector<ScriptVariable>& state = entity.script->declarations().state;
                for (std::size_t i = 0; i < state.size(); ++i) {
                    script.values[i] = state[i].initial;
                }
            }
            world.scripts.add(entity.id, script);
            break;
        }
        case LogicClass::PatrolPoint:
        case LogicClass::None:
            break;
        }
    }

    // Start functions, called on the first tick, in level order.
    std::uint32_t sent = 0;
    for (std::uint32_t index = 0; index < m_entities.size(); ++index) {
        const Entity& entity = m_entities[index];
        if (!entity.id.isValid() || entity.script == nullptr ||
            !entity.script->declarations().start) {
            continue;
        }
        const EntityId id = world.entities.create();
        if (!id.isValid()) {
            break;
        }
        world.pendingInputs.add(id, {0,
                                     0,
                                     sent++,
                                     index,
                                     kDirectInput,
                                     {},
                                     index,
                                     static_cast<std::uint32_t>(kScriptStartInput)});
    }
}

} // namespace ct
