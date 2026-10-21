#include "croftech/level/LevelLoad.h"

#include "croftech/level/LevelFile.h"
#include "croftech/level/MapImport.h"

#include <format>

namespace ct {

std::optional<std::filesystem::path> findLevelFile(const ContentPaths& content,
                                                   std::string_view name, std::string& error) {
    const std::filesystem::path path(name);
    if (path.has_extension()) {
        return path;
    }
    const std::filesystem::path base = std::filesystem::path("levels") / path;
    for (const std::string_view extension : {kLevelFileExtension, kMapFileExtension}) {
        if (auto found = content.find(std::filesystem::path(base).concat(extension))) {
            return found;
        }
    }
    error = std::format("no level '{}' (looked for levels/{}{} and levels/{}{})", name, name,
                        kLevelFileExtension, name, kMapFileExtension);
    return std::nullopt;
}

std::optional<Level> loadAnyLevelFile(const std::filesystem::path& path,
                                      std::vector<std::string>& warnings, std::string& error) {
    if (path.extension() == kMapFileExtension) {
        return importMapFile(path, {}, warnings, error);
    }
    return loadLevelFile(path, error);
}

} // namespace ct
