#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ct {

// A save game file: which level it was saved in, and the simulation state (StateSave bytes). The
// level is named as the map command takes it, with a hash of its content, so a save of a level
// that has since been edited is refused instead of restoring entities that no longer match.
//
// Files are untrusted: readSaveGame checks the header and lengths, and the state is checked by
// loadState when it is used.
inline constexpr std::uint32_t kSaveGameVersion = 1;
inline constexpr std::size_t kMaxSaveLevelName = 4096;

struct SaveGame {
    std::string level;
    std::uint64_t levelHash = 0;
    std::vector<std::byte> state;

    friend bool operator==(const SaveGame&, const SaveGame&) = default;
};

std::vector<std::byte> writeSaveGame(const SaveGame& save);
// On failure save is left unchanged and error says why.
bool readSaveGame(std::span<const std::byte> bytes, SaveGame& save, std::string& error);

} // namespace ct
