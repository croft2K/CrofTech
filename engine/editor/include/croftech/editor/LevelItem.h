#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ct {

// A brush or entity of a level, by its index in the level's list.
struct LevelItem {
    enum class Kind : std::uint8_t { Brush, Entity };

    Kind kind = Kind::Brush;
    std::size_t index = 0;

    static LevelItem brush(std::size_t index) { return {Kind::Brush, index}; }
    static LevelItem entity(std::size_t index) { return {Kind::Entity, index}; }

    // Brushes before entities, each in level order.
    friend auto operator<=>(const LevelItem&, const LevelItem&) = default;
};

bool exists(const Level& level, LevelItem item);

// The box drawn and picked for an entity: a player-sized box standing on a spawn point, a small
// cube for anything else. Nullopt if the entity has no readable origin.
std::optional<Aabb> entityMarker(const LevelEntity& entity, Vec3 playerHalfExtents);

// The box around a brush's corners or an entity's marker.
std::optional<Aabb> itemBounds(const Level& level, LevelItem item, Vec3 playerHalfExtents);
std::optional<Aabb> itemBounds(const Level& level, std::span<const LevelItem> items,
                               Vec3 playerHalfExtents);

} // namespace ct
