#pragma once

#include "croftech/level/Level.h"
#include "croftech/render/Renderer.h"

#include <cstdint>
#include <vector>

namespace ct {

struct WorldMesh {
    std::vector<WorldVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Editing shows every brush; playing leaves out the ones players never see, such as triggers.
enum class MeshPurpose : std::uint8_t { Editing, Playing };

// Triangulates every brush face, with a flat colour per brush so shapes are easy to tell apart, and
// a colour per class for the brushes of movers and triggers.
WorldMesh buildWorldMesh(const Level& level, MeshPurpose purpose);

} // namespace ct
