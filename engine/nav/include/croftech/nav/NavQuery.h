#pragma once

#include "croftech/core/Math.h"
#include "croftech/nav/NavMesh.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

class dtNavMeshQuery;

namespace ct {

inline constexpr std::size_t kMaxNavPathCorners = 64;

// A point where a path turns, or where it moves into a different area.
struct NavCorner {
    Vec3 position; // on the mesh, at the agent's feet
    // What the path goes through from here to the next corner: a lift's link starts at the
    // corner where the ride begins.
    NavArea area = NavArea::Ground;
    std::uint32_t mover = kNoNavMover;
    // Where a link starts: the ride of a lift begins here. Corners where a path only steps onto a
    // lift's top have its area and mover too, but aren't links.
    bool link = false;
};

// The corners from start to goal, both included. Held in fixed storage, so finding one allocates
// nothing.
struct NavPath {
    std::array<NavCorner, kMaxNavPathCorners> corners{};
    std::uint32_t count = 0;
    // False when the goal can't be reached: the path then ends at the closest point to it (or is
    // empty when the start is off the mesh). Long paths are cut at kMaxNavPathCorners corners and
    // count as incomplete too.
    bool complete = false;

    std::span<const NavCorner> view() const { return {corners.data(), count}; }
};

// Which parts of the mesh a path may use.
struct NavFilter {
    // Doors and lifts that won't open or come (locked, or switched off), by the ids given to
    // NavInput; sorted ascending.
    std::span<const std::uint32_t> blockedMovers;
    bool avoidHazards = true;
};

// Finds points and paths on a mesh. It keeps working memory between queries, so each thread
// needs its own; results depend only on the mesh and the arguments.
class NavQuery {
public:
    // How far from a point the mesh is looked for, as half extents (x, y, and z up).
    static constexpr Vec3 kDefaultReach{1.0f, 1.0f, 2.0f};

    explicit NavQuery(const NavMesh& mesh);
    ~NavQuery();
    NavQuery(const NavQuery&) = delete;
    NavQuery& operator=(const NavQuery&) = delete;

    // The closest point of the mesh to position within reach, if there is one.
    std::optional<Vec3> nearestPoint(Vec3 position, const NavFilter& filter = {},
                                     Vec3 reach = kDefaultReach) const;
    // From one point to another (each moved to the closest point of the mesh within reach of it).
    // Returns path.complete.
    bool findPath(Vec3 start, Vec3 goal, const NavFilter& filter, NavPath& path,
                  Vec3 reach = kDefaultReach) const;

private:
    struct QueryDeleter {
        void operator()(dtNavMeshQuery* query) const;
    };

    const NavMesh& m_mesh;
    std::unique_ptr<dtNavMeshQuery, QueryDeleter> m_query;
};

} // namespace ct
