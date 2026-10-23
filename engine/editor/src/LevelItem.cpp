#include "croftech/editor/LevelItem.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/level/BrushGeometry.h"
#include "croftech/level/EntityClasses.h"
#include "croftech/sim/GameSettings.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace ct {
namespace {

constexpr float kEntityMarkerHalfSize = 0.25f;

} // namespace

bool exists(const Level& level, LevelItem item) {
    return item.kind == LevelItem::Kind::Brush ? item.index < level.brushes.size()
                                               : item.index < level.entities.size();
}

std::vector<LevelItem> withWholeEntities(const Level& level, std::span<const LevelItem> items) {
    std::vector<bool> whole(level.entities.size(), false);
    std::vector<LevelItem> result;
    for (const LevelItem& item : items) {
        if (!exists(level, item)) {
            continue;
        }
        result.push_back(item);
        const std::size_t entity =
            item.kind == LevelItem::Kind::Entity ? item.index : level.brushes[item.index].entity;
        if (entity < whole.size()) {
            whole[entity] = true;
        }
    }
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        if (level.brushes[i].entity < whole.size() && whole[level.brushes[i].entity]) {
            result.push_back(LevelItem::brush(i));
        }
    }
    for (std::size_t i = 0; i < whole.size(); ++i) {
        if (whole[i]) {
            result.push_back(LevelItem::entity(i));
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::uint32_t itemGroup(const Level& level, LevelItem item) {
    if (!exists(level, item)) {
        return kNoGroup;
    }
    if (item.kind == LevelItem::Kind::Entity) {
        return level.entities[item.index].group;
    }
    const Brush& brush = level.brushes[item.index];
    return brush.entity < level.entities.size() ? level.entities[brush.entity].group : brush.group;
}

std::vector<LevelItem> groupMembers(const Level& level, std::uint32_t group) {
    std::vector<LevelItem> members;
    if (group == kNoGroup) {
        return members;
    }
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        if (itemGroup(level, LevelItem::brush(i)) == group) {
            members.push_back(LevelItem::brush(i));
        }
    }
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        if (level.entities[i].group == group) {
            members.push_back(LevelItem::entity(i));
        }
    }
    return members;
}

std::vector<LevelItem> withWholeGroups(const Level& level, std::span<const LevelItem> items) {
    std::vector<bool> whole(level.groups.size(), false);
    for (const LevelItem& item : items) {
        const std::uint32_t group = itemGroup(level, item);
        if (group < whole.size()) {
            whole[group] = true;
        }
    }
    std::vector<LevelItem> result(items.begin(), items.end());
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        const std::uint32_t group = itemGroup(level, LevelItem::brush(i));
        if (group < whole.size() && whole[group]) {
            result.push_back(LevelItem::brush(i));
        }
    }
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        if (level.entities[i].group < whole.size() && whole[level.entities[i].group]) {
            result.push_back(LevelItem::entity(i));
        }
    }
    return withWholeEntities(level, result);
}

Vec3 BodySizes::halfExtents(const LevelEntity& entity) const {
    if ((entity.className != kEnemyClass && entity.className != kSpawnerClass) || enemies.empty()) {
        return player;
    }
    const std::string* type = findProperty(entity, "type");
    Vec3 half = player;
    if (type == nullptr || type->empty()) {
        half = enemies.front().second;
    } else {
        const auto found = std::find_if(enemies.begin(), enemies.end(),
                                        [&](const auto& kind) { return kind.first == *type; });
        if (found == enemies.end()) {
            return player;
        }
        half = found->second;
    }
    // Its own size, if it has one, as the game reads it (GameLevel).
    const auto own = [&](std::string_view key, float& halfSize) {
        const std::string* text = findProperty(entity, key);
        const std::optional<std::uint32_t> setting = findEnemySetting(key);
        float value = 0.0f;
        if (text != nullptr && setting && parseFloat(*text, value)) {
            const EnemySetting& info = enemySettings()[*setting];
            halfSize = std::clamp(value, info.minimum, info.maximum) * 0.5f;
        }
    };
    own("width", half.x);
    own("width", half.y);
    own("height", half.z);
    return half;
}

std::optional<Aabb> entityMarker(const LevelEntity& entity, const BodySizes& bodies) {
    const std::optional<Vec3> origin = entityOrigin(entity);
    if (!origin) {
        return std::nullopt;
    }
    if (entity.className == kPlayerSpawnClass || entity.className == kEnemyClass ||
        entity.className == kSpawnerClass) {
        const Vec3 half = bodies.halfExtents(entity);
        return Aabb{*origin - Vec3{half.x, half.y, 0.0f},
                    *origin + Vec3{half.x, half.y, half.z * 2.0f}};
    }
    if (entity.className == kPickupClass) {
        // The box that takes it, resting on its origin.
        const Vec3 half = kPickupHalfExtents;
        return Aabb{*origin - Vec3{half.x, half.y, 0.0f},
                    *origin + Vec3{half.x, half.y, half.z * 2.0f}};
    }
    const Vec3 half{kEntityMarkerHalfSize, kEntityMarkerHalfSize, kEntityMarkerHalfSize};
    return Aabb{*origin - half, *origin + half};
}

std::optional<Vec3> entityFacing(const LevelEntity& entity) {
    std::string_view text;
    if (const std::string* yaw = findProperty(entity, "yaw")) {
        text = *yaw;
    } else if (const EntityClassInfo* entityClass = findEntityClass(entity.className)) {
        const PropertyInfo* property = findPropertyInfo(*entityClass, "yaw");
        if (property == nullptr) {
            return std::nullopt;
        }
        text = property->defaultValue;
    } else {
        return std::nullopt;
    }
    float degrees = 0.0f;
    if (!parseFloat(text, degrees) || !std::isfinite(degrees)) {
        return std::nullopt;
    }
    const double radians = static_cast<double>(degrees) * (std::numbers::pi / 180.0);
    return Vec3{static_cast<float>(std::cos(radians)), static_cast<float>(std::sin(radians)), 0.0f};
}

std::optional<Aabb> itemBounds(const Level& level, LevelItem item, const BodySizes& bodies) {
    if (!exists(level, item)) {
        return std::nullopt;
    }
    if (item.kind == LevelItem::Kind::Entity) {
        return entityMarker(level.entities[item.index], bodies);
    }
    Aabb bounds;
    if (!computeBrushBounds(level.brushes[item.index], bounds)) {
        return std::nullopt;
    }
    return bounds;
}

std::optional<Aabb> itemBounds(const Level& level, std::span<const LevelItem> items,
                               const BodySizes& bodies) {
    std::optional<Aabb> total;
    for (const LevelItem& item : items) {
        if (const std::optional<Aabb> bounds = itemBounds(level, item, bodies)) {
            total =
                total ? Aabb{min(total->min, bounds->min), max(total->max, bounds->max)} : *bounds;
        }
    }
    return total;
}

} // namespace ct
