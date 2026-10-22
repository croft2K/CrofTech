#include "croftech/nav/NavMesh.h"

#include "NavDetour.h"

#include "croftech/level/BrushGeometry.h"

#include <DetourAlloc.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <utility>

namespace ct {
namespace {

using nav::fromDetour;
using nav::toDetour;

// Cells are a third of the agent's radius across, so walls are found to within a few centimetres;
// 5 cm high, so the 18 inch step of the default movement (0.457 m) is measured as 0.45 m. A tile
// is 64 cells square, and is built with a border of cells from its neighbours so that their
// edges meet.
constexpr int kRadiusCells = 3;
constexpr float kCellHeight = 0.05f;
constexpr int kTileCells = 64;
constexpr int kBorderCells = kRadiusCells + 3;
// Polygon edges along open floor are split at 12 agent radii, so paths across big rooms turn
// close to where they should.
constexpr float kMaxEdgeRadii = 12.0f;
constexpr float kMaxSimplificationError = 1.3f; // cells
// Floor patches smaller than this are dropped (tops of thin walls, ledges no one reaches); regions
// smaller than the second are merged into their neighbours.
constexpr int kMinRegionCells = 8 * 8;
constexpr int kMergeRegionCells = 20 * 20;
constexpr int kDetailSampleCells = 6;

// Recast area ids while building. Mover marks cycle through the ids after the first two, so doors
// and lifts next to each other get different ids, and so polygons of their own; each polygon is
// then matched to its mover and given its NavArea.
constexpr unsigned char kBuildGround = 1;
constexpr unsigned char kBuildHazard = 2;
constexpr unsigned char kFirstMoverArea = 3;
constexpr unsigned kMoverAreaCount = RC_WALKABLE_AREA - kFirstMoverArea;

constexpr std::uint32_t kNoIndex = std::numeric_limits<std::uint32_t>::max();

struct Triangles {
    std::vector<float> vertices; // Detour coordinates
    std::vector<int> indices;
    std::vector<unsigned char> areas;
};

void addBrush(Triangles& triangles, const Brush& brush, Vec3 offset, float minGroundNormalZ) {
    for (std::size_t face = 0; face < brush.faces.size(); ++face) {
        const std::vector<std::uint32_t>& corners = brush.faces[face].corners;
        if (corners.size() < 3 ||
            std::any_of(corners.begin(), corners.end(),
                        [&](std::uint32_t corner) { return corner >= brush.vertices.size(); })) {
            continue;
        }
        const unsigned char area =
            facePlane(brush, face).normal.z >= minGroundNormalZ ? kBuildGround : RC_NULL_AREA;
        const int first = static_cast<int>(triangles.vertices.size() / 3);
        for (std::uint32_t corner : corners) {
            const std::array<float, 3> v = toDetour(brush.vertices[corner] + offset);
            triangles.vertices.insert(triangles.vertices.end(), v.begin(), v.end());
        }
        for (int i = 1; i + 1 < static_cast<int>(corners.size()); ++i) {
            triangles.indices.insert(triangles.indices.end(), {first, first + i, first + i + 1});
            triangles.areas.push_back(area);
        }
    }
}

// The convex hull of points on the ground plane, counter-clockwise.
std::vector<std::array<float, 2>> hull2d(std::vector<std::array<float, 2>> points) {
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3) {
        return points;
    }
    const auto turn = [](const std::array<float, 2>& o, const std::array<float, 2>& a,
                         const std::array<float, 2>& b) {
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
    };
    std::vector<std::array<float, 2>> hull(points.size() * 2);
    std::size_t count = 0;
    for (const std::array<float, 2>& point : points) {
        while (count >= 2 && turn(hull[count - 2], hull[count - 1], point) <= 0.0f) {
            --count;
        }
        hull[count++] = point;
    }
    const std::size_t lower = count + 1;
    for (std::size_t i = points.size() - 1; i-- > 0;) {
        while (count >= lower && turn(hull[count - 2], hull[count - 1], points[i]) <= 0.0f) {
            --count;
        }
        hull[count++] = points[i];
    }
    hull.resize(count - 1);
    return hull;
}

// A volume whose floors get an area of their own: where an agent would touch a hurt volume or a
// shut door, or the top of a lift at one of its stops.
struct Mark {
    std::vector<float> polygon; // Detour coordinates; its height is unused
    float minHeight = 0.0f;     // floors this high to maxHeight are marked
    float maxHeight = 0.0f;
    unsigned char area = kBuildGround;
    std::uint32_t mover = kNoIndex;                           // index into NavInput::movers
    float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f; // Detour axes
};

// The brush's outline from above, widened by a square of half-width grow (agents are boxes),
// with the heights of floors it covers.
std::optional<Mark> markBrush(const Brush& brush, Vec3 offset, float grow, float minHeight,
                              float maxHeight, unsigned char area, std::uint32_t mover) {
    std::vector<std::array<float, 2>> points;
    for (const Vec3& corner : brush.vertices) {
        const Vec3 p = corner + offset;
        if (grow > 0.0f) {
            for (float dx : {-grow, grow}) {
                for (float dy : {-grow, grow}) {
                    points.push_back({p.x + dx, p.y + dy});
                }
            }
        } else {
            points.push_back({p.x, p.y});
        }
    }
    const std::vector<std::array<float, 2>> outline = hull2d(std::move(points));
    if (outline.size() < 3) {
        return std::nullopt;
    }
    Mark mark;
    mark.minHeight = minHeight;
    mark.maxHeight = maxHeight;
    mark.area = area;
    mark.mover = mover;
    mark.minX = mark.maxX = outline[0][0];
    mark.minZ = mark.maxZ = -outline[0][1];
    for (const std::array<float, 2>& point : outline) {
        const std::array<float, 3> v = toDetour({point[0], point[1], 0.0f});
        mark.polygon.insert(mark.polygon.end(), v.begin(), v.end());
        mark.minX = std::min(mark.minX, v[0]);
        mark.maxX = std::max(mark.maxX, v[0]);
        mark.minZ = std::min(mark.minZ, v[2]);
        mark.maxZ = std::max(mark.maxZ, v[2]);
    }
    return mark;
}

float brushTop(const Brush& brush) {
    float top = -std::numeric_limits<float>::max();
    for (const Vec3& corner : brush.vertices) {
        top = std::max(top, corner.z);
    }
    return top;
}

// The ride of a lift, from the middle of its top at the start to the same place at the end.
struct Link {
    std::array<float, 6> ends; // Detour coordinates
    std::uint32_t mover = kNoIndex;
};

std::optional<Link> liftLink(const NavMover& mover) {
    Aabb bounds;
    bool any = false;
    for (const Brush& brush : mover.brushes) {
        Aabb brushBounds;
        if (computeBrushBounds(brush, brushBounds)) {
            bounds = any ? Aabb{min(bounds.min, brushBounds.min), max(bounds.max, brushBounds.max)}
                         : brushBounds;
            any = true;
        }
    }
    if (!any) {
        return std::nullopt;
    }
    const Vec3 start{(bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f,
                     bounds.max.z};
    const std::array<float, 3> a = toDetour(start);
    const std::array<float, 3> b = toDetour(start + mover.move);
    return Link{{a[0], a[1], a[2], b[0], b[1], b[2]}, kNoIndex};
}

struct RecastDeleter {
    void operator()(rcHeightfield* p) const { rcFreeHeightField(p); }
    void operator()(rcCompactHeightfield* p) const { rcFreeCompactHeightfield(p); }
    void operator()(rcContourSet* p) const { rcFreeContourSet(p); }
    void operator()(rcPolyMesh* p) const { rcFreePolyMesh(p); }
    void operator()(rcPolyMeshDetail* p) const { rcFreePolyMeshDetail(p); }
};
template <typename T>
using RecastPtr = std::unique_ptr<T, RecastDeleter>;

struct DetourFree {
    void operator()(unsigned char* data) const { dtFree(data); }
};

struct BuiltTile {
    std::unique_ptr<unsigned char, DetourFree> data;
    int size = 0;
    std::vector<std::uint32_t> movers; // of each polygon, as NavMover ids
};

// Everything the tiles are built from, gathered once.
class Builder {
public:
    Builder(const NavInput& input, const NavAgent& agent, std::vector<std::string>& warnings)
        : m_input(input), m_agent(agent), m_warnings(warnings),
          m_cellSize(agent.radius / static_cast<float>(kRadiusCells)) {
        gather();
    }

    bool empty() const { return m_triangles.areas.empty(); }
    float cellSize() const { return m_cellSize; }
    const float* origin() const { return m_min.data(); }
    int tilesX() const { return m_tilesX; }
    int tilesY() const { return m_tilesY; }
    std::optional<BuiltTile> buildTile(int tileX, int tileY);

private:
    void gather();
    void binTriangles();
    rcConfig tileConfig(int tileX, int tileY, std::span<const int> triangles) const;
    std::uint32_t moverOfPolygon(const rcPolyMesh& mesh, int polygon) const;

    const NavInput& m_input;
    const NavAgent& m_agent;
    std::vector<std::string>& m_warnings;
    float m_cellSize;
    Triangles m_triangles;
    std::vector<Mark> m_marks;
    std::vector<Link> m_links;
    std::array<float, 3> m_min{};
    std::array<float, 3> m_max{};
    int m_tilesX = 0;
    int m_tilesY = 0;
    // The triangles that reach each tile or its border.
    std::vector<std::vector<int>> m_bins;
    rcContext m_context{false};
};

void Builder::gather() {
    const float grow = m_agent.radius;
    for (const Brush& brush : m_input.solid) {
        addBrush(m_triangles, brush, {}, m_agent.minGroundNormalZ);
    }
    for (const Brush& brush : m_input.hazards) {
        Aabb bounds;
        if (computeBrushBounds(brush, bounds)) {
            if (std::optional<Mark> mark = markBrush(brush, {}, grow, bounds.min.z - m_agent.height,
                                                     bounds.max.z, kBuildHazard, kNoIndex)) {
                m_marks.push_back(std::move(*mark));
            }
        }
    }
    for (std::uint32_t i = 0; i < m_input.movers.size(); ++i) {
        const NavMover& mover = m_input.movers[i];
        const auto area = static_cast<unsigned char>(kFirstMoverArea + i % kMoverAreaCount);
        const bool moves = dot(mover.move, mover.move) > 0.0f;
        if (mover.kind == NavMoverKind::Solid || (mover.kind == NavMoverKind::Lift && !moves)) {
            for (const Brush& brush : mover.brushes) {
                addBrush(m_triangles, brush, {}, m_agent.minGroundNormalZ);
            }
        } else if (mover.kind == NavMoverKind::Lift) {
            for (const Vec3 offset : {Vec3{}, mover.move}) {
                for (const Brush& brush : mover.brushes) {
                    addBrush(m_triangles, brush, offset, m_agent.minGroundNormalZ);
                    const float top = brushTop(brush) + offset.z;
                    if (std::optional<Mark> mark =
                            markBrush(brush, offset, 0.0f, top - m_agent.climb, top + m_agent.climb,
                                      area, i)) {
                        m_marks.push_back(std::move(*mark));
                    }
                }
            }
            if (std::optional<Link> link = liftLink(mover)) {
                link->mover = i;
                m_links.push_back(*link);
            }
        } else {
            for (const Brush& brush : mover.brushes) {
                Aabb bounds;
                if (!computeBrushBounds(brush, bounds)) {
                    continue;
                }
                if (std::optional<Mark> mark = markBrush(
                        brush, {}, grow, bounds.min.z - m_agent.height, bounds.max.z, area, i)) {
                    m_marks.push_back(std::move(*mark));
                }
            }
        }
    }
    if (empty()) {
        return;
    }
    rcCalcBounds(m_triangles.vertices.data(), static_cast<int>(m_triangles.vertices.size() / 3),
                 m_min.data(), m_max.data());
    int cellsX = 0;
    int cellsY = 0;
    rcCalcGridSize(m_min.data(), m_max.data(), m_cellSize, &cellsX, &cellsY);
    m_tilesX = (cellsX + kTileCells - 1) / kTileCells;
    m_tilesY = (cellsY + kTileCells - 1) / kTileCells;
    binTriangles();
}

void Builder::binTriangles() {
    m_bins.assign(static_cast<std::size_t>(m_tilesX) * static_cast<std::size_t>(m_tilesY), {});
    const float tileWidth = static_cast<float>(kTileCells) * m_cellSize;
    const float border = static_cast<float>(kBorderCells) * m_cellSize;
    const auto tileOf = [&](float position, float origin, int count) {
        const int tile = static_cast<int>(std::floor((position - origin) / tileWidth));
        return std::clamp(tile, 0, count - 1);
    };
    const std::size_t count = m_triangles.areas.size();
    for (std::size_t t = 0; t < count; ++t) {
        float minX = std::numeric_limits<float>::max();
        float minZ = minX;
        float maxX = -minX;
        float maxZ = -minX;
        for (std::size_t k = 0; k < 3; ++k) {
            const auto v = static_cast<std::size_t>(m_triangles.indices[t * 3 + k]) * 3;
            minX = std::min(minX, m_triangles.vertices[v]);
            maxX = std::max(maxX, m_triangles.vertices[v]);
            minZ = std::min(minZ, m_triangles.vertices[v + 2]);
            maxZ = std::max(maxZ, m_triangles.vertices[v + 2]);
        }
        const int x0 = tileOf(minX - border, m_min[0], m_tilesX);
        const int x1 = tileOf(maxX + border, m_min[0], m_tilesX);
        const int y0 = tileOf(minZ - border, m_min[2], m_tilesY);
        const int y1 = tileOf(maxZ + border, m_min[2], m_tilesY);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                m_bins[static_cast<std::size_t>(y * m_tilesX + x)].push_back(static_cast<int>(t));
            }
        }
    }
}

rcConfig Builder::tileConfig(int tileX, int tileY, std::span<const int> triangles) const {
    rcConfig config{};
    config.cs = m_cellSize;
    config.ch = kCellHeight;
    config.walkableHeight = static_cast<int>(std::ceil(m_agent.height / kCellHeight));
    config.walkableClimb = static_cast<int>(std::floor(m_agent.climb / kCellHeight));
    config.walkableRadius = kRadiusCells;
    config.maxEdgeLen = static_cast<int>(kMaxEdgeRadii * static_cast<float>(kRadiusCells));
    config.maxSimplificationError = kMaxSimplificationError;
    config.minRegionArea = kMinRegionCells;
    config.mergeRegionArea = kMergeRegionCells;
    config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    config.tileSize = kTileCells;
    config.borderSize = kBorderCells;
    config.width = kTileCells + 2 * kBorderCells;
    config.height = config.width;
    config.detailSampleDist = static_cast<float>(kDetailSampleCells) * m_cellSize;
    config.detailSampleMaxError = kCellHeight;

    const float tileWidth = static_cast<float>(kTileCells) * m_cellSize;
    const float border = static_cast<float>(kBorderCells) * m_cellSize;
    config.bmin[0] = m_min[0] + static_cast<float>(tileX) * tileWidth - border;
    config.bmin[2] = m_min[2] + static_cast<float>(tileY) * tileWidth - border;
    config.bmax[0] = m_min[0] + static_cast<float>(tileX + 1) * tileWidth + border;
    config.bmax[2] = m_min[2] + static_cast<float>(tileY + 1) * tileWidth + border;

    // Only as tall as what is in the tile, which keeps spans within the heights Recast can hold.
    float minHeight = std::numeric_limits<float>::max();
    float maxHeight = -minHeight;
    for (int t : triangles) {
        for (std::size_t k = 0; k < 3; ++k) {
            const auto v =
                static_cast<std::size_t>(m_triangles.indices[static_cast<std::size_t>(t) * 3 + k]) *
                3;
            minHeight = std::min(minHeight, m_triangles.vertices[v + 1]);
            maxHeight = std::max(maxHeight, m_triangles.vertices[v + 1]);
        }
    }
    config.bmin[1] = minHeight - kCellHeight;
    config.bmax[1] = maxHeight + m_agent.height;
    return config;
}

std::uint32_t Builder::moverOfPolygon(const rcPolyMesh& mesh, int polygon) const {
    const unsigned char area = mesh.areas[polygon];
    // The middle of the polygon.
    const unsigned short* corners = &mesh.polys[polygon * 2 * mesh.nvp];
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    int count = 0;
    for (; count < mesh.nvp && corners[count] != RC_MESH_NULL_IDX; ++count) {
        const unsigned short* v = &mesh.verts[corners[count] * 3];
        x += static_cast<float>(v[0]);
        y += static_cast<float>(v[1]);
        z += static_cast<float>(v[2]);
    }
    const float inverse = 1.0f / static_cast<float>(std::max(count, 1));
    x = mesh.bmin[0] + x * inverse * mesh.cs;
    y = mesh.bmin[1] + y * inverse * mesh.ch;
    z = mesh.bmin[2] + z * inverse * mesh.cs;
    // Marks sharing the area id; the one the middle is in, or failing that the nearest.
    std::uint32_t best = kNoIndex;
    float bestDistance = std::numeric_limits<float>::max();
    for (const Mark& mark : m_marks) {
        if (mark.area != area || mark.mover == kNoIndex) {
            continue;
        }
        const float dx = std::max({mark.minX - x, 0.0f, x - mark.maxX});
        const float dz = std::max({mark.minZ - z, 0.0f, z - mark.maxZ});
        const float dy = std::max({mark.minHeight - y, 0.0f, y - mark.maxHeight});
        const float distance = dx * dx + dy * dy + dz * dz;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = mark.mover;
        }
    }
    return best;
}

std::optional<BuiltTile> Builder::buildTile(int tileX, int tileY) {
    const std::vector<int>& bin = m_bins[static_cast<std::size_t>(tileY * m_tilesX + tileX)];
    if (bin.empty()) {
        return std::nullopt;
    }
    rcConfig config = tileConfig(tileX, tileY, bin);
    const auto fail = [&](std::string_view step) {
        m_warnings.push_back(
            std::format("navigation tile ({}, {}): {} failed", tileX, tileY, step));
        return std::nullopt;
    };
    const float tallest = static_cast<float>(RC_SPAN_MAX_HEIGHT - 1) * kCellHeight;
    if (config.bmax[1] - config.bmin[1] > tallest) {
        m_warnings.push_back(std::format(
            "navigation tile ({}, {}) is {:.0f} m tall; floors above the lowest {:.0f} m are "
            "left out",
            tileX, tileY, config.bmax[1] - config.bmin[1], tallest));
        config.bmax[1] = config.bmin[1] + tallest;
    }

    std::vector<int> indices;
    std::vector<unsigned char> areas;
    indices.reserve(bin.size() * 3);
    areas.reserve(bin.size());
    for (int t : bin) {
        const auto first = static_cast<std::size_t>(t) * 3;
        indices.insert(indices.end(),
                       m_triangles.indices.begin() + static_cast<std::ptrdiff_t>(first),
                       m_triangles.indices.begin() + static_cast<std::ptrdiff_t>(first + 3));
        areas.push_back(m_triangles.areas[static_cast<std::size_t>(t)]);
    }

    RecastPtr<rcHeightfield> heightfield(rcAllocHeightfield());
    if (!heightfield || !rcCreateHeightfield(&m_context, *heightfield, config.width, config.height,
                                             config.bmin, config.bmax, config.cs, config.ch)) {
        return fail("making the heightfield");
    }
    if (!rcRasterizeTriangles(&m_context, m_triangles.vertices.data(),
                              static_cast<int>(m_triangles.vertices.size() / 3), indices.data(),
                              areas.data(), static_cast<int>(areas.size()), *heightfield,
                              config.walkableClimb)) {
        return fail("rasterising");
    }
    rcFilterLowHangingWalkableObstacles(&m_context, config.walkableClimb, *heightfield);
    rcFilterLedgeSpans(&m_context, config.walkableHeight, config.walkableClimb, *heightfield);
    rcFilterWalkableLowHeightSpans(&m_context, config.walkableHeight, *heightfield);

    RecastPtr<rcCompactHeightfield> compact(rcAllocCompactHeightfield());
    if (!compact || !rcBuildCompactHeightfield(&m_context, config.walkableHeight,
                                               config.walkableClimb, *heightfield, *compact)) {
        return fail("compacting");
    }
    heightfield.reset();
    if (!rcErodeWalkableArea(&m_context, config.walkableRadius, *compact)) {
        return fail("eroding");
    }
    for (const Mark& mark : m_marks) {
        if (mark.maxX >= config.bmin[0] && mark.minX <= config.bmax[0] &&
            mark.maxZ >= config.bmin[2] && mark.minZ <= config.bmax[2]) {
            rcMarkConvexPolyArea(&m_context, mark.polygon.data(),
                                 static_cast<int>(mark.polygon.size() / 3), mark.minHeight,
                                 mark.maxHeight, mark.area, *compact);
        }
    }
    if (!rcBuildDistanceField(&m_context, *compact) ||
        !rcBuildRegions(&m_context, *compact, config.borderSize, config.minRegionArea,
                        config.mergeRegionArea)) {
        return fail("finding regions");
    }
    RecastPtr<rcContourSet> contours(rcAllocContourSet());
    if (!contours || !rcBuildContours(&m_context, *compact, config.maxSimplificationError,
                                      config.maxEdgeLen, *contours)) {
        return fail("tracing contours");
    }
    if (contours->nconts == 0) {
        return std::nullopt;
    }
    RecastPtr<rcPolyMesh> mesh(rcAllocPolyMesh());
    if (!mesh || !rcBuildPolyMesh(&m_context, *contours, config.maxVertsPerPoly, *mesh)) {
        return fail("making polygons");
    }
    RecastPtr<rcPolyMeshDetail> detail(rcAllocPolyMeshDetail());
    if (!detail || !rcBuildPolyMeshDetail(&m_context, *mesh, *compact, config.detailSampleDist,
                                          config.detailSampleMaxError, *detail)) {
        return fail("adding heights");
    }
    if (mesh->npolys == 0) {
        return std::nullopt;
    }
    if (mesh->nverts >= 0xffff) {
        return fail("fitting the polygons in a tile");
    }

    BuiltTile tile;
    tile.movers.resize(static_cast<std::size_t>(mesh->npolys), kNoNavMover);
    for (int i = 0; i < mesh->npolys; ++i) {
        NavArea area = NavArea::Ground;
        if (mesh->areas[i] == kBuildHazard) {
            area = NavArea::Hazard;
        } else if (mesh->areas[i] >= kFirstMoverArea) {
            const std::uint32_t mover = moverOfPolygon(*mesh, i);
            if (mover != kNoIndex) {
                const NavMover& navMover = m_input.movers[mover];
                area = navMover.kind == NavMoverKind::Door ? NavArea::Door : NavArea::Lift;
                tile.movers[static_cast<std::size_t>(i)] = navMover.id;
            }
        }
        mesh->areas[i] = nav::detourArea(area);
        mesh->flags[i] = nav::kWalkableFlag;
    }

    // Links are kept by the tile their start is in.
    std::vector<float> linkEnds;
    std::vector<float> linkRadii;
    std::vector<unsigned short> linkFlags;
    std::vector<unsigned char> linkAreas;
    std::vector<unsigned char> linkDirections;
    std::vector<unsigned int> linkIds;
    for (const Link& link : m_links) {
        if (link.ends[0] >= mesh->bmin[0] && link.ends[0] < mesh->bmax[0] &&
            link.ends[2] >= mesh->bmin[2] && link.ends[2] < mesh->bmax[2]) {
            linkEnds.insert(linkEnds.end(), link.ends.begin(), link.ends.end());
            linkRadii.push_back(m_agent.radius);
            linkFlags.push_back(nav::kWalkableFlag);
            linkAreas.push_back(nav::detourArea(NavArea::Lift));
            linkDirections.push_back(DT_OFFMESH_CON_BIDIR);
            linkIds.push_back(m_input.movers[link.mover].id);
        }
    }

    dtNavMeshCreateParams params{};
    params.verts = mesh->verts;
    params.vertCount = mesh->nverts;
    params.polys = mesh->polys;
    params.polyAreas = mesh->areas;
    params.polyFlags = mesh->flags;
    params.polyCount = mesh->npolys;
    params.nvp = mesh->nvp;
    params.detailMeshes = detail->meshes;
    params.detailVerts = detail->verts;
    params.detailVertsCount = detail->nverts;
    params.detailTris = detail->tris;
    params.detailTriCount = detail->ntris;
    params.offMeshConVerts = linkEnds.data();
    params.offMeshConRad = linkRadii.data();
    params.offMeshConFlags = linkFlags.data();
    params.offMeshConAreas = linkAreas.data();
    params.offMeshConDir = linkDirections.data();
    params.offMeshConUserID = linkIds.data();
    params.offMeshConCount = static_cast<int>(linkIds.size());
    params.walkableHeight = m_agent.height;
    params.walkableRadius = m_agent.radius;
    params.walkableClimb = m_agent.climb;
    params.tileX = tileX;
    params.tileY = tileY;
    params.tileLayer = 0;
    std::copy(mesh->bmin, mesh->bmin + 3, params.bmin);
    std::copy(mesh->bmax, mesh->bmax + 3, params.bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;

    unsigned char* data = nullptr;
    if (!dtCreateNavMeshData(&params, &data, &tile.size)) {
        return fail("packing the tile");
    }
    tile.data.reset(data);
    return tile;
}

} // namespace

void NavMesh::MeshDeleter::operator()(dtNavMesh* mesh) const { dtFreeNavMesh(mesh); }

NavMesh::NavMesh(const NavInput& input, const NavAgent& agent)
    : m_agent(agent), m_mesh(dtAllocNavMesh()) {
    Builder builder(input, agent, m_warnings);
    std::vector<BuiltTile> tiles;
    for (int y = 0; y < builder.tilesY(); ++y) {
        for (int x = 0; x < builder.tilesX(); ++x) {
            if (std::optional<BuiltTile> tile = builder.buildTile(x, y)) {
                tiles.push_back(std::move(*tile));
            }
        }
    }

    dtNavMeshParams params{};
    if (!builder.empty()) {
        std::copy(builder.origin(), builder.origin() + 3, params.orig);
    }
    params.tileWidth = static_cast<float>(kTileCells) * builder.cellSize();
    params.tileHeight = params.tileWidth;
    params.maxTiles = std::max(static_cast<int>(tiles.size()), 1);
    params.maxPolys = 1 << DT_POLY_BITS;
    if (!m_mesh || dtStatusFailed(m_mesh->init(&params))) {
        m_warnings.push_back("navigation mesh: setting up Detour failed");
        return;
    }
    m_polygonMovers.resize(static_cast<std::size_t>(params.maxTiles));
    for (BuiltTile& built : tiles) {
        dtTileRef ref = 0;
        if (dtStatusFailed(
                m_mesh->addTile(built.data.get(), built.size, DT_TILE_FREE_DATA, 0, &ref))) {
            m_warnings.push_back("navigation mesh: adding a tile failed");
            continue;
        }
        built.data.release();
        const dtMeshTile* tile = m_mesh->getTileByRef(ref);
        const unsigned int index = m_mesh->decodePolyIdTile(m_mesh->getPolyRefBase(tile));
        std::vector<std::uint32_t>& movers = m_polygonMovers[index];
        movers = std::move(built.movers);
        movers.resize(static_cast<std::size_t>(tile->header->polyCount), kNoNavMover);
        for (int i = 0; i < tile->header->offMeshConCount; ++i) {
            const dtOffMeshConnection& link = tile->offMeshCons[i];
            movers[link.poly] = link.userId;
        }
        m_polygonCount +=
            static_cast<std::size_t>(tile->header->polyCount - tile->header->offMeshConCount);
    }
}

NavMesh::~NavMesh() = default;

std::uint32_t NavMesh::moverOf(std::uint64_t polygon) const {
    unsigned int salt = 0;
    unsigned int tile = 0;
    unsigned int index = 0;
    m_mesh->decodePolyId(polygon, salt, tile, index);
    if (tile >= m_polygonMovers.size() || index >= m_polygonMovers[tile].size()) {
        return kNoNavMover;
    }
    return m_polygonMovers[tile][index];
}

std::vector<NavTriangle> NavMesh::triangles() const {
    std::vector<NavTriangle> triangles;
    std::uint32_t polygon = 0;
    const dtNavMesh& mesh = *m_mesh;
    for (int t = 0; t < mesh.getMaxTiles(); ++t) {
        const dtMeshTile* tile = mesh.getTile(t);
        if (tile == nullptr || tile->header == nullptr) {
            continue;
        }
        for (int p = 0; p < tile->header->polyCount; ++p) {
            const dtPoly& poly = tile->polys[p];
            if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
                continue;
            }
            const dtPolyDetail& detail = tile->detailMeshes[p];
            for (unsigned i = 0; i < detail.triCount; ++i) {
                const unsigned char* corners = &tile->detailTris[(detail.triBase + i) * 4];
                NavTriangle triangle;
                for (std::size_t k = 0; k < 3; ++k) {
                    const unsigned char c = corners[k];
                    const float* v =
                        c < poly.vertCount
                            ? &tile->verts[poly.verts[c] * 3]
                            : &tile->detailVerts[(detail.vertBase + c - poly.vertCount) * 3];
                    triangle.corners[k] = fromDetour(v);
                }
                triangle.area = nav::navArea(poly.getArea());
                triangle.polygon = polygon;
                triangles.push_back(triangle);
            }
            ++polygon;
        }
    }
    return triangles;
}

std::vector<NavEdge> NavMesh::edges() const {
    std::vector<NavEdge> edges;
    const dtNavMesh& mesh = *m_mesh;
    for (int t = 0; t < mesh.getMaxTiles(); ++t) {
        const dtMeshTile* tile = mesh.getTile(t);
        if (tile == nullptr || tile->header == nullptr) {
            continue;
        }
        for (int p = 0; p < tile->header->polyCount; ++p) {
            const dtPoly& poly = tile->polys[p];
            if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
                continue;
            }
            for (unsigned char i = 0; i < poly.vertCount; ++i) {
                const unsigned short neighbour = poly.neis[i];
                // A shared edge is taken from one side only: in a tile, from the polygon with the
                // lower index; between tiles, from the tile whose neighbour is on its +x or +z
                // side (Detour's sides 0 to 3, in its axes).
                if ((neighbour & DT_EXT_LINK) != 0 ? (neighbour & 0xff) >= 4
                                                   : neighbour != 0 && neighbour - 1 < p) {
                    continue;
                }
                const unsigned char j = static_cast<unsigned char>((i + 1) % poly.vertCount);
                edges.push_back({fromDetour(&tile->verts[poly.verts[i] * 3]),
                                 fromDetour(&tile->verts[poly.verts[j] * 3]), neighbour == 0});
            }
        }
    }
    return edges;
}

std::vector<NavLink> NavMesh::links() const {
    std::vector<NavLink> links;
    const dtNavMesh& mesh = *m_mesh;
    for (int t = 0; t < mesh.getMaxTiles(); ++t) {
        const dtMeshTile* tile = mesh.getTile(t);
        if (tile == nullptr || tile->header == nullptr) {
            continue;
        }
        for (int i = 0; i < tile->header->offMeshConCount; ++i) {
            const dtOffMeshConnection& link = tile->offMeshCons[i];
            links.push_back({fromDetour(&link.pos[0]), fromDetour(&link.pos[3]), link.userId});
        }
    }
    return links;
}

} // namespace ct
