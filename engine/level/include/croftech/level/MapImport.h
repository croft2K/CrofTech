#pragma once

#include "croftech/level/Level.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// Imports .map files, the text format TrenchBroom and other Quake-family editors save, so levels
// can be blocked out in TrenchBroom until the CrofTech editor has brush tools. Reads the standard
// and Valve 220 face formats; texture names and alignment are ignored until there are materials.
//
// Brushes of worldspawn and func_group become world brushes. Point entities keep their class and
// properties, with "origin" converted to metres and "angle" renamed to "yaw". Quake's
// info_player_start and info_player_deathmatch become player_spawn. Brush entities (doors and
// other movers), patches and brush primitives are skipped with a warning until the engine
// supports them, as are brushes that are not closed solids.
inline constexpr std::string_view kMapFileExtension = ".map";
inline constexpr std::size_t kMaxImportWarnings = 50;

struct MapImportOptions {
    // Quake units are inches by convention, and the default movement tuning keeps Quake's sizes,
    // so a map built for a Quake-sized player plays at the scale it was built for.
    double metresPerUnit = 0.0254;
};

// error and warnings are "line N: problem". Syntax errors fail the import; problems with single
// brushes or entities only skip them. Warnings stop after kMaxImportWarnings with a count of the
// rest.
std::optional<Level> importMap(std::string_view text, const MapImportOptions& options,
                               std::vector<std::string>& warnings, std::string& error);

std::optional<Level> importMapFile(const std::filesystem::path& path,
                                   const MapImportOptions& options,
                                   std::vector<std::string>& warnings, std::string& error);

} // namespace ct
