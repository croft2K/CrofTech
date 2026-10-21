#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

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

// The items plus every entity that one of their brushes belongs to and every brush of every such
// entity, so a door is picked and edited as one thing. Sorted, with no repeats.
std::vector<LevelItem> withWholeEntities(const Level& level, std::span<const LevelItem> items);

// The group the item is in (a brush of an entity is in its entity's group), or kNoGroup.
std::uint32_t itemGroup(const Level& level, LevelItem item);

// The world brushes and entities of the group, with the brushes of those entities. Sorted.
std::vector<LevelItem> groupMembers(const Level& level, std::uint32_t group);

// withWholeEntities, plus every member of each group one of the items is in, so a click in the
// view picks a whole group as it does a whole door. Sorted, with no repeats.
std::vector<LevelItem> withWholeGroups(const Level& level, std::span<const LevelItem> items);

// The box drawn and picked for an entity: a player-sized box standing on a spawn point, a small
// cube for anything else. Nullopt if the entity has no readable origin.
std::optional<Aabb> entityMarker(const LevelEntity& entity, Vec3 playerHalfExtents);

// The box around a brush's corners or an entity's marker.
std::optional<Aabb> itemBounds(const Level& level, LevelItem item, Vec3 playerHalfExtents);
std::optional<Aabb> itemBounds(const Level& level, std::span<const LevelItem> items,
                               Vec3 playerHalfExtents);

} // namespace ct
