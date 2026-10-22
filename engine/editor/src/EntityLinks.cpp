#include "croftech/editor/EntityLinks.h"

#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/LevelCommands.h"
#include "croftech/editor/LevelItem.h"
#include "croftech/level/BrushGeometry.h"
#include "croftech/level/EntityClasses.h"

#include <algorithm>
#include <format>
#include <memory>
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

NameIndex::const_iterator firstNamed(const NameIndex& names, std::string_view name) {
    return std::lower_bound(
        names.begin(), names.end(), name,
        [](const auto& entry, std::string_view wanted) { return entry.first < wanted; });
}

// The references from each Reference property of the entity to each entity with the name.
void addReferences(const Level& level, const NameIndex& names, std::size_t from,
                   std::vector<Reference>& references) {
    const LevelEntity& entity = level.entities[from];
    const EntityClassInfo* entityClass = findEntityClass(entity.className);
    if (entityClass == nullptr) {
        return;
    }
    for (const PropertyInfo& property : entityClass->properties) {
        if (property.type != PropertyType::Reference) {
            continue;
        }
        const std::string_view target = propertyValue(entity, property);
        for (auto it = firstNamed(names, target);
             !target.empty() && it != names.end() && it->first == target; ++it) {
            references.push_back({from, it->second, property.key});
        }
    }
}

// The wires from each output of the entity to each entity with its target name.
void addOutputWires(const Level& level, const NameIndex& names, std::size_t from,
                    std::vector<Wire>& wires) {
    const std::vector<EntityOutput>& outputs = level.entities[from].outputs;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const std::string_view target = outputs[i].target;
        for (auto it = firstNamed(names, target); it != names.end() && it->first == target; ++it) {
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

std::vector<Reference> entityReferences(const Level& level, std::size_t entity) {
    std::vector<Reference> references;
    if (entity >= level.entities.size()) {
        return references;
    }
    const NameIndex names = indexNames(level);
    std::vector<bool> onChain(level.entities.size(), false);
    std::vector<std::size_t> chain{entity};
    onChain[entity] = true;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const std::size_t first = references.size();
        addReferences(level, names, chain[i], references);
        for (std::size_t j = first; j < references.size(); ++j) {
            if (!onChain[references[j].to]) {
                onChain[references[j].to] = true;
                chain.push_back(references[j].to);
            }
        }
    }
    std::vector<Reference> from;
    for (std::size_t other = 0; other < level.entities.size(); ++other) {
        if (onChain[other]) {
            continue;
        }
        from.clear();
        addReferences(level, names, other, from);
        for (const Reference& reference : from) {
            if (reference.to == entity) {
                references.push_back(reference);
            }
        }
    }
    return references;
}

std::vector<Reference> allReferences(const Level& level) {
    std::vector<Reference> references;
    const NameIndex names = indexNames(level);
    for (std::size_t from = 0; from < level.entities.size(); ++from) {
        addReferences(level, names, from, references);
    }
    return references;
}

bool addPatrolPoint(EditorDocument& document, std::size_t entity, float grid) {
    const Level& level = document.level();
    if (entity >= level.entities.size() || level.entities.size() >= kMaxLevelEntities) {
        return false;
    }
    const LevelEntity& source = level.entities[entity];
    const bool enemy = source.className == kEnemyClass;
    const std::optional<Vec3> origin = entityOrigin(source);
    if ((!enemy && source.className != kPatrolPointClass) || !origin) {
        return false;
    }
    const std::string key = enemy ? "patrol" : "next";

    // On the way the enemy faces, or the way the route came into the point.
    Vec3 direction{1.0f, 0.0f, 0.0f};
    if (enemy) {
        direction = entityFacing(source).value_or(direction);
    } else {
        for (const Reference& reference : allReferences(level)) {
            const std::optional<Vec3> before = entityAnchor(level, reference.from);
            if (reference.to != entity || reference.from == entity || !before) {
                continue;
            }
            const Vec3 along{origin->x - before->x, origin->y - before->y, 0.0f};
            if (dot(along, along) > 0.0f) {
                direction = normalize(along);
                break;
            }
        }
    }
    Vec3 point = *origin + direction * kPatrolPointStep;
    if (grid > 0.0f) {
        point = {snapToGrid(point.x, grid), snapToGrid(point.y, grid), origin->z};
    }

    // Named after the route: the enemy's name, or the point's without its number.
    std::string base = "patrol";
    if (const std::string* name = findProperty(source, "name"); name != nullptr && !name->empty()) {
        base = *name;
        if (!enemy) {
            const std::size_t digits = base.find_last_not_of("0123456789");
            if (digits != std::string::npos && digits + 1 < base.size() && base[digits] == '_') {
                base.resize(digits);
            }
        }
    }
    const NameIndex names = indexNames(level);
    std::string name;
    for (int number = 1;; ++number) {
        name = std::format("{}_{}", base, number);
        const auto it = firstNamed(names, name);
        if (it == names.end() || it->first != name) {
            break;
        }
    }
    if (!isValidEntityName(name)) {
        return false;
    }

    LevelEntity added{std::string(kPatrolPointClass), {}};
    setProperty(added, "origin", formatVec3(point));
    setProperty(added, "name", name);
    if (const std::string* next = findProperty(source, key); next != nullptr && !next->empty()) {
        setProperty(added, "next", *next);
    }
    added.group = source.group;
    LevelEntity linked = source;
    setProperty(linked, key, name);

    const std::string command = "Add patrol point";
    const std::size_t index = level.entities.size();
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(makeSetEntity(command, entity, source, std::move(linked)));
    commands.push_back(makeInsertEntity(command, index, std::move(added)));
    document.execute(makeCompound(command, std::move(commands)));
    document.select(LevelItem::entity(index));
    return true;
}

} // namespace ct
