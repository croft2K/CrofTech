#include "croftech/editor/LevelItem.h"

#include "croftech/level/BrushGeometry.h"

namespace ct {
namespace {

constexpr float kEntityMarkerHalfSize = 0.25f;

} // namespace

bool exists(const Level& level, LevelItem item) {
    return item.kind == LevelItem::Kind::Brush ? item.index < level.brushes.size()
                                               : item.index < level.entities.size();
}

std::optional<Aabb> entityMarker(const LevelEntity& entity, Vec3 playerHalfExtents) {
    const std::optional<Vec3> origin = entityOrigin(entity);
    if (!origin) {
        return std::nullopt;
    }
    if (entity.className == kPlayerSpawnClass) {
        const Vec3 half = playerHalfExtents;
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
