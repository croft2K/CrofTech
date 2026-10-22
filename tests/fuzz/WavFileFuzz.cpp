// Fuzzes reading WAV files. Whatever decodes must be mono or stereo, at a supported rate, with
// whole frames of finite samples from -1 to 1.

#include "croftech/audio/WavFile.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::string error;
    const std::optional<ct::Sound> sound =
        ct::parseWav(std::as_bytes(std::span{data, size}), error);
    if (!sound) {
        return 0;
    }
    if (sound->channels < 1 || sound->channels > 2 || sound->sampleRate < ct::kMinSampleRate ||
        sound->sampleRate > ct::kMaxSampleRate || sound->samples.size() % sound->channels != 0) {
        std::abort();
    }
    for (const float sample : sound->samples) {
        if (!(sample >= -1.0f && sample <= 1.0f)) {
            std::abort();
        }
    }
    return 0;
}
