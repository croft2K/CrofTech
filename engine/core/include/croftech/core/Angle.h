#pragma once

#include "croftech/core/Math.h"

#include <cstdint>

namespace ct {

// Simulation angles are 16-bit fractions of a turn (65536 units = 360 degrees), the same precision
// the client sends in its input commands. Yaw is unsigned; pitch is signed and spans +-16384.
inline constexpr std::int32_t kAngleUnitsPerTurn = 65536;
inline constexpr std::int16_t kMaxPitch = 16000; // just under 90 degrees, avoids a degenerate view

struct SinCos {
    float sin = 0.0f;
    float cos = 1.0f;
};

// Polynomial sine and cosine using only + and *, so results are bit-identical on every platform
// and compiler (std::sin/std::cos are not). Error is below 1e-7 over the whole circle.
constexpr SinCos sinCos(std::uint16_t angle) noexcept {
    constexpr float kRadiansPerQuarterUnit = (kPi * 0.5f) / 16384.0f;
    const std::uint32_t quadrant = static_cast<std::uint32_t>(angle) >> 14;
    const float x = static_cast<float>(angle & 0x3FFFu) * kRadiansPerQuarterUnit;
    const float x2 = x * x;
    const float s =
        x * (1.0f + x2 * (-1.0f / 6.0f +
                          x2 * (1.0f / 120.0f +
                                x2 * (-1.0f / 5040.0f +
                                      x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
    const float c =
        1.0f +
        x2 * (-0.5f + x2 * (1.0f / 24.0f +
                            x2 * (-1.0f / 720.0f +
                                  x2 * (1.0f / 40320.0f +
                                        x2 * (-1.0f / 3628800.0f + x2 * (1.0f / 479001600.0f))))));
    switch (quadrant) {
    case 0:
        return {s, c};
    case 1:
        return {c, -s};
    case 2:
        return {-s, -c};
    default:
        return {-c, s};
    }
}

constexpr SinCos sinCos(std::int16_t angle) noexcept {
    return sinCos(static_cast<std::uint16_t>(angle));
}

// Conversions for presentation code (camera, editor). Not for use inside the simulation.
constexpr float angleToRadians(std::int32_t units) noexcept {
    return static_cast<float>(units) * (2.0f * kPi / static_cast<float>(kAngleUnitsPerTurn));
}

constexpr std::int32_t radiansToAngle(float radians) noexcept {
    return static_cast<std::int32_t>(radians *
                                     (static_cast<float>(kAngleUnitsPerTurn) / (2.0f * kPi)));
}

} // namespace ct
