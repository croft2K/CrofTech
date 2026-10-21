#pragma once

#include "croftech/sim/SaveGame.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ct {

// Save games on disk, as <name>.save in the user's own folder for the game (SDL's pref path, under
// saves/), never in the game's content. Names are 1 to 64 letters, digits, '_' and '-', so a name
// typed at the console can't reach outside the folder.
class SaveFiles {
public:
    // A file bigger than this isn't read: saves are untrusted, and states are far smaller.
    static constexpr std::uintmax_t kMaxSaveFileBytes = 64u << 20;

    explicit SaveFiles(const std::string& gameName);

    static bool isValidName(std::string_view name);

    // Writes a temporary file and renames it over the save, so a failed save never leaves a
    // half-written one behind.
    bool write(std::string_view name, const SaveGame& save, std::string& error) const;
    std::optional<SaveGame> read(std::string_view name, std::string& error) const;
    // The save written most recently, if there is one.
    std::optional<std::string> newest() const;

    const std::filesystem::path& directory() const { return m_directory; }

private:
    std::filesystem::path m_directory;
};

} // namespace ct
