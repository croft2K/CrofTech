#pragma once

#include <algorithm>
#include <cmath>

namespace ct {

inline constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 a) noexcept { return {-a.x, -a.y, -a.z}; }
constexpr Vec3 operator*(Vec3 a, float s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 operator*(float s, Vec3 a) noexcept { return a * s; }
constexpr Vec3 operator/(Vec3 a, float s) noexcept { return {a.x / s, a.y / s, a.z / s}; }

constexpr Vec3& operator+=(Vec3& a, Vec3 b) noexcept { return a = a + b; }
constexpr Vec3& operator-=(Vec3& a, Vec3 b) noexcept { return a = a - b; }
constexpr Vec3& operator*=(Vec3& a, float s) noexcept { return a = a * s; }

constexpr float dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// sqrt is correctly rounded under IEEE 754, so this is deterministic across platforms.
inline float length(Vec3 v) noexcept { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(Vec3 v) noexcept {
    const float len = length(v);
    return len > 0.0f ? v / len : Vec3{};
}

constexpr Vec3 min(Vec3 a, Vec3 b) noexcept {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

constexpr Vec3 max(Vec3 a, Vec3 b) noexcept {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

struct Aabb {
    Vec3 min;
    Vec3 max;
};

constexpr bool overlaps(const Aabb& a, const Aabb& b) noexcept {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

} // namespace ct
