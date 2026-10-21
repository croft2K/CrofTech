#pragma once

#include "croftech/core/Content.h"
#include "croftech/level/Level.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// Finds a level by name through the content layers: levels/<name>.level, then levels/<name>.map.
// A name with a file extension is taken as a path and returned as it is.
std::optional<std::filesystem::path> findLevelFile(const ContentPaths& content,
                                                   std::string_view name, std::string& error);

// Loads a .level file, or imports a .map file (warnings collects what the import skipped).
std::optional<Level> loadAnyLevelFile(const std::filesystem::path& path,
                                      std::vector<std::string>& warnings, std::string& error);

} // namespace ct
