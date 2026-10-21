#pragma once

#include "croftech/core/DVec3.h"
#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace ct {

// Points this close to a plane (metres) count as on it, and corners this close together are one:
// far above the error of double arithmetic on float corners, far below the editor's smallest grid
// (1/32 m).
inline constexpr double kOnPlaneDistance = 1e-4;

// A plane in double precision: the points p with dot(normal, p) == distance, the normal pointing
// out of the solid. The normal needn't be unit length. The plane through three grid points has
// their cross product as an exact normal, which keeps every test of a grid point against it
// exact; normalizing would round it.
struct DPlane {
    DVec3 normal;
    double distance = 0.0;

    // In metres, positive in front. The normal must not be zero.
    double distanceTo(DVec3 point) const;
    DPlane flipped() const { return {-normal, -distance}; }
};

DPlane toDPlane(const Plane& plane);
// With a unit normal, rounded to float. The normal must not be zero.
Plane toPlane(const DPlane& plane);
// The plane through three points, facing the side from which they run counter-clockwise.
DPlane planeThroughPoints(DVec3 a, DVec3 b, DVec3 c);

// The plane of a face, from its corners: exact for a face whose corners are on the grid.
DPlane exactFacePlane(const Brush& brush, std::size_t face);
Plane facePlane(const Brush& brush, std::size_t face);
// One per face, in face order: what collision and ray casts test against.
std::vector<Plane> brushPlanes(const Brush& brush);

std::vector<Vec3> faceVertices(const Brush& brush, std::size_t face);
Vec3 faceCentre(const Brush& brush, std::size_t face);

// A face as a polygon: what rendering and outlines draw.
struct BrushPolygon {
    Plane plane;
    std::vector<Vec3> vertices; // counter-clockwise seen from outside
};

std::vector<BrushPolygon> brushPolygons(const Brush& brush);

// False if the brush has no corners.
bool computeBrushBounds(const Brush& brush, Aabb& bounds);

// Ways to build brushes. Each works in double precision and rounds to float once, and each returns
// nullopt unless the result passes checkBrush.

// The solid behind every plane: how brushes made of planes (.map files, old level files, faces
// pushed along their normal) become corners. Faces come out in the order of the planes that make
// them; planes that don't touch the solid make none, and planeOfFace (if given) receives the
// plane index of each face. A corner that lands within a hair of one of exactCorners takes its
// value exactly, so an edit that keeps most planes keeps their corners bit for bit.
std::optional<Brush> brushFromPlanes(std::span<const DPlane> planes,
                                     std::span<const Vec3> exactCorners = {},
                                     std::vector<std::size_t>* planeOfFace = nullptr);

// The convex hull of the points: what corner editing, merging and the shape tools build. Points
// inside the hull or on its faces or edges are not corners of the result; neighbouring faces in
// the same plane become one.
std::optional<Brush> brushFromPoints(std::span<const Vec3> points);

// The part of the brush behind the plane, with the cut face last; the brush unchanged if the plane
// misses it. Nullopt if nothing is left behind the plane.
std::optional<Brush> clipBrush(const Brush& brush, const DPlane& plane);

} // namespace ct
