#include "OpenLevelDialog.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace ct {
namespace {

bool containsIgnoringCase(std::string_view text, std::string_view part) {
    return std::search(text.begin(), text.end(), part.begin(), part.end(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           }) != text.end();
}

std::string shownPath(std::filesystem::path path) { return path.make_preferred().string(); }

} // namespace

void OpenLevelDialog::show(const ContentPaths& content) {
    m_levels = listLevelFiles(content);
    m_folder =
        content.layers().empty() ? std::filesystem::path() : content.layers().back() / "levels";
    m_filter.clear();
    m_selected = 0;
    m_show = true;
}

void OpenLevelDialog::browsed(void* self, const char* const* files, int) {
    // Null is an error, an empty list a cancel.
    if (files == nullptr || files[0] == nullptr) {
        return;
    }
    auto* dialog = static_cast<OpenLevelDialog*>(self);
    const std::string_view chosen = files[0];
    const std::lock_guard lock(dialog->m_mutex);
    dialog->m_browsed = std::filesystem::path(std::u8string(chosen.begin(), chosen.end()));
}

std::optional<std::filesystem::path> OpenLevelDialog::draw() {
    {
        const std::lock_guard lock(m_mutex);
        if (m_browsed) {
            std::optional<std::filesystem::path> chosen = std::move(m_browsed);
            m_browsed.reset();
            return chosen;
        }
    }
    if (m_show) {
        ImGui::OpenPopup("Open level");
        m_show = false;
    }
    std::optional<std::filesystem::path> chosen;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34.0f, ImGui::GetFontSize() * 26.0f),
                             ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Open level")) {
        return chosen;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool entered = ImGui::InputTextWithHint("##filter", "type to filter", &m_filter,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    std::vector<const LevelFileListing*> shown;
    for (const LevelFileListing& level : m_levels) {
        if (containsIgnoringCase(level.name, m_filter)) {
            shown.push_back(&level);
        }
    }
    // Up and down choose while typing.
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && m_selected + 1 < shown.size()) {
        ++m_selected;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && m_selected > 0) {
        --m_selected;
    }
    m_selected = shown.empty() ? 0 : std::min(m_selected, shown.size() - 1);
    const float buttons = ImGui::GetFrameHeightWithSpacing() * 1.5f;
    if (ImGui::BeginChild("levels", ImVec2(0.0f, -buttons), ImGuiChildFlags_Borders)) {
        for (std::size_t i = 0; i < shown.size(); ++i) {
            const bool selected = i == m_selected;
            if (ImGui::Selectable(shown[i]->name.c_str(), selected,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                m_selected = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    chosen = shown[i]->path;
                }
            }
            ImGui::SetItemTooltip("%s", shownPath(shown[i]->path).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", shownPath(shown[i]->path.filename()).c_str());
        }
        if (shown.empty()) {
            ImGui::TextDisabled("No level's name has that in it.");
        }
    }
    ImGui::EndChild();
    ImGui::BeginDisabled(shown.empty());
    if ((ImGui::Button("Open") || entered) && !shown.empty()) {
        chosen = shown[m_selected]->path;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        static constexpr SDL_DialogFileFilter kFilters[] = {{"Levels", "level;map"}};
        const std::string folder = m_folder.string();
        SDL_ShowOpenFileDialog(&OpenLevelDialog::browsed, this, m_window, kFilters, 1,
                               folder.empty() ? nullptr : folder.c_str(), false);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("Any .level or .map file, through the system's file dialog");
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
    }
    if (chosen) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return chosen;
}

} // namespace ct
