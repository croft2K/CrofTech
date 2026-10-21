#pragma once

#include "croftech/core/ByteStream.h"
#include "croftech/sim/World.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace ct {

// Saves a simulation state to bytes and loads it back, for save games, checkpoints and test
// fixtures. Components are written field by field from their schemas, with each component type's
// field names and types in the header, so a save outlives changes to the components:
//
// - a field the save doesn't have keeps the component's default;
// - a field or component type the build doesn't know is skipped;
// - a field whose type changed fails the load.
//
// Loading treats the bytes as untrusted: every count and index is bounds-checked, entity
// references must resolve, floats must be finite and integers inside their schema ranges.
inline constexpr std::uint32_t kStateSaveVersion = 1;

std::vector<std::byte> saveState(const State& state);
// The same bytes, appended to writer.
void writeState(ByteWriter& writer, const State& state);

// On failure state is left unchanged and error says why.
bool loadState(std::span<const std::byte> bytes, State& state, std::string& error);

} // namespace ct
