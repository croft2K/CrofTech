#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ct {

// The sizes of the bodies that spawns and enemies stand for, so their markers show what the game
// uses: the player's box, and each kind of enemy's in the order of the game's list.
struct BodySizes {
    Vec3 player{0.4f, 0.4f, 0.9f};                     // half extents
    std::vector<std::pair<std::string, Vec3>> enemies; // name and half extents

    // An enemy's by its type: the first kind's when it names none, the player's when the game
    // has no such kind (or no enemies). The player's for anything else.
    Vec3 halfExtents(const LevelEntity& entity) const;
};

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

// The box drawn and picked for an entity: the body standing on a spawn point or an enemy's
// origin, the box that takes a pickup, a small cube for anything else. Nullopt if the entity has
// no readable origin.
std::optional<Aabb> entityMarker(const LevelEntity& entity, const BodySizes& bodies);

// The way an entity faces across the ground, as a unit vector, for those with a "yaw" (player
// spawns, enemies): its own value, or its class's default. Nullopt for entities without one.
std::optional<Vec3> entityFacing(const LevelEntity& entity);

// The box around a brush's corners or an entity's marker.
std::optional<Aabb> itemBounds(const Level& level, LevelItem item, const BodySizes& bodies);
std::optional<Aabb> itemBounds(const Level& level, std::span<const LevelItem> items,
                               const BodySizes& bodies);

} // namespace ct
