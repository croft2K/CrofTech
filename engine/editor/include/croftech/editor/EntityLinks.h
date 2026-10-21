#pragma once

#include "croftech/core/Math.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/level/Level.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace ct {

// What the view draws to show how entities move and connect: a mover's end position, and wires
// from outputs to the entities they target.

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

} // namespace ct
