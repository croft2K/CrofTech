#include "croftech/nav/NavQuery.h"

#include "NavDetour.h"

#include <DetourNavMeshQuery.h>

#include <algorithm>

namespace ct {
namespace {

using nav::fromDetour;
using nav::toDetour;

// Polygons a search may visit at once; enough for paths across the largest levels.
constexpr int kMaxSearchNodes = 4096;
// Polygons along one path.
constexpr int kMaxPathPolygons = 512;

// Turns away polygons and links of blocked movers, and hazards unless they are allowed (then they
// cost several times their length, so paths still go round them where they can).
class Filter final : public dtQueryFilter {
public:
    static constexpr float kHazardCost = 8.0f;

    Filter(const NavMesh& mesh, const NavFilter& filter) : m_mesh(mesh), m_filter(filter) {
        setAreaCost(nav::detourArea(NavArea::Hazard), kHazardCost);
    }

    bool passFilter(const dtPolyRef ref, const dtMeshTile* tile,
                    const dtPoly* poly) const override {
        if (m_filter.avoidHazards && poly->getArea() == nav::detourArea(NavArea::Hazard)) {
            return false;
        }
        if (!m_filter.blockedMovers.empty()) {
            const std::uint32_t mover = m_mesh.moverOf(ref);
            if (mover != kNoNavMover && std::binary_search(m_filter.blockedMovers.begin(),
                                                           m_filter.blockedMovers.end(), mover)) {
                return false;
            }
        }
        return dtQueryFilter::passFilter(ref, tile, poly);
    }

private:
    const NavMesh& m_mesh;
    const NavFilter& m_filter;
};

} // namespace

void NavQuery::QueryDeleter::operator()(dtNavMeshQuery* query) const { dtFreeNavMeshQuery(query); }

NavQuery::NavQuery(const NavMesh& mesh) : m_mesh(mesh), m_query(dtAllocNavMeshQuery()) {
    m_query->init(&mesh.detour(), kMaxSearchNodes);
}

NavQuery::~NavQuery() = default;

std::optional<Vec3> NavQuery::nearestPoint(Vec3 position, const NavFilter& filter,
                                           Vec3 reach) const {
    const Filter detourFilter(m_mesh, filter);
    const std::array<float, 3> centre = toDetour(position);
    const std::array<float, 3> extents{reach.x, reach.z, reach.y};
    dtPolyRef ref = 0;
    std::array<float, 3> nearest{};
    if (dtStatusFailed(m_query->findNearestPoly(centre.data(), extents.data(), &detourFilter, &ref,
                                                nearest.data())) ||
        ref == 0) {
        return std::nullopt;
    }
    return fromDetour(nearest.data());
}

bool NavQuery::findPath(Vec3 start, Vec3 goal, const NavFilter& filter, NavPath& path,
                        Vec3 reach) const {
    path.count = 0;
    path.complete = false;
    const Filter detourFilter(m_mesh, filter);
    const std::array<float, 3> extents{reach.x, reach.z, reach.y};
    const std::array<float, 3> from = toDetour(start);
    const std::array<float, 3> to = toDetour(goal);
    dtPolyRef startRef = 0;
    dtPolyRef goalRef = 0;
    std::array<float, 3> startPoint{};
    std::array<float, 3> goalPoint{};
    if (dtStatusFailed(m_query->findNearestPoly(from.data(), extents.data(), &detourFilter,
                                                &startRef, startPoint.data())) ||
        startRef == 0) {
        return false;
    }
    if (dtStatusFailed(m_query->findNearestPoly(to.data(), extents.data(), &detourFilter, &goalRef,
                                                goalPoint.data()))) {
        goalRef = 0;
    }

    std::array<dtPolyRef, kMaxPathPolygons> polygons{};
    int polygonCount = 0;
    bool complete = false;
    if (goalRef != 0) {
        const dtStatus status =
            m_query->findPath(startRef, goalRef, startPoint.data(), goalPoint.data(), &detourFilter,
                              polygons.data(), &polygonCount, kMaxPathPolygons);
        if (dtStatusFailed(status) || polygonCount == 0) {
            return false;
        }
        complete = !dtStatusDetail(status, DT_PARTIAL_RESULT) &&
                   polygons[static_cast<std::size_t>(polygonCount - 1)] == goalRef;
    } else {
        polygons[0] = startRef;
        polygonCount = 1;
    }
    // When the goal can't be reached, the path ends at the closest point of the last polygon.
    if (!complete) {
        bool over = false;
        m_query->closestPointOnPoly(polygons[static_cast<std::size_t>(polygonCount - 1)], to.data(),
                                    goalPoint.data(), &over);
    }

    std::array<float, kMaxNavPathCorners * 3> points{};
    std::array<unsigned char, kMaxNavPathCorners> flags{};
    std::array<dtPolyRef, kMaxNavPathCorners> entered{};
    int cornerCount = 0;
    if (dtStatusFailed(m_query->findStraightPath(
            startPoint.data(), goalPoint.data(), polygons.data(), polygonCount, points.data(),
            flags.data(), entered.data(), &cornerCount, static_cast<int>(kMaxNavPathCorners),
            DT_STRAIGHTPATH_AREA_CROSSINGS))) {
        return false;
    }
    const auto count = static_cast<std::size_t>(cornerCount);
    for (std::size_t i = 0; i < count; ++i) {
        NavCorner& corner = path.corners[i];
        corner.position = fromDetour(&points[i * 3]);
        corner.area = NavArea::Ground;
        corner.mover = kNoNavMover;
        corner.link = (flags[i] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0;
        // The corner at the goal enters nothing.
        if (entered[i] != 0) {
            unsigned char area = 0;
            m_mesh.detour().getPolyArea(entered[i], &area);
            corner.area = nav::navArea(area);
            corner.mover = m_mesh.moverOf(entered[i]);
        }
    }
    path.count = static_cast<std::uint32_t>(count);
    const bool reachedGoal = count > 0 && (flags[count - 1] & DT_STRAIGHTPATH_END) != 0;
    path.complete = complete && reachedGoal;
    return path.complete;
}

} // namespace ct
