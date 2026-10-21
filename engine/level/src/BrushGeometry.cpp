#include "croftech/level/BrushGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace ct {
namespace {

// Points this close to a plane are on it, and corners this close together are one: far above the
// error of double arithmetic on float corners, far below the editor's smallest grid (1/32 m).
constexpr double kOnPlane = 1e-4;
constexpr std::size_t kNoPlane = std::numeric_limits<std::size_t>::max();
// Building from planes starts with a box this far out on each axis. It is well outside the world,
// so a solid that still reaches it is unbounded or outside the world.
constexpr double kStartHalfSize = 2.0 * kMaxWorldCoordinate;

using Edge = std::pair<std::uint32_t, std::uint32_t>;

// A brush in double precision while it is built. planeOfFace runs alongside faces.
struct Solid {
    std::vector<DVec3> vertices;
    std::vector<std::vector<std::uint32_t>> faces;
    std::vector<std::size_t> planeOfFace;
};

Solid toSolid(const Brush& brush) {
    Solid solid;
    for (const Vec3& v : brush.vertices) {
        solid.vertices.push_back(toDouble(v));
    }
    for (std::size_t i = 0; i < brush.faces.size(); ++i) {
        solid.faces.push_back(brush.faces[i].corners);
        solid.planeOfFace.push_back(i);
    }
    return solid;
}

Brush toBrush(const Solid& solid) {
    Brush brush;
    for (const DVec3& v : solid.vertices) {
        brush.vertices.push_back(toFloat(v));
    }
    for (const std::vector<std::uint32_t>& corners : solid.faces) {
        brush.faces.push_back({corners});
    }
    return brush;
}

void keepFaces(Solid& solid, const std::vector<bool>& keep) {
    std::size_t kept = 0;
    for (std::size_t i = 0; i < solid.faces.size(); ++i) {
        if (!keep[i]) {
            continue;
        }
        // Never move a vector onto itself: libstdc++ empties it.
        if (kept != i) {
            solid.faces[kept] = std::move(solid.faces[i]);
            solid.planeOfFace[kept] = solid.planeOfFace[i];
        }
        ++kept;
    }
    solid.faces.resize(kept);
    solid.planeOfFace.resize(kept);
}

// Removes corners where a face's outline runs straight on (they sit on an edge, not at a corner),
// then faces left with under three corners, then corners no face uses.
void tidy(Solid& solid) {
    const std::vector<DVec3>& v = solid.vertices;
    std::vector<bool> keep(solid.faces.size());
    for (std::size_t f = 0; f < solid.faces.size(); ++f) {
        std::vector<std::uint32_t>& face = solid.faces[f];
        bool removed = true;
        while (removed && face.size() >= 3) {
            removed = false;
            for (std::size_t i = 0; i < face.size(); ++i) {
                const DVec3 prev = v[face[(i + face.size() - 1) % face.size()]];
                const DVec3 next = v[face[(i + 1) % face.size()]];
                const DVec3 chord = next - prev;
                const double chordLength = length(chord);
                const DVec3 offset = v[face[i]] - prev;
                const double off =
                    chordLength > 0.0 ? length(cross(chord, offset)) / chordLength : length(offset);
                if (off <= kOnPlane) {
                    face.erase(face.begin() + static_cast<std::ptrdiff_t>(i));
                    removed = true;
                    break;
                }
            }
        }
        keep[f] = face.size() >= 3;
    }
    keepFaces(solid, keep);

    std::vector<std::uint32_t> remap(solid.vertices.size(), 0);
    std::vector<bool> used(solid.vertices.size(), false);
    for (const auto& face : solid.faces) {
        for (const std::uint32_t corner : face) {
            used[corner] = true;
        }
    }
    std::vector<DVec3> vertices;
    for (std::size_t i = 0; i < solid.vertices.size(); ++i) {
        if (used[i]) {
            remap[i] = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(solid.vertices[i]);
        }
    }
    solid.vertices = std::move(vertices);
    for (auto& face : solid.faces) {
        for (std::uint32_t& corner : face) {
            corner = remap[corner];
        }
    }
}

// Rounds the corners to float, taking the value of an exact corner where one is within a hair,
// and merges corners that round to the same point.
void roundSolid(Solid& solid, std::span<const Vec3> exactCorners) {
    std::vector<Vec3> rounded;
    std::vector<std::uint32_t> remap(solid.vertices.size());
    for (std::size_t i = 0; i < solid.vertices.size(); ++i) {
        const DVec3 v = solid.vertices[i];
        Vec3 value = toFloat(v) + Vec3{}; // adding zero turns -0 into 0
        for (const Vec3& exact : exactCorners) {
            if (length(toDouble(exact) - v) <= kOnPlane) {
                value = exact;
                break;
            }
        }
        const auto found = std::find(rounded.begin(), rounded.end(), value);
        remap[i] = static_cast<std::uint32_t>(found - rounded.begin());
        if (found == rounded.end()) {
            rounded.push_back(value);
        }
    }
    solid.vertices.clear();
    for (const Vec3& v : rounded) {
        solid.vertices.push_back(toDouble(v));
    }
    for (auto& face : solid.faces) {
        std::vector<std::uint32_t> corners;
        for (const std::uint32_t corner : face) {
            if (corners.empty() || corners.back() != remap[corner]) {
                corners.push_back(remap[corner]);
            }
        }
        while (corners.size() > 1 && corners.front() == corners.back()) {
            corners.pop_back();
        }
        face = std::move(corners);
    }
    tidy(solid);
}

std::optional<Brush> finish(Solid& solid, std::span<const Vec3> exactCorners) {
    roundSolid(solid, exactCorners);
    Brush brush = toBrush(solid);
    std::string problem;
    if (!checkBrush(brush, problem)) {
        return std::nullopt;
    }
    return brush;
}

// Chains directed edges into a loop of their start corners. False unless they make exactly one.
bool chainLoop(std::span<const Edge> edges, std::vector<std::uint32_t>& loop) {
    loop.clear();
    if (edges.size() < 3) {
        return false;
    }
    std::map<std::uint32_t, std::uint32_t> next;
    for (const auto& [from, to] : edges) {
        if (!next.emplace(from, to).second) {
            return false;
        }
    }
    const std::uint32_t start = edges.front().first;
    std::uint32_t at = start;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        if (i > 0 && at == start) {
            return false; // back at the start early: more than one loop
        }
        loop.push_back(at);
        const auto found = next.find(at);
        if (found == next.end()) {
            return false;
        }
        at = found->second;
    }
    return at == start;
}

std::vector<Edge> directedEdges(const std::vector<std::vector<std::uint32_t>>& faces) {
    std::vector<Edge> edges;
    for (const auto& face : faces) {
        for (std::size_t i = 0; i < face.size(); ++i) {
            edges.emplace_back(face[i], face[(i + 1) % face.size()]);
        }
    }
    std::sort(edges.begin(), edges.end());
    return edges;
}

enum class Clip { Unchanged, Cut, Nothing, Broken };

// Keeps the part of the solid behind the plane and closes it with a face on the plane.
Clip clipSolid(Solid& solid, const DPlane& plane, std::size_t planeIndex) {
    const double scale = length(plane.normal);
    if (!(scale > 0.0) || !std::isfinite(scale) || !std::isfinite(plane.distance)) {
        return Clip::Broken;
    }
    const std::size_t count = solid.vertices.size();
    std::vector<double> distances(count);
    std::vector<int> sides(count);
    bool anyFront = false;
    bool anyBack = false;
    for (std::size_t i = 0; i < count; ++i) {
        const double d = (dot(plane.normal, solid.vertices[i]) - plane.distance) / scale;
        distances[i] = d;
        sides[i] = d > kOnPlane ? 1 : (d < -kOnPlane ? -1 : 0);
        anyFront = anyFront || sides[i] > 0;
        anyBack = anyBack || sides[i] < 0;
    }
    if (!anyFront) {
        return Clip::Unchanged;
    }
    if (!anyBack) {
        return Clip::Nothing;
    }

    // An axis-aligned plane gives its cut points its own coordinate exactly.
    const std::array<double, 3> normal{plane.normal.x, plane.normal.y, plane.normal.z};
    int axis = -1;
    for (int k = 0; k < 3; ++k) {
        if (normal[static_cast<std::size_t>(k)] != 0.0) {
            axis = axis == -1 ? k : 3;
        }
    }

    Solid result;
    std::vector<std::uint32_t> remap(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        if (sides[i] <= 0) {
            remap[i] = static_cast<std::uint32_t>(result.vertices.size());
            result.vertices.push_back(solid.vertices[i]);
        }
    }
    std::map<Edge, std::uint32_t> cuts;
    const auto cut = [&](std::uint32_t a, std::uint32_t b) {
        // Always from the lower index, so both faces on an edge get the same point.
        const Edge key{std::min(a, b), std::max(a, b)};
        if (const auto found = cuts.find(key); found != cuts.end()) {
            return found->second;
        }
        const DVec3 from = solid.vertices[key.first];
        const DVec3 to = solid.vertices[key.second];
        const double t = distances[key.first] / (distances[key.first] - distances[key.second]);
        DVec3 point = from + (to - from) * t;
        if (axis >= 0 && axis < 3) {
            const double value = plane.distance / normal[static_cast<std::size_t>(axis)];
            (axis == 0 ? point.x : (axis == 1 ? point.y : point.z)) = value;
        }
        const auto index = static_cast<std::uint32_t>(result.vertices.size());
        result.vertices.push_back(point);
        cuts.emplace(key, index);
        return index;
    };
    for (std::size_t f = 0; f < solid.faces.size(); ++f) {
        const std::vector<std::uint32_t>& face = solid.faces[f];
        std::vector<std::uint32_t> kept;
        for (std::size_t i = 0; i < face.size(); ++i) {
            const std::uint32_t current = face[i];
            const std::uint32_t next = face[(i + 1) % face.size()];
            if (sides[current] <= 0) {
                kept.push_back(remap[current]);
            }
            if (sides[current] * sides[next] < 0) {
                kept.push_back(cut(current, next));
            }
        }
        if (kept.size() >= 3) {
            result.faces.push_back(std::move(kept));
            result.planeOfFace.push_back(solid.planeOfFace[f]);
        }
    }

    // The kept faces leave a hole on the plane; its rim, walked the other way, is the new face.
    const std::vector<Edge> edges = directedEdges(result.faces);
    std::vector<Edge> rim;
    for (const auto& [from, to] : edges) {
        if (!std::binary_search(edges.begin(), edges.end(), Edge{to, from})) {
            rim.emplace_back(to, from);
        }
    }
    std::vector<std::uint32_t> cap;
    if (!chainLoop(rim, cap)) {
        return Clip::Broken;
    }
    result.faces.push_back(std::move(cap));
    result.planeOfFace.push_back(planeIndex);
    tidy(result);
    solid = std::move(result);
    return Clip::Cut;
}

Solid startBox() {
    const double s = kStartHalfSize;
    Solid solid;
    // Corner i has bit 0 for x, bit 1 for y, bit 2 for z, as in makeBoxBrush.
    for (std::uint32_t i = 0; i < 8; ++i) {
        solid.vertices.push_back(
            {(i & 1) != 0 ? s : -s, (i & 2) != 0 ? s : -s, (i & 4) != 0 ? s : -s});
    }
    solid.faces = {{1, 3, 7, 5}, {0, 4, 6, 2}, {2, 6, 7, 3},
                   {0, 1, 5, 4}, {4, 5, 7, 6}, {0, 2, 3, 1}};
    solid.planeOfFace.assign(6, kNoPlane);
    return solid;
}

// A triangle of the hull being built, with its plane (unit normal).
struct Triangle {
    std::array<std::uint32_t, 3> corners{};
    DVec3 normal;
    double distance = 0.0;
    bool alive = true;

    double height(DVec3 p) const { return dot(normal, p) - distance; }
};

std::optional<Triangle> makeTriangle(const std::vector<DVec3>& points, std::uint32_t a,
                                     std::uint32_t b, std::uint32_t c) {
    const DVec3 n = cross(points[b] - points[a], points[c] - points[a]);
    const double size = length(n);
    if (!(size > 0.0)) {
        return std::nullopt;
    }
    const DVec3 unit = n * (1.0 / size);
    return Triangle{{a, b, c}, unit, dot(unit, points[a]), true};
}

// The convex hull as triangles, built a point at a time, then neighbouring triangles in one plane
// merged into faces.
std::optional<Solid> hull(std::span<const Vec3> input) {
    std::vector<DVec3> points;
    for (const Vec3& p : input) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
            return std::nullopt;
        }
        const DVec3 d = toDouble(p);
        if (std::none_of(points.begin(), points.end(),
                         [&](DVec3 q) { return length(q - d) <= kOnPlane; })) {
            points.push_back(d);
        }
    }
    if (points.size() < 4) {
        return std::nullopt;
    }

    // A first tetrahedron of points far apart, so its faces are well defined.
    const auto farthest = [&](auto&& measure) {
        std::uint32_t best = 0;
        double bestValue = -1.0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const double value = measure(points[i]);
            if (value > bestValue) {
                best = static_cast<std::uint32_t>(i);
                bestValue = value;
            }
        }
        return std::pair{best, bestValue};
    };
    const std::uint32_t i0 = 0;
    const auto [i1, d1] = farthest([&](DVec3 p) { return length(p - points[i0]); });
    if (d1 <= kOnPlane) {
        return std::nullopt;
    }
    const DVec3 axis = (points[i1] - points[i0]) * (1.0 / d1);
    const auto [i2, d2] = farthest([&](DVec3 p) { return length(cross(p - points[i0], axis)); });
    if (d2 <= kOnPlane) {
        return std::nullopt;
    }
    std::uint32_t a = i1;
    std::uint32_t b = i2;
    std::optional<Triangle> base = makeTriangle(points, i0, a, b);
    if (!base) {
        return std::nullopt;
    }
    const auto [i3, d3] = farthest([&](DVec3 p) { return std::fabs(base->height(p)); });
    if (d3 <= kOnPlane) {
        return std::nullopt; // flat
    }
    if (base->height(points[i3]) > 0.0) {
        std::swap(a, b);
    }
    std::vector<Triangle> triangles;
    for (const std::array<std::uint32_t, 3>& t : {std::array{i0, a, b}, std::array{a, i0, i3},
                                                  std::array{b, a, i3}, std::array{i0, b, i3}}) {
        const std::optional<Triangle> triangle = makeTriangle(points, t[0], t[1], t[2]);
        if (!triangle) {
            return std::nullopt;
        }
        triangles.push_back(*triangle);
    }

    for (std::uint32_t k = 0; k < points.size(); ++k) {
        if (k == i0 || k == a || k == b || k == i3) {
            continue;
        }
        std::vector<Edge> edges;
        for (Triangle& triangle : triangles) {
            if (triangle.height(points[k]) > kOnPlane) {
                triangle.alive = false;
                for (std::size_t i = 0; i < 3; ++i) {
                    edges.emplace_back(triangle.corners[i], triangle.corners[(i + 1) % 3]);
                }
            }
        }
        if (edges.empty()) {
            continue; // inside, or on the surface
        }
        std::sort(edges.begin(), edges.end());
        std::erase_if(triangles, [](const Triangle& t) { return !t.alive; });
        // Edges of the visible triangles that only one of them has are the horizon.
        for (const auto& [from, to] : edges) {
            if (!std::binary_search(edges.begin(), edges.end(), Edge{to, from})) {
                const std::optional<Triangle> triangle = makeTriangle(points, from, to, k);
                if (!triangle) {
                    return std::nullopt;
                }
                triangles.push_back(*triangle);
            }
        }
    }

    std::map<Edge, std::size_t> owner;
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        for (std::size_t i = 0; i < 3; ++i) {
            const Edge edge{triangles[t].corners[i], triangles[t].corners[(i + 1) % 3]};
            if (!owner.emplace(edge, t).second) {
                return std::nullopt;
            }
        }
    }
    const auto across = [&](const Edge& edge) -> std::optional<std::size_t> {
        const auto found = owner.find({edge.second, edge.first});
        return found != owner.end() ? std::optional(found->second) : std::nullopt;
    };

    Solid solid;
    solid.vertices = points;
    std::vector<int> group(triangles.size(), -1);
    for (std::size_t seed = 0; seed < triangles.size(); ++seed) {
        if (group[seed] != -1) {
            continue;
        }
        const int id = static_cast<int>(solid.faces.size());
        const Triangle& plane = triangles[seed];
        std::vector<std::size_t> members{seed};
        group[seed] = id;
        for (std::size_t m = 0; m < members.size(); ++m) {
            const Triangle& triangle = triangles[members[m]];
            for (std::size_t i = 0; i < 3; ++i) {
                const auto neighbour = across({triangle.corners[i], triangle.corners[(i + 1) % 3]});
                if (!neighbour) {
                    return std::nullopt;
                }
                const Triangle& other = triangles[*neighbour];
                const bool coplanar =
                    dot(other.normal, plane.normal) > 0.0 &&
                    std::all_of(other.corners.begin(), other.corners.end(), [&](std::uint32_t c) {
                        return std::fabs(plane.height(points[c])) <= kOnPlane;
                    });
                if (group[*neighbour] == -1 && coplanar) {
                    group[*neighbour] = id;
                    members.push_back(*neighbour);
                }
            }
        }
        std::vector<Edge> rim;
        for (const std::size_t member : members) {
            for (std::size_t i = 0; i < 3; ++i) {
                const Edge edge{triangles[member].corners[i],
                                triangles[member].corners[(i + 1) % 3]};
                if (group[*across(edge)] != id) {
                    rim.push_back(edge);
                }
            }
        }
        std::vector<std::uint32_t> loop;
        if (!chainLoop(rim, loop)) {
            return std::nullopt;
        }
        solid.faces.push_back(std::move(loop));
        solid.planeOfFace.push_back(kNoPlane);
    }
    tidy(solid);
    return solid;
}

} // namespace

double DPlane::distanceTo(DVec3 point) const {
    return (dot(normal, point) - distance) / length(normal);
}

DPlane toDPlane(const Plane& plane) { return {toDouble(plane.normal), plane.distance}; }

Plane toPlane(const DPlane& plane) {
    const double size = length(plane.normal);
    // Adding zero turns -0 into 0; -0 reads badly in the inspector.
    return {toFloat(plane.normal * (1.0 / size)) + Vec3{},
            static_cast<float>(plane.distance / size) + 0.0f};
}

DPlane planeThroughPoints(DVec3 a, DVec3 b, DVec3 c) {
    const DVec3 normal = cross(b - a, c - a);
    return {normal, dot(normal, a)};
}

DPlane exactFacePlane(const Brush& brush, std::size_t face) {
    const std::vector<std::uint32_t>& corners = brush.faces[face].corners;
    const DVec3 origin = toDouble(brush.vertices[corners[0]]);
    // Newell's method, measured from the first corner: every corner counts, and differences of
    // grid points are exact.
    DVec3 normal;
    for (std::size_t i = 1; i + 1 < corners.size(); ++i) {
        normal += cross(toDouble(brush.vertices[corners[i]]) - origin,
                        toDouble(brush.vertices[corners[i + 1]]) - origin);
    }
    return {normal, dot(normal, origin)};
}

Plane facePlane(const Brush& brush, std::size_t face) {
    return toPlane(exactFacePlane(brush, face));
}

std::vector<Plane> brushPlanes(const Brush& brush) {
    std::vector<Plane> planes;
    planes.reserve(brush.faces.size());
    for (std::size_t i = 0; i < brush.faces.size(); ++i) {
        planes.push_back(facePlane(brush, i));
    }
    return planes;
}

std::vector<Vec3> faceVertices(const Brush& brush, std::size_t face) {
    std::vector<Vec3> vertices;
    for (const std::uint32_t corner : brush.faces[face].corners) {
        vertices.push_back(brush.vertices[corner]);
    }
    return vertices;
}

Vec3 faceCentre(const Brush& brush, std::size_t face) {
    DVec3 sum;
    const std::vector<std::uint32_t>& corners = brush.faces[face].corners;
    for (const std::uint32_t corner : corners) {
        sum += toDouble(brush.vertices[corner]);
    }
    return toFloat(sum * (1.0 / static_cast<double>(corners.size())));
}

std::vector<BrushPolygon> brushPolygons(const Brush& brush) {
    std::vector<BrushPolygon> polygons;
    polygons.reserve(brush.faces.size());
    for (std::size_t i = 0; i < brush.faces.size(); ++i) {
        polygons.push_back({facePlane(brush, i), faceVertices(brush, i)});
    }
    return polygons;
}

bool computeBrushBounds(const Brush& brush, Aabb& bounds) {
    if (brush.vertices.empty()) {
        return false;
    }
    bounds = {brush.vertices.front(), brush.vertices.front()};
    for (const Vec3& v : brush.vertices) {
        bounds.min = min(bounds.min, v);
        bounds.max = max(bounds.max, v);
    }
    return true;
}

std::optional<Brush> brushFromPlanes(std::span<const DPlane> planes,
                                     std::span<const Vec3> exactCorners,
                                     std::vector<std::size_t>* planeOfFace) {
    Solid solid = startBox();
    for (std::size_t i = 0; i < planes.size(); ++i) {
        const Clip clip = clipSolid(solid, planes[i], i);
        if (clip == Clip::Nothing || clip == Clip::Broken) {
            return std::nullopt;
        }
    }
    std::optional<Brush> brush = finish(solid, exactCorners);
    if (!brush || std::find(solid.planeOfFace.begin(), solid.planeOfFace.end(), kNoPlane) !=
                      solid.planeOfFace.end()) {
        return std::nullopt;
    }
    if (planeOfFace != nullptr) {
        *planeOfFace = solid.planeOfFace;
    }
    return brush;
}

std::optional<Brush> brushFromPoints(std::span<const Vec3> points) {
    std::optional<Solid> solid = hull(points);
    if (!solid) {
        return std::nullopt;
    }
    return finish(*solid, {});
}

std::optional<Brush> clipBrush(const Brush& brush, const DPlane& plane) {
    Solid solid = toSolid(brush);
    switch (clipSolid(solid, plane, brush.faces.size())) {
    case Clip::Unchanged:
        return brush;
    case Clip::Cut:
        return finish(solid, brush.vertices);
    case Clip::Nothing:
    case Clip::Broken:
        break;
    }
    return std::nullopt;
}

} // namespace ct
