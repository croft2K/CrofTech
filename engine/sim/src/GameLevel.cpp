#include "croftech/sim/GameLevel.h"

#include "croftech/level/EntityClasses.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>

namespace ct {
namespace {

constexpr std::string_view kEventNames[] = {
    "on_enter",       "on_exit",        "on_trigger",   "on_reached",       "on_timer",
    "on_leave_start", "on_reached_end", "on_leave_end", "on_reached_start",
};
static_assert(std::size(kEventNames) == static_cast<std::size_t>(LogicEvent::OnReachedStart) + 1);

constexpr std::string_view kInputNames[] = {
    "enable", "disable", "toggle",      "trigger",       "add",    "reset",
    "start",  "stop",    "move_to_end", "move_to_start", "resume",
};
static_assert(std::size(kInputNames) == static_cast<std::size_t>(LogicInput::Resume) + 1);

template <typename Enum, std::size_t N>
std::optional<Enum> findByName(const std::string_view (&names)[N], std::string_view name) {
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == name) {
            return static_cast<Enum>(i);
        }
    }
    return std::nullopt;
}

LogicClass logicClassOf(std::string_view className) {
    if (className == "trigger") {
        return LogicClass::Trigger;
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
    return LogicClass::None;
}

// Triggers are volumes players walk through, not walls.
bool isSolid(const Level& level, const Brush& brush) {
    return brush.entity >= level.entities.size() ||
           logicClassOf(level.entities[brush.entity].className) != LogicClass::Trigger;
}

std::vector<Brush> solidBrushes(const Level& level) {
    std::vector<Brush> brushes;
    for (const Brush& brush : level.brushes) {
        if (isSolid(level, brush)) {
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

} // namespace

std::string_view logicEventName(LogicEvent event) {
    return kEventNames[static_cast<std::size_t>(event)];
}

std::string_view logicInputName(LogicInput input) {
    return kInputNames[static_cast<std::size_t>(input)];
}

GameLevel::GameLevel(const Level& level)
    : m_collision(solidBrushes(level)), m_entities(level.entities.size()) {
    std::string error;
    if (std::optional<std::vector<SpawnPoint>> spawns = readSpawnPoints(level, error)) {
        m_spawns = std::move(*spawns);
    } else {
        m_warnings.push_back(error);
    }

    // Each brush entity's brushes, gathered in one pass.
    std::vector<std::vector<Brush>> owned(level.entities.size());
    for (const Brush& brush : level.brushes) {
        if (brush.entity < level.entities.size() && !isSolid(level, brush)) {
            owned[brush.entity].push_back(brush);
        }
    }
    for (std::uint32_t i = 0; i < level.entities.size(); ++i) {
        readEntity(level, i);
        if (m_entities[i].logicClass == LogicClass::Trigger) {
            if (owned[i].empty()) {
                m_warnings.push_back(std::format("{} has no brushes", describe(level, i)));
            } else {
                m_models.push_back(std::make_unique<CollisionWorld>(owned[i]));
                m_entities[i].model = m_models.back().get();
            }
        }
    }
    resolveOutputs(level);
    spawnLogic();
}

void GameLevel::readEntity(const Level& level, std::uint32_t index) {
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
    case LogicClass::Relay:
        entity.startEnabled = !flagProperty(levelEntity, *entityClass, "start_disabled");
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
    case LogicClass::Mover:
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

void GameLevel::spawnLogic() {
    m_start = std::make_unique<State>();
    World& world = m_start->world;
    std::uint32_t spawned = 0;
    for (std::uint32_t index = 0; index < m_entities.size(); ++index) {
        Entity& entity = m_entities[index];
        // Movers keep to the world's collision until they can move.
        if (entity.logicClass == LogicClass::None || entity.logicClass == LogicClass::Mover) {
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
        case LogicClass::Mover:
        case LogicClass::None:
            break;
        }
    }
}

} // namespace ct
