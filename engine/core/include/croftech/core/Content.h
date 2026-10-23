#pragma once

#include "croftech/core/ConfigFile.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// Content comes in layers: the engine's content folder first, then the game's (later, a mod's).
// A later layer overrides an earlier one: whole files for assets, single settings for config files.
class ContentPaths {
public:
    void addLayer(std::filesystem::path root) { m_roots.push_back(std::move(root)); }
    const std::vector<std::filesystem::path>& layers() const { return m_roots; }

    // The copy of a file from the highest layer that has one.
    std::optional<std::filesystem::path> find(const std::filesystem::path& relative) const;

private:
    std::vector<std::filesystem::path> m_roots;
};

// One config file read through every content layer, and re-read when any layer's copy is added,
// removed or changed.
class LayeredConfig {
public:
    struct Layer {
        std::filesystem::path path;
        std::vector<ConfigEntry> entries;
    };

    LayeredConfig(const ContentPaths& paths, const std::filesystem::path& relative);

    // On the first call and after any change: the entries of every layer that has the file, lowest
    // layer first, so applying them in order lets later layers win. Otherwise nullopt.
    std::optional<std::vector<Layer>> reloadIfChanged();

private:
    struct Candidate {
        std::filesystem::path path;
        std::optional<std::filesystem::file_time_type> writeTime;
        std::uintmax_t size = 0;
    };

    std::vector<Candidate> m_candidates;
    bool m_loaded = false;
};

struct GameManifest {
    std::string name;     // folder name under games/
    std::string title;    // shown to players
    std::string startMap; // level loaded when none is named on the command line
    // In number-key order; each is the file weapons/<name>.cfg in the content.
    std::vector<std::string> weapons;
    // Each is the file enemies/<name>.cfg in the content.
    std::vector<std::string> enemies;
    std::filesystem::path root;
};

// Whether the name can be a game's, a weapon's or an enemy's: they are parts of file paths, so they
// are limited to lowercase letters, digits, '-' and '_'.
bool isContentName(std::string_view name);

// Reads games/<name>/game.cfg.
std::optional<GameManifest> loadGameManifest(const std::filesystem::path& gamesDirectory,
                                             std::string_view name, std::string& error);
// The same from the file's text, for the game in root (games/<name>).
std::optional<GameManifest> parseGameManifest(std::string_view text,
                                              const std::filesystem::path& root,
                                              std::string& error);

} // namespace ct
