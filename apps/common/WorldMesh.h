#pragma once

#include "croftech/level/Level.h"
#include "croftech/render/Renderer.h"

#include <cstdint>
#include <vector>

namespace ct {

struct WorldMesh {
    std::vector<WorldVertex> vertices;
    std::vector<std::uint32_t> indices;
    // For play, each mover's brushes are a part of their own, after everything that never moves.
    std::vector<WorldMeshPart> parts;
    std::vector<std::uint32_t> partEntities; // the level entity each part is
};

// Editing shows every brush where it was built. Playing leaves out the brushes players never
// see, such as triggers, and makes a part of each mover's.
enum class MeshPurpose : std::uint8_t { Editing, Playing };

// Triangulates every brush face, with a flat colour per brush so shapes are easy to tell apart, and
// a colour per class for the brushes of movers and triggers.
WorldMesh buildWorldMesh(const Level& level, MeshPurpose purpose);

} // namespace ct
