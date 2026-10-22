#pragma once

#include <cstdint>
#include <vector>

namespace ct {

// Decoded audio: samples from -1 to 1, interleaved when there are two channels.
struct Sound {
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 1; // 1 or 2
    std::vector<float> samples;

    std::size_t frameCount() const { return channels == 0 ? 0 : samples.size() / channels; }
};

} // namespace ct
