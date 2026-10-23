#pragma once

#include "croftech/core/Content.h"
#include "croftech/level/Level.h"
#include "croftech/script/ScriptProgram.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ct {

// The scripts a level's script entities name, each once and in name order, read from
// scripts/<name>.luau through the content layers. A name that isn't valid, or a file that is
// missing or too big, is left out with a warning; building the level then warns about the
// entities that have no script.
std::vector<ScriptSource> loadLevelScripts(const ContentPaths& content, const Level& level,
                                           std::vector<std::string>& warnings);

// Of the names and texts, continuing from the seed: saves record the level's hash continued with
// its scripts, so a save made with other scripts isn't loaded into these. No scripts leave the
// seed as it is.
std::uint64_t scriptSourcesHash(std::span<const ScriptSource> scripts, std::uint64_t seed);

} // namespace ct
