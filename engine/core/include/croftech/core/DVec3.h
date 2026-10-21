#pragma once

#include "croftech/core/Math.h"

#include <cmath>

namespace ct {

// Double-precision vector for geometry that has to stay exact: brush building and editing work in
// doubles and round to float once, when a result is stored. Coordinates on the editor's grid
// (multiples of 1/32 m inside the world) need at most 17 bits, so their differences, products
// and cross products are exact in a double.
struct DVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend constexpr bool operator==(const DVec3&, const DVec3&) = default;
};

constexpr DVec3 operator+(DVec3 a, DVec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr DVec3 operator-(DVec3 a, DVec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr DVec3 operator-(DVec3 a) noexcept { return {-a.x, -a.y, -a.z}; }
constexpr DVec3 operator*(DVec3 a, double s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
constexpr DVec3& operator+=(DVec3& a, DVec3 b) noexcept { return a = a + b; }

constexpr double dot(DVec3 a, DVec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr DVec3 cross(DVec3 a, DVec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double length(DVec3 v) noexcept { return std::sqrt(dot(v, v)); }

constexpr DVec3 toDouble(Vec3 v) noexcept { return {v.x, v.y, v.z}; }

constexpr Vec3 toFloat(DVec3 v) noexcept {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

} // namespace ct
