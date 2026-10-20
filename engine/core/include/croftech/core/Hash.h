#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ct {

inline constexpr std::uint64_t kFnv1aOffsetBasis = 0xcbf29ce484222325ull;
inline constexpr std::uint64_t kFnv1aPrime = 0x100000001b3ull;

// 64-bit FNV-1a. The result depends only on the input bytes, so it is stable across builds and
// platforms and can be stored or compared: per-tick state hashes, compiled level hashes.
// Pass a previous result as the seed to hash several buffers as one stream.
constexpr std::uint64_t hashBytes(std::span<const std::byte> bytes,
                                  std::uint64_t seed = kFnv1aOffsetBasis) noexcept {
    std::uint64_t hash = seed;
    for (const std::byte b : bytes) {
        hash ^= static_cast<std::uint64_t>(b);
        hash *= kFnv1aPrime;
    }
    return hash;
}

constexpr std::uint64_t hashString(std::string_view text,
                                   std::uint64_t seed = kFnv1aOffsetBasis) noexcept {
    std::uint64_t hash = seed;
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= kFnv1aPrime;
    }
    return hash;
}

} // namespace ct
