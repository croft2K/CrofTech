#pragma once

#include <cstdint>

namespace ct {

// Counter-based random numbers for the simulation. A number depends only on its counter, which
// lives in the state, so replays, rewinds and every platform draw the same ones. Each draw takes
// the counter and moves it on.

// An integer hash that spreads every bit of the counter across the result.
constexpr std::uint32_t randomBits(std::uint32_t counter) noexcept {
    std::uint32_t x = counter;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// A float in [0, 1), from the top 24 bits so every value is exact.
constexpr float randomUnit(std::uint32_t& counter) noexcept {
    return static_cast<float>(randomBits(counter++) >> 8) * (1.0f / 16777216.0f);
}

} // namespace ct
