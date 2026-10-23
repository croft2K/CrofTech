#include "ScriptPanel.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/editor/LevelItem.h"
#include "croftech/level/EntityClasses.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <system_error>

namespace ct {
namespace {

constexpr ImVec4 kProblemColour{1.0f, 0.47f, 0.4f, 1.0f};
constexpr ImVec4 kRemarkColour{0.92f, 0.71f, 0.31f, 1.0f};
constexpr double kCheckDelay = 0.3; // seconds after the last keystroke

constexpr std::string_view kTemplate =
    R"(-- What this script does, and which entities' outputs reach it.

return {
    -- Kept between calls and saved with the game: numbers and true/false only.
    state = { count = 0 },

    -- Events it fires with game:fire, which its entity's outputs wire to other entities.
    events = { "on_done" },

    -- Inputs that other entities' outputs send it. Each gets the game.
    inputs = {
        trigger = function(game)
            game.state.count += 1
            game:fire("on_done")
        end,
    },
}
)";

std::string entityLabel(const Level& level, std::uint32_t index) {
    const std::string* name = findProperty(level.entities[index], "name");
    if (name != nullptr && !name->empty()) {
        return std::format("{} (entity {})", *name, index);
    }
    return std::format("{} (entity {})", level.entities[index].className, index);
}

// The error without the "name:line: " it starts with, which the line marks already say.
std::string_view errorMessage(std::string_view error, std::string_view name) {
    if (error.starts_with(name) && error.size() > name.size() && error[name.size()] == ':') {
        const std::size_t colon = error.find(": ", name.size() + 1);
        if (colon != std::string_view::npos) {
            return error.substr(colon + 2);
        }
    }
    return error;
}

std::optional<std::filesystem::file_time_type> writeTimeOf(const std::filesystem::path& path) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? std::nullopt : std::optional(time);
}

} // namespace

ScriptPanel::ScriptPanel(const ContentPaths& content, ScriptLibrary& library)
    : m_content(content), m_library(library) {}

ScriptPanel::Tab* ScriptPanel::findTab(std::string_view name) {
    for (const std::unique_ptr<Tab>& tab : m_tabs) {
        if (tab->name == name) {
            return tab.get();
        }
    }
    return nullptr;
}

void ScriptPanel::open(std::string_view name) {
    m_focusWindow = true;
    if (Tab* tab = findTab(name)) {
        tab->select = true;
        return;
    }
    const std::filesystem::path path = m_library.path(name);
    const std::optional<std::string> text = readTextFile(path, kMaxScriptBytes);
    if (path.empty() || !text) {
        m_message =
            std::format("Can't open script '{}': no scripts/{}.luau that can be read", name, name);
        return;
    }
    auto tab = std::make_unique<Tab>();
    tab->name = std::string(name);
    tab->buffer = TextBuffer(*text);
    tab->checkedVersion = tab->buffer.version();
    tab->fileTime = writeTimeOf(path);
    tab->select = true;
    tab->editor.focus();
    m_tabs.push_back(std::move(tab));
}

bool ScriptPanel::create(std::string_view name, std::string& error) {
    if (!isValidScriptName(name)) {
        error = "A script's name is lowercase letters, digits, '-' and '_'.";
        return false;
    }
    if (!m_library.path(name).empty()) {
        error = std::format("There is already a script named '{}'.", name);
        return false;
    }
    if (m_content.layers().empty()) {
        error = "There is no content folder to put it in.";
        return false;
    }
    const std::filesystem::path folder = m_content.layers().back() / "scripts";
    std::error_code made;
    std::filesystem::create_directories(folder, made);
    if (!writeTextFile(folder / std::format("{}.luau", name), kTemplate, error)) {
        return false;
    }
    m_library.refresh();
    open(name);
    return true;
}

bool ScriptPanel::save(Tab& tab, std::string& error) {
    const std::filesystem::path path = m_library.path(tab.name);
    if (path.empty()) {
        error = std::format("scripts/{}.luau is gone from the content folders", tab.name);
        return false;
    }
    if (!writeTextFile(path, tab.buffer.text(), error)) {
        return false;
    }
    tab.buffer.markSaved();
    tab.fileTime = writeTimeOf(path);
    // The library follows the file again; it compiles the same text on its next refresh.
    m_library.clearText(tab.name);
    return true;
}

bool ScriptPanel::saveAll(std::string& error) {
    for (const std::unique_ptr<Tab>& tab : m_tabs) {
        if (tab->buffer.modified() && !save(*tab, error)) {
            return false;
        }
    }
    return true;
}

bool ScriptPanel::hasKeyboard() const {
    return std::any_of(m_tabs.begin(), m_tabs.end(),
                       [](const std::unique_ptr<Tab>& tab) { return tab->editor.hasKeyboard(); });
}

bool ScriptPanel::hasUnsavedChanges() const { return !unsavedNames().empty(); }

std::vector<std::string> ScriptPanel::unsavedNames() const {
    std::vector<std::string> names;
    for (const std::unique_ptr<Tab>& tab : m_tabs) {
        if (tab->buffer.modified()) {
            names.push_back(tab->name);
        }
    }
    return names;
}

void ScriptPanel::applyOpenText(std::vector<ScriptSource>& scripts) const {
    for (const std::unique_ptr<Tab>& tab : m_tabs) {
        for (ScriptSource& script : scripts) {
            if (script.name == tab->name) {
                script.text = tab->buffer.text();
            }
        }
    }
}

// Unedited tabs follow their files when they change outside the editor.
void ScriptPanel::checkFiles() {
    const double now = ImGui::GetTime();
    if (now - m_filesCheckedAt < 1.0) {
        return;
    }
    m_filesCheckedAt = now;
    for (const std::unique_ptr<Tab>& tab : m_tabs) {
        const std::filesystem::path path = m_library.path(tab->name);
        const auto time = writeTimeOf(path);
        if (tab->buffer.modified() || !time || time == tab->fileTime) {
            continue;
        }
        if (const std::optional<std::string> text = readTextFile(path, kMaxScriptBytes)) {
            tab->buffer = TextBuffer(*text);
            tab->checkedVersion = tab->buffer.version();
            tab->fileTime = time;
            m_library.clearText(tab->name);
        }
    }
}

const ScriptLinks& ScriptPanel::linksOf(const Tab& tab, const EditorDocument& document) {
    if (m_linksScript != tab.name || m_linksLevel != document.changeCount() ||
        m_linksGeneration != m_library.generation()) {
        m_linksScript = tab.name;
        m_linksLevel = document.changeCount();
        m_linksGeneration = m_library.generation();
        m_links = scriptLinks(document.level(), tab.name, m_library.find(tab.name),
                              [this](std::string_view name) { return m_library.find(name); });
    }
    return m_links;
}

bool ScriptPanel::drawDetails(Tab& tab, EditorDocument& document) {
    const Level& level = document.level();
    bool frame = false;
    const std::filesystem::path path = m_library.path(tab.name);
    ImGui::SeparatorText(std::format("{}.luau", tab.name).c_str());
    ImGui::BeginDisabled(!tab.buffer.modified());
    if (ImGui::Button("Save")) {
        std::string error;
        if (!save(tab, error)) {
            m_message = error;
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Ctrl+S in the script");
    ImGui::SameLine();
    if (ImGui::Button("Open outside")) {
        const std::string url = "file:///" + path.generic_string();
        if (!SDL_OpenURL(url.c_str())) {
            m_message = std::format("Couldn't open {}: {}", path.string(), SDL_GetError());
        }
    }
    ImGui::SetItemTooltip("In the program that opens .luau files, such as VS Code with the Luau "
                          "extension; save here first");
    ImGui::TextDisabled("%s", path.string().c_str());

    const ScriptLinks& links = linksOf(tab, document);
    ImGui::SeparatorText(std::format("Used by {}", links.users.size()).c_str());
    for (const std::uint32_t user : links.users) {
        const bool selected = document.isSelected(LevelItem::entity(user));
        if (ImGui::Selectable(entityLabel(level, user).c_str(), selected)) {
            document.select(LevelItem::entity(user));
            frame = true;
        }
    }
    if (links.users.size() > 1) {
        ImGui::TextDisabled("Changes here change all of them.");
    }

    if (const ScriptProgram* program = m_library.find(tab.name)) {
        const ScriptDeclarations& declared = program->declarations();
        ImGui::SeparatorText("Declares");
        std::string state;
        for (const ScriptVariable& variable : declared.state) {
            state += std::format("{}{} ({})", state.empty() ? "" : ", ", variable.name,
                                 variable.flag ? "true/false" : "number");
        }
        const auto joined = [](const std::vector<std::string>& names) {
            std::string text;
            for (const std::string& name : names) {
                text += (text.empty() ? "" : ", ") + name;
            }
            return text.empty() ? std::string("none") : text;
        };
        ImGui::TextWrapped("State: %s", state.empty() ? "none" : state.c_str());
        ImGui::TextWrapped("Events: %s", joined(declared.events).c_str());
        ImGui::TextWrapped("Inputs: %s", joined(declared.inputs).c_str());
        if (declared.start) {
            ImGui::TextUnformatted("Has a start function, called as play begins.");
        }
    }

    if (!links.incoming.empty() || !links.outgoing.empty()) {
        ImGui::SeparatorText("Wiring");
        for (const ScriptWire& wire : links.incoming) {
            ImGui::TextWrapped("%s %s -> %s", entityLabel(level, wire.from).c_str(),
                               wire.event.c_str(), wire.input.c_str());
        }
        for (const ScriptWire& wire : links.outgoing) {
            const std::string delay =
                wire.delay > 0.0f ? std::format(" after {:g} s", wire.delay) : std::string();
            ImGui::TextWrapped("%s -> %s %s%s", wire.event.c_str(), wire.target.c_str(),
                               wire.input.c_str(), delay.c_str());
        }
    }

    const std::string_view error = m_library.error(tab.name);
    if (!links.notes.empty() || !error.empty()) {
        ImGui::SeparatorText("Problems");
    }
    if (!error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kProblemColour);
        const std::uint32_t line = m_library.errorLine(tab.name);
        const std::string text =
            line > 0 ? std::format("Line {}: {}", line, errorMessage(error, tab.name))
                     : std::string(error);
        if (ImGui::Selectable(text.c_str()) && line > 0) {
            tab.editor.goToLine(tab.buffer, line);
        }
        ImGui::PopStyleColor();
    }
    for (std::size_t i = 0; i < links.notes.size(); ++i) {
        const ScriptNote& note = links.notes[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_Text, note.problem ? kProblemColour : kRemarkColour);
        const std::string text =
            note.line > 0 ? std::format("Line {}: {}", note.line, note.text) : note.text;
        if (ImGui::Selectable(text.c_str()) && note.line > 0) {
            tab.editor.goToLine(tab.buffer, note.line);
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    return frame;
}

bool ScriptPanel::drawSide(EditorDocument& document) {
    bool frame = false;
    ImGui::SeparatorText("Scripts");
    for (const std::string_view name : m_library.names()) {
        const Tab* tab = findTab(name);
        const std::size_t users = scriptUsers(document.level(), name).size();
        const std::string label =
            std::format("{}{}{}", name, tab != nullptr && tab->buffer.modified() ? " *" : "",
                        users > 0 ? std::format("  ({} in level)", users) : std::string());
        if (ImGui::Selectable(label.c_str(), name == m_selected)) {
            open(name);
        }
        if (m_library.find(name) == nullptr) {
            ImGui::SetItemTooltip("Doesn't load: %s", std::string(m_library.error(name)).c_str());
        }
    }
    if (m_library.names().empty()) {
        ImGui::TextDisabled("None yet");
    }
    if (ImGui::Button("New script...")) {
        m_newName.clear();
        ImGui::OpenPopup("New script");
    }
    if (ImGui::BeginPopup("New script")) {
        ImGui::TextUnformatted("Name (lowercase letters, digits, '-' and '_'):");
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        const bool entered =
            ImGui::InputText("##name", &m_newName, ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("Make") || entered) && !m_newName.empty()) {
            std::string error;
            if (create(m_newName, error)) {
                ImGui::CloseCurrentPopup();
            } else {
                m_message = error;
            }
        }
        ImGui::EndPopup();
    }
    if (!m_message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kProblemColour);
        ImGui::TextWrapped("%s", m_message.c_str());
        ImGui::PopStyleColor();
        if (ImGui::SmallButton("OK")) {
            m_message.clear();
        }
    }
    if (Tab* tab = findTab(m_selected)) {
        frame = drawDetails(*tab, document);
    }
    return frame;
}

void ScriptPanel::drawTabs() {
    if (m_tabs.empty()) {
        ImGui::TextDisabled("Choose a script from the list to edit it, or make a new one. Script "
                            "entities' inspectors open theirs too.");
        return;
    }
    if (!ImGui::BeginTabBar("open scripts", ImGuiTabBarFlags_AutoSelectNewTabs |
                                                ImGuiTabBarFlags_FittingPolicyScroll)) {
        return;
    }
    std::string selected;
    for (const std::unique_ptr<Tab>& tab : m_tabs) {
        bool open = true;
        ImGuiTabItemFlags flags = tab->buffer.modified() ? ImGuiTabItemFlags_UnsavedDocument : 0;
        if (tab->select) {
            flags |= ImGuiTabItemFlags_SetSelected;
            tab->select = false;
        }
        const bool shown = ImGui::BeginTabItem(tab->name.c_str(), &open, flags);
        if (!open) {
            m_closing = tab->name;
        }
        if (!shown) {
            continue;
        }
        selected = tab->name;
        // Marks: where it fails to load, and the problems with how it fits the level.
        std::vector<CodeMark> marks;
        if (m_library.find(tab->name) == nullptr && m_library.errorLine(tab->name) > 0) {
            marks.push_back({m_library.errorLine(tab->name), true,
                             std::string(errorMessage(m_library.error(tab->name), tab->name))});
        }
        if (m_linksScript == tab->name) {
            for (const ScriptNote& note : m_links.notes) {
                if (note.line > 0) {
                    marks.push_back({note.line, note.problem, note.text});
                }
            }
        }
        if (tab->editor.draw(tab->buffer, marks)) {
            tab->changedAt = ImGui::GetTime();
        }
        if (tab->editor.hasKeyboard() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
            std::string error;
            if (!save(*tab, error)) {
                m_message = error;
            }
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    m_selected = selected;

    // The library checks what is typed once typing pauses.
    for (const std::unique_ptr<Tab>& tab : m_tabs) {
        if (tab->buffer.version() != tab->checkedVersion &&
            ImGui::GetTime() - tab->changedAt > kCheckDelay) {
            m_library.setText(tab->name, tab->buffer.text());
            tab->checkedVersion = tab->buffer.version();
        }
    }
}

bool ScriptPanel::draw(EditorDocument& document) {
    checkFiles();
    m_front.clear();
    if (m_focusWindow) {
        ImGui::SetNextWindowFocus();
        m_focusWindow = false;
    }
    if (!ImGui::Begin("Scripts")) {
        ImGui::End();
        return false;
    }
    bool frame = false;
    const float sideWidth = ImGui::GetFontSize() * 20.0f;
    if (ImGui::BeginChild("side", ImVec2(sideWidth, 0.0f),
                          ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders)) {
        frame = drawSide(document);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("editors", ImVec2(0.0f, 0.0f))) {
        drawTabs();
    }
    ImGui::EndChild();
    m_front = m_selected;

    // Closing a tab with unsaved changes asks first.
    const auto closeTab = [&] {
        std::erase_if(m_tabs,
                      [&](const std::unique_ptr<Tab>& tab) { return tab->name == m_closing; });
        m_library.clearText(m_closing);
        m_closing.clear();
    };
    if (!m_closing.empty() && !ImGui::IsPopupOpen("Unsaved script")) {
        const Tab* closing = findTab(m_closing);
        if (closing != nullptr && closing->buffer.modified()) {
            ImGui::OpenPopup("Unsaved script");
        } else {
            closeTab();
        }
    }
    if (ImGui::BeginPopupModal("Unsaved script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save changes to %s.luau before closing it?", m_closing.c_str());
        if (ImGui::Button("Save")) {
            std::string error;
            if (Tab* closing = findTab(m_closing); closing != nullptr && !save(*closing, error)) {
                m_message = error;
                m_closing.clear();
            } else {
                closeTab();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save")) {
            closeTab();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_closing.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
    return frame;
}

} // namespace ct
