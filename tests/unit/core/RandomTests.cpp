#include "croftech/core/Random.h"

#include <doctest/doctest.h>

#include <cstdint>

TEST_CASE("random numbers depend only on the counter, which each draw moves on") {
    // Fixed values: a change here would change every replay and desync servers from clients.
    static_assert(ct::randomBits(0) == 0);
    static_assert(ct::randomBits(1) == 0x688990c0u);
    std::uint32_t a = 41;
    std::uint32_t b = 41;
    const float first = ct::randomUnit(a);
    CHECK(a == 42);
    CHECK(ct::randomUnit(b) == first);
    CHECK(ct::randomUnit(a) != first);
}

TEST_CASE("random units cover [0, 1) evenly") {
    std::uint32_t counter = 1;
    int low = 0;
    float smallest = 1.0f;
    float largest = 0.0f;
    for (int i = 0; i < 10000; ++i) {
        const float value = ct::randomUnit(counter);
        REQUIRE(value >= 0.0f);
        REQUIRE(value < 1.0f);
        low += value < 0.5f ? 1 : 0;
        smallest = value < smallest ? value : smallest;
        largest = value > largest ? value : largest;
    }
    CHECK(low > 4800);
    CHECK(low < 5200);
    CHECK(smallest < 0.001f);
    CHECK(largest > 0.999f);
}
