#include "croftech/editor/EntityLinks.h"

#include "croftech/editor/LevelCommands.h"
#include "croftech/level/BrushGeometry.h"
#include "croftech/level/EntityClasses.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace ct {
namespace {

std::optional<Aabb> brushBounds(const Level& level, std::size_t entity) {
    std::optional<Aabb> bounds;
    for (const Brush& brush : level.brushes) {
        Aabb more;
        if (brush.entity == entity && computeBrushBounds(brush, more)) {
            bounds = bounds ? Aabb{min(bounds->min, more.min), max(bounds->max, more.max)} : more;
        }
    }
    return bounds;
}

const std::string* entityName(const LevelEntity& entity) {
    const std::string* name = findProperty(entity, "name");
    return name != nullptr && !name->empty() ? name : nullptr;
}

// Every named entity by name, sorted, for finding the targets of outputs.
using NameIndex = std::vector<std::pair<std::string_view, std::size_t>>;

NameIndex indexNames(const Level& level) {
    NameIndex names;
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        if (const std::string* name = entityName(level.entities[i])) {
            names.emplace_back(*name, i);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

// The wires from each output of the entity to each entity with its target name.
void addOutputWires(const Level& level, const NameIndex& names, std::size_t from,
                    std::vector<Wire>& wires) {
    const std::vector<EntityOutput>& outputs = level.entities[from].outputs;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const std::string_view target = outputs[i].target;
        auto it = std::lower_bound(
            names.begin(), names.end(), target,
            [](const auto& entry, std::string_view name) { return entry.first < name; });
        for (; it != names.end() && it->first == target; ++it) {
            wires.push_back({from, it->second, i});
        }
    }
}

} // namespace

std::optional<std::size_t> selectedEntity(const EditorDocument& document) {
    std::optional<std::size_t> entity;
    for (const LevelItem& item : document.selection()) {
        if (item.kind == LevelItem::Kind::Entity) {
            if (entity) {
                return std::nullopt;
            }
            entity = item.index;
        }
    }
    if (!entity) {
        return std::nullopt;
    }
    for (const LevelItem& item : document.selection()) {
        if (item.kind == LevelItem::Kind::Brush &&
            document.level().brushes[item.index].entity != *entity) {
            return std::nullopt;
        }
    }
    return entity;
}

std::optional<Vec3> entityAnchor(const Level& level, std::size_t entity) {
    if (entity >= level.entities.size()) {
        return std::nullopt;
    }
    if (const std::optional<Vec3> origin = entityOrigin(level.entities[entity])) {
        return origin;
    }
    if (const std::optional<Aabb> bounds = brushBounds(level, entity)) {
        return (bounds->min + bounds->max) * 0.5f;
    }
    return std::nullopt;
}

std::optional<MoverPath> moverPath(const Level& level, std::size_t entity) {
    if (entity >= level.entities.size()) {
        return std::nullopt;
    }
    const LevelEntity& levelEntity = level.entities[entity];
    const EntityClassInfo* entityClass = findEntityClass(levelEntity.className);
    if (entityClass == nullptr || findPropertyInfo(*entityClass, "move") == nullptr) {
        return std::nullopt;
    }
    const std::optional<Aabb> bounds = brushBounds(level, entity);
    if (!bounds) {
        return std::nullopt;
    }
    return MoverPath{entity, *bounds, vectorProperty(levelEntity, *entityClass, "move")};
}

bool setMoverEnd(EditorDocument& document, std::size_t entity, Vec3 move) {
    if (!moverPath(document.level(), entity)) {
        return false;
    }
    const LevelEntity& before = document.level().entities[entity];
    LevelEntity after = before;
    setProperty(after, "move", formatVec3(move + Vec3{}));
    if (after == before) {
        return false;
    }
    document.execute(makeSetEntity("Move mover end", entity, before, std::move(after)));
    return true;
}

std::vector<Wire> entityWires(const Level& level, std::size_t entity) {
    std::vector<Wire> wires;
    if (entity >= level.entities.size()) {
        return wires;
    }
    addOutputWires(level, indexNames(level), entity, wires);
    const std::string* name = entityName(level.entities[entity]);
    if (name == nullptr) {
        return wires;
    }
    for (std::size_t from = 0; from < level.entities.size(); ++from) {
        if (from == entity) {
            continue; // its wires to itself are among its outputs already
        }
        const std::vector<EntityOutput>& outputs = level.entities[from].outputs;
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            if (outputs[i].target == *name) {
                wires.push_back({from, entity, i});
            }
        }
    }
    return wires;
}

std::vector<Wire> allWires(const Level& level) {
    std::vector<Wire> wires;
    const NameIndex names = indexNames(level);
    for (std::size_t from = 0; from < level.entities.size(); ++from) {
        addOutputWires(level, names, from, wires);
    }
    return wires;
}

} // namespace ct
