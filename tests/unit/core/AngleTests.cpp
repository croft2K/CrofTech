#include "croftech/core/Angle.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>

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
