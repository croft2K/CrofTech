#pragma once

#include "croftech/core/Content.h"
#include "croftech/level/LevelLoad.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace ct {

// File > Open: the levels of the content folders by name, filtered as you type, or any .level or
// .map file through the system's file dialog.
class OpenLevelDialog {
public:
    explicit OpenLevelDialog(SDL_Window* window) : m_window(window) {}
    OpenLevelDialog(const OpenLevelDialog&) = delete;
    OpenLevelDialog& operator=(const OpenLevelDialog&) = delete;

    // Lists the levels again and shows the dialog.
    void show(const ContentPaths& content);
    // Draws it while shown. Returns the file chosen, once.
    std::optional<std::filesystem::path> draw();

private:
    static void browsed(void* self, const char* const* files, int filter);

    SDL_Window* m_window;
    std::vector<LevelFileListing> m_levels;
    std::filesystem::path m_folder; // where browsing starts
    std::string m_filter;
    std::size_t m_selected = 0;
    bool m_show = false;
    // The system dialog answers on a thread of its own.
    std::mutex m_mutex;
    std::optional<std::filesystem::path> m_browsed;
};

} // namespace ct
