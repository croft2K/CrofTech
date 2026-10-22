#include "croftech/editor/LevelItem.h"

#include "croftech/level/BrushGeometry.h"
#include "croftech/level/EntityClasses.h"

#include <algorithm>

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

std::optional<Aabb> entityMarker(const LevelEntity& entity, Vec3 playerHalfExtents) {
    const std::optional<Vec3> origin = entityOrigin(entity);
    if (!origin) {
        return std::nullopt;
    }
    // Enemies are drawn the player's size until markers know each kind's.
    if (entity.className == kPlayerSpawnClass || entity.className == kEnemyClass) {
        const Vec3 half = playerHalfExtents;
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

std::optional<Aabb> itemBounds(const Level& level, LevelItem item, Vec3 playerHalfExtents) {
    if (!exists(level, item)) {
        return std::nullopt;
    }
    if (item.kind == LevelItem::Kind::Entity) {
        return entityMarker(level.entities[item.index], playerHalfExtents);
    }
    Aabb bounds;
    if (!computeBrushBounds(level.brushes[item.index], bounds)) {
        return std::nullopt;
    }
    return bounds;
}

std::optional<Aabb> itemBounds(const Level& level, std::span<const LevelItem> items,
                               Vec3 playerHalfExtents) {
    std::optional<Aabb> total;
    for (const LevelItem& item : items) {
        if (const std::optional<Aabb> bounds = itemBounds(level, item, playerHalfExtents)) {
            total =
                total ? Aabb{min(total->min, bounds->min), max(total->max, bounds->max)} : *bounds;
        }
    }
    return total;
}

} // namespace ct
