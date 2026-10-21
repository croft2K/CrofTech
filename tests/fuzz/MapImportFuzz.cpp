// Fuzzes the TrenchBroom .map importer. Anything it imports must be a valid level that saves.

#include "croftech/level/LevelFile.h"
#include "croftech/level/MapImport.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    std::vector<std::string> warnings;
    std::string error;
    const auto level = ct::importMap(text, {}, warnings, error);
    if (!level) {
        return 0;
    }
    if (warnings.size() > ct::kMaxImportWarnings + 1 || !ct::formatLevel(*level, error)) {
        std::abort();
    }
    return 0;
}
