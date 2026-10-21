#pragma once

#include "croftech/level/Level.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ct {

// The level file: the editable source of a level, as line-based text so version control can diff
// and merge it. Shipped games will cook levels into packed binary files; this stays the source.
//
//     croftech_level 2
//     # Lines starting with '#' are comments.
//     brush
//         vertex <x> <y> <z>
//         ...
//         face <vertex number> <vertex number> <vertex number> ...
//         ...
//     end
//     entity <class>
//         <key> <value, the rest of the line>
//         ...
//     end
//
// Vertices are numbered from 0 in the order they appear; each face lists its corners
// counter-clockwise seen from outside. Numbers are written in their shortest exact form, so saving
// and loading never changes a value. Indentation is optional. Loading rejects anything malformed
// or outside the limits in Level.h.
//
// Version 1 files, whose brushes were "plane <normal x> <normal y> <normal z> <distance>" lines,
// still load; their planes become corners and saving writes version 2.
inline constexpr std::string_view kLevelFileExtension = ".level";
inline constexpr int kLevelFileVersion = 2;
inline constexpr std::size_t kMaxLevelFileBytes = 64u << 20;

// error is "line N: problem".
std::optional<Level> parseLevel(std::string_view text, std::string& error);

// Fails if the level breaks a limit or rule in Level.h, so every saved file loads again.
std::optional<std::string> formatLevel(const Level& level, std::string& error);

std::optional<Level> loadLevelFile(const std::filesystem::path& path, std::string& error);

// Writes a temporary file and renames it over the target, so a failed save never leaves a
// half-written level behind.
bool saveLevelFile(const Level& level, const std::filesystem::path& path, std::string& error);

} // namespace ct
