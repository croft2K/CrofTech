#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <array>
#include <cstddef>
#include <optional>

namespace ct {

// Grid sizes in metres, smallest first; the editor steps through them.
inline constexpr std::array<float, 9> kGridSizes{1.0f / 32.0f, 1.0f / 16.0f, 0.125f, 0.25f, 0.5f,
                                                 1.0f,         2.0f,         4.0f,   8.0f};

float snapToGrid(float value, float grid);
Vec3 snapToGrid(Vec3 value, float grid);

// Moves every corner by the offset. Exact for grid-sized offsets of corners on the grid.
Brush translatedBrush(const Brush& brush, Vec3 offset);

// The brush with one face pushed along its normal until its plane is distance from the origin (as
// in facePlane), the other faces keeping their planes: how faces are dragged. Corners off the
// moved face keep their exact values. Nullopt if the face would vanish or the result is not a
// valid brush; otherwise movedFace (if given) receives the face's index in the result.
std::optional<Brush> withFacePushed(const Brush& brush, std::size_t face, float distance,
                                    std::size_t* movedFace = nullptr);

// The box if the brush is exactly six axis-aligned faces.
std::optional<Aabb> boxBounds(const Brush& brush);

// True if the normal is one of the six axis directions.
bool isAxial(Vec3 normal);

struct BrushHit {
    float distance = 0.0f;
    std::size_t face = 0; // the face the ray enters through
};

// Rays start at origin and run along a unit direction. A ray starting inside the solid misses,
// so the camera can sit inside a brush and still pick what it can see.
std::optional<BrushHit> rayCastBrush(const Brush& brush, Vec3 origin, Vec3 direction);
std::optional<float> rayCastBox(const Aabb& box, Vec3 origin, Vec3 direction);
std::optional<float> rayCastPlane(const Plane& plane, Vec3 origin, Vec3 direction);

// How far along the axis line (through axisOrigin, unit axis) the point nearest the ray lies.
// Dragging a face or moving vertically follows the mouse this way. Nullopt when the ray runs
// parallel to the axis.
std::optional<float> closestAlongAxis(Vec3 axisOrigin, Vec3 axis, Vec3 rayOrigin,
                                      Vec3 rayDirection);

} // namespace ct
