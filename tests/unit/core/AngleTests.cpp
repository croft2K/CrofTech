#include "croftech/core/Angle.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

TEST_CASE("sinCos matches the standard library to within 1e-6 over the whole circle") {
    float worst = 0.0f;
    for (std::uint32_t a = 0; a < 65536; ++a) {
        const ct::SinCos sc = ct::sinCos(static_cast<std::uint16_t>(a));
        const double radians = static_cast<double>(a) * (2.0 * 3.14159265358979323846 / 65536.0);
        worst = std::fmax(worst, static_cast<float>(std::fabs(sc.sin - std::sin(radians))));
        worst = std::fmax(worst, static_cast<float>(std::fabs(sc.cos - std::cos(radians))));
    }
    CHECK(worst < 1e-6f);
}

TEST_CASE("sinCos is exact at the quarter turns") {
    CHECK(ct::sinCos(std::uint16_t{0}).sin == 0.0f);
    CHECK(ct::sinCos(std::uint16_t{0}).cos == 1.0f);
    CHECK(ct::sinCos(std::uint16_t{16384}).sin == 1.0f);
    CHECK(ct::sinCos(std::uint16_t{32768}).cos == -1.0f);
    CHECK(ct::sinCos(std::uint16_t{49152}).sin == -1.0f);
}

TEST_CASE("signed angles wrap to the same point as unsigned ones") {
    const ct::SinCos negative = ct::sinCos(std::int16_t{-16384});
    const ct::SinCos positive = ct::sinCos(std::uint16_t{49152});
    CHECK(negative.sin == positive.sin);
    CHECK(negative.cos == positive.cos);
}

TEST_CASE("directionToAngle matches atan2 to within an angle unit all the way round") {
    std::int32_t worst = 0;
    for (std::uint32_t a = 0; a < 65536; a += 7) {
        const double radians = static_cast<double>(a) * (2.0 * 3.14159265358979323846 / 65536.0);
        for (const double distance : {0.01, 1.0, 1000.0}) {
            const auto x = static_cast<float>(std::cos(radians) * distance);
            const auto y = static_cast<float>(std::sin(radians) * distance);
            const std::int32_t found = ct::directionToAngle(x, y);
            std::int32_t error = std::abs(found - static_cast<std::int32_t>(a));
            error = std::min(error, 65536 - error);
            worst = std::max(worst, error);
        }
    }
    CHECK(worst <= 1);
    CHECK(ct::directionToAngle(1.0f, 0.0f) == 0);
    CHECK(ct::directionToAngle(0.0f, 1.0f) == 16384);
    CHECK(ct::directionToAngle(-1.0f, 0.0f) == 32768);
    CHECK(ct::directionToAngle(0.0f, -1.0f) == 49152);
    CHECK(ct::directionToAngle(1.0f, -1e-9f) == 0);
    CHECK(ct::directionToAngle(0.0f, 0.0f) == 0);
}

TEST_CASE("pitchToward looks up and down, no further than the pitch limit") {
    CHECK(ct::pitchToward(1.0f, 0.0f) == 0);
    CHECK(ct::pitchToward(1.0f, 1.0f) == 8192);
    CHECK(ct::pitchToward(-1.0f, -1.0f) == -8192);
    CHECK(ct::pitchToward(0.0f, 5.0f) == ct::kMaxPitch);
    CHECK(ct::pitchToward(0.0f, -5.0f) == -ct::kMaxPitch);
}
