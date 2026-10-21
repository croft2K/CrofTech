#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/BrushGeometry.h"
#include "croftech/level/Level.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace ct {

// Cutting and combining brushes. All of them build in double precision from exact face planes, and
// corners that survive keep their exact values, so work on the grid stays on it.

// The parts of a brush behind and in front of a plane. A side the brush doesn't reach is empty.
struct BrushSplit {
    std::optional<Brush> back;
    std::optional<Brush> front;
};

// Nullopt if a part that should exist could not be built (it would be thinner than a hair).
std::optional<BrushSplit> splitBrush(const Brush& brush, const DPlane& plane);

// The brush with the cutter's volume taken out, as convex pieces that don't overlap: at most one
// per face of the cutter. The brush itself if they don't overlap; empty if the cutter covers it.
// Nullopt if a piece could not be built.
std::optional<std::vector<Brush>> subtractBrush(const Brush& brush, const Brush& cutter);

// Walls thickness thick inside the brush's faces, leaving it empty inside: the brush minus a copy
// with every face moved in by thickness. Nullopt if the brush is too thin for that.
std::optional<std::vector<Brush>> hollowBrush(const Brush& brush, float thickness);

// One brush around all the corners of several: their convex hull.
std::optional<Brush> mergeBrushes(std::span<const Brush> brushes);

// A new brush made by pushing a copy of a face out along its normal by distance (positive): what
// pulling a face out of a brush makes. Exact for a face on the grid moved in grid steps along an
// axis.
std::optional<Brush> extrudeFace(const Brush& brush, std::size_t face, float distance);

} // namespace ct
