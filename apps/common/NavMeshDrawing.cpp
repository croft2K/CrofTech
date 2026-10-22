#include "NavMeshDrawing.h"

#include "croftech/core/Random.h"

#include <cmath>

namespace ct {
namespace {

// Raised off the floor so the depth test doesn't hide it: the mesh's heights are within 5 cm of
// the floor's.
constexpr float kFloorLift = 0.06f;
constexpr float kEdgeLift = 0.08f;
constexpr float kEdgeWidth = 0.04f;
constexpr float kLinkWidth = 0.06f;
constexpr float kFloorAlpha = 0.45f;
// Rides along the ground arc this high, so they don't lie flat on the floor between their stops.
constexpr float kLinkArc = 0.5f;
constexpr int kLinkSegments = 12;

Vec3 areaColour(NavArea area) {
    switch (area) {
    case NavArea::Ground:
        return {0.05f, 0.4f, 0.2f};
    case NavArea::Door:
        return {0.55f, 0.25f, 0.02f};
    case NavArea::Lift:
        return {0.3f, 0.1f, 0.55f};
    case NavArea::Hazard:
        return {0.6f, 0.04f, 0.02f};
    }
    return {};
}

void addTriangle(std::vector<EffectVertex>& out, Vec3 a, Vec3 b, Vec3 c, Vec3 colour, float alpha) {
    out.push_back({a, colour, alpha});
    out.push_back({b, colour, alpha});
    out.push_back({c, colour, alpha});
}

// A line as two crossed strips, so it shows from any side.
void addLine(std::vector<EffectVertex>& out, Vec3 start, Vec3 end, float width, Vec3 colour,
             float alpha) {
    const Vec3 along = end - start;
    if (dot(along, along) == 0.0f) {
        return;
    }
    const Vec3 direction = normalize(along);
    Vec3 side = cross(direction, Vec3{0.0f, 0.0f, 1.0f});
    if (dot(side, side) < 1e-6f) {
        side = {1.0f, 0.0f, 0.0f};
    }
    side = normalize(side) * (width * 0.5f);
    const Vec3 other = normalize(cross(direction, side)) * (width * 0.5f);
    for (const Vec3 offset : {side, other}) {
        addTriangle(out, start - offset, start + offset, end + offset, colour, alpha);
        addTriangle(out, start - offset, end + offset, end - offset, colour, alpha);
    }
}

} // namespace

void NavMeshDrawing::set(const NavMesh& mesh) {
    m_vertices.clear();
    const Vec3 floorLift{0.0f, 0.0f, kFloorLift};
    for (const NavTriangle& triangle : mesh.triangles()) {
        // A shade between 0.7 and 1.3 of the area's colour for each polygon.
        const float shade = 0.7f + static_cast<float>(randomBits(triangle.polygon) % 61u) / 100.0f;
        addTriangle(m_vertices, triangle.corners[0] + floorLift, triangle.corners[1] + floorLift,
                    triangle.corners[2] + floorLift, areaColour(triangle.area) * shade,
                    kFloorAlpha);
    }
    const Vec3 edgeLift{0.0f, 0.0f, kEdgeLift};
    for (const NavEdge& edge : mesh.edges()) {
        if (edge.boundary) {
            addLine(m_vertices, edge.start + edgeLift, edge.end + edgeLift, kEdgeWidth,
                    {0.1f, 0.8f, 0.5f}, 0.8f);
        } else {
            addLine(m_vertices, edge.start + edgeLift, edge.end + edgeLift, kEdgeWidth * 0.5f,
                    {0.05f, 0.3f, 0.25f}, 0.6f);
        }
    }
    for (const NavLink& link : mesh.links()) {
        const Vec3 colour{0.6f, 0.3f, 1.0f};
        Vec3 previous = link.start + edgeLift;
        for (int i = 1; i <= kLinkSegments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kLinkSegments);
            const Vec3 point = link.start + (link.end - link.start) * t + edgeLift +
                               Vec3{0.0f, 0.0f, 4.0f * t * (1.0f - t) * kLinkArc};
            addLine(m_vertices, previous, point, kLinkWidth, colour, 1.0f);
            previous = point;
        }
        // A post at each stop.
        for (const Vec3 stop : {link.start, link.end}) {
            addLine(m_vertices, stop, stop + Vec3{0.0f, 0.0f, 0.5f}, kLinkWidth * 2.0f, colour,
                    1.0f);
        }
    }
    m_ready = true;
}

void NavMeshDrawing::clear() {
    m_vertices.clear();
    m_ready = false;
}

void NavMeshDrawing::add(std::vector<EffectVertex>& out) const {
    out.insert(out.end(), m_vertices.begin(), m_vertices.end());
}

} // namespace ct
