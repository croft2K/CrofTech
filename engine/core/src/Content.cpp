#include "croftech/core/Content.h"

#include <algorithm>
#include <format>
#include <system_error>

namespace ct {

std::optional<std::filesystem::path>
ContentPaths::find(const std::filesystem::path& relative) const {
    for (auto root = m_roots.rbegin(); root != m_roots.rend(); ++root) {
        std::error_code error;
        std::filesystem::path candidate = *root / relative;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return std::nullopt;
}

LayeredConfig::LayeredConfig(const ContentPaths& paths, const std::filesystem::path& relative) {
    for (const std::filesystem::path& root : paths.layers()) {
        m_candidates.push_back({root / relative, std::nullopt, 0});
    }
}

std::optional<std::vector<LayeredConfig::Layer>> LayeredConfig::reloadIfChanged() {
    bool changed = !m_loaded;
    for (Candidate& candidate : m_candidates) {
        std::error_code error;
        std::optional<std::filesystem::file_time_type> writeTime;
        std::uintmax_t size = 0;
        if (std::filesystem::is_regular_file(candidate.path, error)) {
            const auto time = std::filesystem::last_write_time(candidate.path, error);
            if (!error) {
                writeTime = time;
                size = std::filesystem::file_size(candidate.path, error);
            }
        }
        if (writeTime != candidate.writeTime || size != candidate.size) {
            changed = true;
            candidate.writeTime = writeTime;
            candidate.size = size;
        }
    }
    if (!changed) {
        return std::nullopt;
    }

    std::vector<Layer> layers;
    for (Candidate& candidate : m_candidates) {
        if (!candidate.writeTime) {
            continue;
        }
        const std::optional<std::string> text = readTextFile(candidate.path);
        if (!text) {
            // Probably caught mid-save; forget its timestamp so the next call retries.
            candidate.writeTime.reset();
            return std::nullopt;
        }
        layers.push_back({candidate.path, parseConfig(*text)});
    }
    m_loaded = true;
    return layers;
}

std::optional<GameManifest> loadGameManifest(const std::filesystem::path& gamesDirectory,
                                             std::string_view name, std::string& error) {
    const auto validName = [](std::string_view text) {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        });
    };
    if (!validName(name)) {
        error = std::format("invalid game name '{}'", name);
        return std::nullopt;
    }

    GameManifest manifest;
    manifest.name = std::string(name);
    manifest.title = manifest.name;
    manifest.root = gamesDirectory / manifest.name;
    const std::optional<std::string> text = readTextFile(manifest.root / "game.cfg");
    if (!text) {
        std::filesystem::path expected = manifest.root / "game.cfg";
        error = std::format("no game '{}' (expected {})", name, expected.make_preferred().string());
        return std::nullopt;
    }
    for (const ConfigEntry& entry : parseConfig(*text)) {
        if (entry.key == "title" && !entry.value.empty()) {
            manifest.title = entry.value;
        } else if (entry.key == "start_map") {
            manifest.startMap = entry.value;
        } else if (entry.key == "weapons" || entry.key == "enemies") {
            const bool weapons = entry.key == "weapons";
            std::vector<std::string>& names = weapons ? manifest.weapons : manifest.enemies;
            names.clear();
            std::size_t start = 0;
            while (start < entry.value.size()) {
                const std::size_t end =
                    std::min(entry.value.find_first_of(" 	", start), entry.value.size());
                const std::string_view item =
                    std::string_view(entry.value).substr(start, end - start);
                if (!item.empty()) {
                    if (!validName(item)) {
                        error = std::format("game.cfg: line {}: invalid {} name '{}'", entry.line,
                                            weapons ? "weapon" : "enemy", item);
                        return std::nullopt;
                    }
                    names.emplace_back(item);
                }
                start = end + 1;
            }
        }
    }
    return manifest;
}

} // namespace ct
