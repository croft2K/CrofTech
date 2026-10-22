#pragma once

#include "croftech/core/Math.h"
#include "croftech/level/Level.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

class dtNavMesh;

namespace ct {

// The size of what walks a mesh, and the steps and slopes it can take. One mesh serves one size.
struct NavAgent {
    float radius = 0.4f; // half the width of its box
    float height = 1.8f;
    float climb = 0.457f;          // tallest step up or down
    float minGroundNormalZ = 0.7f; // steeper surfaces aren't walked on
};

// What a polygon or link of the mesh is. Paths give it at each corner, so whatever follows one
// knows when it reaches a door or a lift.
enum class NavArea : std::uint8_t {
    Ground,
    Door,   // where a door stands when shut, widened by the agent's radius
    Lift,   // the top of a lift or platform at either stop, and the ride between them
    Hazard, // where the agent would touch a hurt volume: lava, slime, kill pits
};

// How a mover takes part in the mesh.
enum class NavMoverKind : std::uint8_t {
    Door,  // built as open; where it stands shut is a Door area
    Lift,  // its top is walkable at both stops, with a link between them for the ride
    Solid, // an obstacle where it is built
};

inline constexpr std::uint32_t kNoNavMover = UINT32_MAX;

struct NavMover {
    std::uint32_t id = kNoNavMover; // given back with its areas and link: the level entity index
    NavMoverKind kind = NavMoverKind::Door;
    std::vector<Brush> brushes; // where it is built, which is its start
    Vec3 move;                  // from the start to the end
};

// What a mesh is built from.
struct NavInput {
    std::vector<Brush> solid;   // world geometry that never moves
    std::vector<Brush> hazards; // volumes to keep out of
    std::vector<NavMover> movers;
};

// For drawing the mesh: a triangle of its surface, with a number for the polygon it is part of.
struct NavTriangle {
    std::array<Vec3, 3> corners;
    NavArea area = NavArea::Ground;
    std::uint32_t polygon = 0;
};

// An edge of a polygon of the mesh: between two polygons, or a boundary with nothing beyond it
// (a wall, or a drop).
struct NavEdge {
    Vec3 start;
    Vec3 end;
    bool boundary = false;
};

// A way between two parts of the mesh that isn't walked: the ride of a lift. Both ways.
struct NavLink {
    Vec3 start;
    Vec3 end;
    std::uint32_t mover = kNoNavMover;
};

// A navigation mesh: the floors an agent can walk, as convex polygons in tiles, with links for
// lifts. Built with Recast from brushes; NavQuery finds paths on it with Detour. Built once and
// never changed, so any number of threads may read it (each with its own NavQuery). Building is
// deterministic: the same input and agent give the same mesh.
class NavMesh {
public:
    NavMesh(const NavInput& input, const NavAgent& agent);
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    const NavAgent& agent() const { return m_agent; }
    std::size_t polygonCount() const { return m_polygonCount; }
    // Problems found while building, such as a part of the level too tall to build in one go.
    std::span<const std::string> warnings() const { return m_warnings; }

    // The mesh's surface, edges and links, for drawing.
    std::vector<NavTriangle> triangles() const;
    // Each edge once, shared or not.
    std::vector<NavEdge> edges() const;
    std::vector<NavLink> links() const;

    // The door or lift a polygon or link belongs to, or kNoNavMover. Detour's polygon reference
    // is a std::uint64_t (64-bit references).
    std::uint32_t moverOf(std::uint64_t polygon) const;
    const dtNavMesh& detour() const { return *m_mesh; }

private:
    struct MeshDeleter {
        void operator()(dtNavMesh* mesh) const;
    };

    NavAgent m_agent;
    std::unique_ptr<dtNavMesh, MeshDeleter> m_mesh;
    // The mover of each polygon, by Detour's tile index and then polygon index.
    std::vector<std::vector<std::uint32_t>> m_polygonMovers;
    std::size_t m_polygonCount = 0;
    std::vector<std::string> m_warnings;
};

} // namespace ct
