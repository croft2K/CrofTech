// Fuzzes the level file parser. Anything it accepts must save and load back unchanged.

#include "croftech/level/LevelFile.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    std::string error;
    const auto level = ct::parseLevel(text, error);
    if (!level) {
        return 0;
    }
    const auto saved = ct::formatLevel(*level, error);
    if (!saved || ct::parseLevel(*saved, error) != level) {
        std::abort();
    }
    return 0;
}
