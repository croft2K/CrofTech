#pragma once

#include "croftech/core/Math.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/level/Level.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace ct {

// What the view draws to show how entities move and connect: a mover's end position, wires from
// outputs to the entities they target, and the entities that properties name (patrol routes).

// The entity, when the selection is one entity and nothing but its own brushes: what the inspector
// shows and the end handle and wires belong to.
std::optional<std::size_t> selectedEntity(const EditorDocument& document);

// Where an entity is drawn from: its origin, or the middle of its brushes.
std::optional<Vec3> entityAnchor(const Level& level, std::size_t entity);

// A mover's brushes where they are built (bounds) and the offset they move by.
struct MoverPath {
    std::size_t entity = 0;
    Aabb bounds;
    Vec3 move;

    Vec3 start() const { return (bounds.min + bounds.max) * 0.5f; }
    Vec3 end() const { return start() + move; }
};

// The path of an entity whose class has a "move" property, if it has brushes.
std::optional<MoverPath> moverPath(const Level& level, std::size_t entity);

// Sets a mover's "move" as one undoable command. False if it isn't a mover or nothing changes.
bool setMoverEnd(EditorDocument& document, std::size_t entity, Vec3 move);

// One output of one entity, to one entity with its target name.
struct Wire {
    std::size_t from = 0;
    std::size_t to = 0;
    std::size_t output = 0; // in the from entity's outputs
};

// The wires out of the entity (each output to each entity with its target name), then the wires
// into it from other entities. An output whose target matches no entity has no wire.
std::vector<Wire> entityWires(const Level& level, std::size_t entity);

// Every wire in the level.
std::vector<Wire> allWires(const Level& level);

// A property of one entity that names another (EntityClasses' Reference): an enemy's first patrol
// point, or a patrol point's next.
struct Reference {
    std::size_t from = 0;
    std::size_t to = 0;
    std::string_view key; // the property's key
};

// The references along the chain from the entity (what it names, what that names, and so on: a
// whole patrol route), then those into it from entities off the chain.
std::vector<Reference> entityReferences(const Level& level, std::size_t entity);

// Every reference in the level.
std::vector<Reference> allReferences(const Level& level);

// How far on from an enemy or patrol point addPatrolPoint puts the new point.
inline constexpr float kPatrolPointStep = 4.0f;

// Adds a patrol point after an enemy or patrol point and links it in: the new point names what
// the entity named (the enemy's patrol, the point's next) and the entity names it, so it can be
// added in the middle of a route, or before a route goes round again. It goes kPatrolPointStep
// on, the way the enemy faces or the way the route was going, on the grid, and is named after the
// route ("guard_3" after "guard_2" or the enemy "guard"). One command; selects the new point.
// False if the entity is neither, or the level is full.
bool addPatrolPoint(EditorDocument& document, std::size_t entity, float grid);

} // namespace ct
