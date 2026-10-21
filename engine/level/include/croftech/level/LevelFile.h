#pragma once

#include "croftech/level/Level.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ct {

// The level file: the editable source of a level, as line-based text so version control can diff
// and merge it. Shipped games will cook levels into packed binary files; this stays the source.
//
//     croftech_level 4
//     # Lines starting with '#' are comments.
//     group <name, the rest of the line>
//     ...
//     brush
//         name <the rest of the line>
//         group <group name>
//         vertex <x> <y> <z>
//         ...
//         face <vertex number> <vertex number> <vertex number> ...
//         ...
//     end
//     entity <class>
//         group <group name>
//         <key> <value, the rest of the line>
//         ...
//         output <event> <target name> <input> <delay in seconds>
//         ...
//         brush
//             ...
//         end
//     end
//
// Brushes at the top level are world geometry; brushes inside an entity block belong to that
// entity. Groups are declared before anything uses them; brush names and groups are optional, and
// a brush inside an entity is in its entity's group, so it has no group line. Loading lists the
// world brushes first, then each entity's in entity order, as saving writes them. Vertices are
// numbered from 0 in the order they appear; each face lists its corners counter-clockwise seen from
// outside. Numbers are written in their shortest exact form, so saving and loading never changes a
// value. Indentation is optional. Loading rejects anything malformed or outside the limits in
// Level.h.
//
// Older files still load and are saved as version 4. Version 3 had no groups or brush names;
// version 2 had no outputs or entity brushes.
// Version 1 brushes were "plane <normal x> <normal y> <normal z> <distance>" lines; their planes
// become corners.
inline constexpr std::string_view kLevelFileExtension = ".level";
inline constexpr int kLevelFileVersion = 4;
inline constexpr std::size_t kMaxLevelFileBytes = 64u << 20;

// error is "line N: problem".
std::optional<Level> parseLevel(std::string_view text, std::string& error);

// Fails if the level breaks a limit or rule in Level.h, so every saved file loads again.
std::optional<std::string> formatLevel(const Level& level, std::string& error);

std::optional<Level> loadLevelFile(const std::filesystem::path& path, std::string& error);

// A hash of the level as its file would be written. Saves record it to tell whether the level
// has changed since; a level that can't be written hashes to 0.
std::uint64_t levelContentHash(const Level& level);

// Writes a temporary file and renames it over the target, so a failed save never leaves a
// half-written level behind.
bool saveLevelFile(const Level& level, const std::filesystem::path& path, std::string& error);

} // namespace ct
