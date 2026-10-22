#pragma once

#include "croftech/audio/Sound.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace ct {

// WAV files (RIFF WAVE): integer PCM of 8, 16, 24 or 32 bits or 32-bit float, in the plain or the
// extensible format, mono or stereo, from 8 to 192 kHz. Sound files are content, which can come
// from mods and downloaded maps, so they are untrusted: anything malformed or past the limits is
// rejected with an error (and fuzzed).
inline constexpr std::size_t kMaxWavBytes = std::size_t{64} << 20;
inline constexpr std::size_t kMaxSoundSamples = std::size_t{1} << 25;
inline constexpr std::uint32_t kMinSampleRate = 8000;
inline constexpr std::uint32_t kMaxSampleRate = 192000;

std::optional<Sound> parseWav(std::span<const std::byte> bytes, std::string& error);

} // namespace ct
