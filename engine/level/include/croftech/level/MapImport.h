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
// Brushes of worldspawn, func_group and func_detail become world brushes. Other entities keep
// their class, properties and brushes, with "origin" converted to metres, "angle" renamed to "yaw"
// and "targetname" to "name". Quake's info_player_start and info_player_deathmatch become
// player_spawn, trigger_once and trigger_multiple become trigger, and func_door becomes a mover
// that opens when touched, with its angle, lip and speed converted to a move and a speed in
// metres. Wiring ("target") is not converted. Patches, brush primitives and brushes that are not
// closed solids are skipped with a warning.
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
