#pragma once

#include "CodeEditor.h"

#include "croftech/core/Content.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/editor/ScriptLinks.h"
#include "croftech/editor/TextBuffer.h"
#include "croftech/script/ScriptLibrary.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

// What a new script starts as: one for script entities, or one for behaviour trees' script nodes.
enum class ScriptTemplate : std::uint8_t { Entity, Behaviour };

// The Scripts window: the scripts of the content folders, each opened in a tab of its own to edit,
// beside what the script in front declares, the entities that run it, the wiring in and out of
// them, and what doesn't line up (marked on its line where it has one). The text being edited is
// checked as it is typed and stands for the file in the rest of the editor (the inspector's
// wiring, play) until saved.
class ScriptPanel {
public:
    ScriptPanel(const ContentPaths& content, ScriptLibrary& library);

    // Returns whether it selected an entity and asks for the view to frame it.
    bool draw(EditorDocument& document);

    // Brings the script's tab forward, opening it if need be, and the window with it.
    void open(std::string_view name);
    // Writes scripts/<name>.luau into the game's content folder from a starting template and
    // opens it. Fails, saying why, for a bad name or one already taken.
    bool create(std::string_view name, std::string& error,
                ScriptTemplate kind = ScriptTemplate::Entity);

    bool hasKeyboard() const;
    bool hasUnsavedChanges() const;
    std::vector<std::string> unsavedNames() const;
    bool saveAll(std::string& error);
    // Puts the text of open scripts in place of their files', for play.
    void applyOpenText(std::vector<ScriptSource>& scripts) const;
    // The text of every script with changes not yet saved.
    std::vector<ScriptSource> unsavedTexts() const;
    // The script in the front tab while the window shows it; empty otherwise.
    const std::string& frontScript() const { return m_front; }

private:
    struct Tab {
        std::string name;
        TextBuffer buffer;
        CodeEditor editor;
        std::uint64_t checkedVersion = 0; // the buffer version the library last compiled
        double changedAt = 0.0;
        std::optional<std::filesystem::file_time_type> fileTime; // when read or saved
        bool select = false;
    };

    Tab* findTab(std::string_view name);
    bool save(Tab& tab, std::string& error);
    void checkFiles();
    // Draws the list of scripts and the front script's details; returns a frame request.
    bool drawSide(EditorDocument& document);
    bool drawDetails(Tab& tab, EditorDocument& document);
    void drawTabs();
    const ScriptLinks& linksOf(const Tab& tab, const EditorDocument& document);

    const ContentPaths& m_content;
    ScriptLibrary& m_library;
    std::vector<std::unique_ptr<Tab>> m_tabs;
    std::string m_front;    // this frame's
    std::string m_selected; // the tab in front, as of last frame
    std::string m_newName;
    std::string m_message;
    std::string m_closing; // a tab with unsaved changes being closed
    bool m_focusWindow = false;
    double m_filesCheckedAt = 0.0;

    // Links of the selected script, kept until the level, the scripts or the selection change.
    ScriptLinks m_links;
    std::string m_linksScript;
    std::uint64_t m_linksLevel = ~std::uint64_t{0};
    std::uint64_t m_linksGeneration = ~std::uint64_t{0};
};

} // namespace ct
