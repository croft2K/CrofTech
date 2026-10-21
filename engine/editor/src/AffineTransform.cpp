#include "croftech/editor/AffineTransform.h"

#include "croftech/core/ConfigFile.h"

#include <cmath>
#include <numbers>
#include <string>

namespace ct {
namespace {

constexpr double kFlatDeterminant = 1e-12;

DVec3 operator+(DVec3 a, DVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
DVec3 operator-(DVec3 a, DVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
DVec3 operator*(DVec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(DVec3 a, DVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
DVec3 cross(DVec3 a, DVec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
DVec3 toDouble(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3 toFloat(DVec3 v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

DVec3 linear(const AffineTransform& t, DVec3 v) {
    return t.xAxis * v.x + t.yAxis * v.y + t.zAxis * v.z;
}

// Exact cosine and sine for quarter turns, so they don't pick up cos(90) = 6e-17.
void cosSin(double degrees, double& c, double& s) {
    const double quarters = degrees / 90.0;
    if (quarters == std::round(quarters)) {
        const double wrapped = std::fmod(quarters, 4.0);
        const auto turn = static_cast<std::size_t>(wrapped < 0.0 ? wrapped + 4.0 : wrapped);
        constexpr double kCos[4]{1.0, 0.0, -1.0, 0.0};
        constexpr double kSin[4]{0.0, 1.0, 0.0, -1.0};
        c = kCos[turn];
        s = kSin[turn];
        return;
    }
    const double radians = degrees * std::numbers::pi / 180.0;
    c = std::cos(radians);
    s = std::sin(radians);
}

AffineTransform aroundPivot(AffineTransform t, Vec3 pivot) {
    const DVec3 p = toDouble(pivot);
    t.translation = p - linear(t, p);
    return t;
}

} // namespace

AffineTransform AffineTransform::moveBy(Vec3 offset) {
    AffineTransform t;
    t.translation = toDouble(offset);
    return t;
}

AffineTransform AffineTransform::rotation(std::size_t axis, double degrees, Vec3 pivot) {
    double c = 1.0;
    double s = 0.0;
    cosSin(degrees, c, s);
    AffineTransform t;
    if (axis == 0) {
        t.yAxis = {0.0, c, s};
        t.zAxis = {0.0, -s, c};
    } else if (axis == 1) {
        t.xAxis = {c, 0.0, -s};
        t.zAxis = {s, 0.0, c};
    } else {
        t.xAxis = {c, s, 0.0};
        t.yAxis = {-s, c, 0.0};
    }
    return aroundPivot(t, pivot);
}

AffineTransform AffineTransform::scale(DVec3 factors, Vec3 pivot) {
    AffineTransform t;
    t.xAxis = {factors.x, 0.0, 0.0};
    t.yAxis = {0.0, factors.y, 0.0};
    t.zAxis = {0.0, 0.0, factors.z};
    return aroundPivot(t, pivot);
}

AffineTransform AffineTransform::boxToBox(const Aabb& from, const Aabb& to) {
    const DVec3 fromSize = toDouble(from.max) - toDouble(from.min);
    const DVec3 toSize = toDouble(to.max) - toDouble(to.min);
    // A flat source (a lone entity has no size) can only move along that axis.
    const auto factor = [](double toLength, double fromLength) {
        return fromLength > 0.0 ? toLength / fromLength : 1.0;
    };
    AffineTransform t;
    t.xAxis = {factor(toSize.x, fromSize.x), 0.0, 0.0};
    t.yAxis = {0.0, factor(toSize.y, fromSize.y), 0.0};
    t.zAxis = {0.0, 0.0, factor(toSize.z, fromSize.z)};
    t.translation = toDouble(to.min) - linear(t, toDouble(from.min));
    return t;
}

Vec3 AffineTransform::apply(Vec3 p) const {
    return toFloat(linear(*this, toDouble(p)) + translation);
}

double AffineTransform::determinant() const { return dot(xAxis, cross(yAxis, zAxis)); }

AffineTransform AffineTransform::then(const AffineTransform& next) const {
    AffineTransform t;
    t.xAxis = linear(next, xAxis);
    t.yAxis = linear(next, yAxis);
    t.zAxis = linear(next, zAxis);
    t.translation = linear(next, translation) + next.translation;
    return t;
}

std::optional<Plane> transformedPlane(const Plane& plane, const AffineTransform& transform) {
    const double det = transform.determinant();
    if (!(std::fabs(det) > kFlatDeterminant)) {
        return std::nullopt;
    }
    // Normals transform by the inverse transpose, whose columns are these cross products / det.
    // It keeps dot(normal, point) <= distance meaning "inside" even through a mirror.
    const DVec3 a = transform.xAxis;
    const DVec3 b = transform.yAxis;
    const DVec3 c = transform.zAxis;
    const DVec3 n = toDouble(plane.normal);
    const DVec3 m = (cross(b, c) * n.x + cross(c, a) * n.y + cross(a, b) * n.z) * (1.0 / det);
    const double length = std::sqrt(dot(m, m));
    if (!(length > 0.0)) {
        return std::nullopt;
    }
    const DVec3 normal = m * (1.0 / length);
    // The point of the plane nearest the origin, moved by the transform, is on the new plane.
    const DVec3 onPlane = linear(transform, n * plane.distance) + transform.translation;
    return Plane{toFloat(normal), static_cast<float>(dot(normal, onPlane))};
}

std::optional<Brush> transformedBrush(const Brush& brush, const AffineTransform& transform) {
    Brush result;
    result.planes.reserve(brush.planes.size());
    for (const Plane& plane : brush.planes) {
        const std::optional<Plane> moved = transformedPlane(plane, transform);
        if (!moved) {
            return std::nullopt;
        }
        result.planes.push_back(*moved);
    }
    std::string problem;
    if (!checkBrush(result, problem)) {
        return std::nullopt;
    }
    return result;
}

LevelEntity transformedEntity(const LevelEntity& entity, const AffineTransform& transform) {
    const std::optional<Vec3> origin = entityOrigin(entity);
    if (!origin) {
        return entity;
    }
    LevelEntity result = entity;
    setProperty(result, "origin", formatVec3(transform.apply(*origin)));

    float yaw = 0.0f;
    const std::string* yawText = findProperty(entity, "yaw");
    if (yawText != nullptr && parseFloat(*yawText, yaw)) {
        double c = 1.0;
        double s = 0.0;
        cosSin(yaw, c, s);
        const DVec3 facing = linear(transform, {c, s, 0.0});
        // Turned to face straight up or down, the old yaw is as good as any.
        if (std::hypot(facing.x, facing.y) > 1e-9) {
            const double degrees = std::atan2(facing.y, facing.x) * 180.0 / std::numbers::pi;
            setProperty(result, "yaw", formatFloat(static_cast<float>(degrees)));
        }
    }
    return result;
}

double snapAngle(double degrees, double step) { return std::round(degrees / step) * step; }

} // namespace ct
