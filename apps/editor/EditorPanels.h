#pragma once

#include "LevelViewport.h"

#include "croftech/editor/EditorDocument.h"

#include <optional>
#include <string>

namespace ct {

// What the menu bar asked the application to do this frame.
struct MenuRequests {
    bool save = false;
    bool quit = false;
    bool frameSelection = false;
    bool addPlayerSpawn = false;
};

// The editor's panels: menu bar, hierarchy (every brush and entity) and inspector (the selected
// one's details, editable). Inspector edits preview live and finish as one undoable command.
class EditorPanels {
public:
    MenuRequests drawMenuBar(EditorDocument& document, EditorSettings& settings,
                             LevelViewport& viewport);
    void drawHierarchy(EditorDocument& document);
    void drawInspector(EditorDocument& document, const EditorSettings& settings);

private:
    void drawBrushInspector(EditorDocument& document, const EditorSettings& settings);
    void drawEntityInspector(EditorDocument& document);
    // Applies an edited entity as one command, or explains why it can't be.
    void commitEntity(EditorDocument& document, std::size_t index, const LevelEntity& edited,
                      const char* name);
    // A text field that edits its own copy while focused, and returns true with the new text when
    // focus leaves it (Enter, Tab or a click elsewhere) after a change.
    bool textField(const char* label, int field, const std::string& current, std::string& result);

    // The brush as it was when an inspector drag began, so the drag undoes in one step.
    std::optional<Brush> m_brushBefore;
    Selection m_editing;
    int m_activeField = -1;
    std::string m_fieldText;
    std::string m_newKey;
    std::string m_newValue;
    std::string m_problem;
};

} // namespace ct
