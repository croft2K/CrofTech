#include "croftech/level/LevelLoad.h"

#include "croftech/level/LevelFile.h"
#include "croftech/level/MapImport.h"

#include <algorithm>
#include <format>
#include <system_error>

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

std::vector<LevelFileListing> listLevelFiles(const ContentPaths& content) {
    std::vector<std::string> names;
    for (const std::filesystem::path& root : content.layers()) {
        std::error_code error;
        std::filesystem::directory_iterator it(root / "levels", error);
        for (; !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
            const std::filesystem::path& path = it->path();
            if ((path.extension() == kLevelFileExtension ||
                 path.extension() == kMapFileExtension) &&
                it->is_regular_file(error)) {
                names.push_back(path.stem().string());
            }
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::vector<LevelFileListing> levels;
    for (std::string& name : names) {
        std::string error;
        if (std::optional<std::filesystem::path> path = findLevelFile(content, name, error)) {
            levels.push_back({std::move(name), std::move(*path)});
        }
    }
    return levels;
}

std::optional<Level> loadAnyLevelFile(const std::filesystem::path& path,
                                      std::vector<std::string>& warnings, std::string& error) {
    if (path.extension() == kMapFileExtension) {
        return importMapFile(path, {}, warnings, error);
    }
    return loadLevelFile(path, error);
}

} // namespace ct
