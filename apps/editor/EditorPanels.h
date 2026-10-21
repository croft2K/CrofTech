#pragma once

#include "LevelViewport.h"

#include "croftech/editor/EditorDocument.h"
#include "croftech/level/EntityClasses.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ct {

// What the menu bar asked the application to do this frame.
struct MenuRequests {
    bool save = false;
    bool quit = false;
    bool frameSelection = false;
    const EntityClassInfo* addEntity = nullptr; // a point entity, at the placement point
    bool play = false;
};

// The editor's panels: menu bar, tool bar (over the 3D view), hierarchy (every brush and entity)
// and inspector (the selection's details, editable). Inspector edits preview live and finish as
// one undoable command.
class EditorPanels {
public:
    MenuRequests drawMenuBar(EditorDocument& document, EditorSettings& settings,
                             LevelViewport& viewport);
    // True when Play was clicked.
    bool drawToolbar(LevelViewport& viewport, EditorSettings& settings, const ViewRect& view);
    void drawHierarchy(EditorDocument& document);
    void drawInspector(EditorDocument& document, const EditorSettings& settings);

private:
    // Sides, steps, thickness and facing for the shape being drawn.
    void drawShapeSettings(EditorSettings& settings);
    void drawBrushInspector(EditorDocument& document, const EditorSettings& settings,
                            std::size_t index);
    void drawEntityInspector(EditorDocument& document, std::size_t index);
    // The entity's properties: those of its class, with their defaults, then any others.
    void drawEntityProperties(EditorDocument& document, std::size_t index);
    // The entity's outputs, as a table of event, target, input and delay.
    void drawEntityOutputs(EditorDocument& document, std::size_t index);
    void drawMultipleInspector(EditorDocument& document);
    // Quarter turns, flips, duplicate and delete for whatever is selected.
    void drawSelectionActions(EditorDocument& document, const EditorSettings& settings);
    // Applies an edited entity as one command, or explains why it can't be.
    void commitEntity(EditorDocument& document, std::size_t index, const LevelEntity& edited,
                      const char* name);
    // A text field that edits its own copy while focused, and returns true with the new text when
    // focus leaves it (Enter, Tab or a click elsewhere) after a change. The hint shows while it is
    // empty.
    bool textField(const char* label, int field, const std::string& current, std::string& result,
                   const char* hint = nullptr);

    // The brush as it was when an inspector drag began, so the drag undoes in one step.
    std::optional<Brush> m_brushBefore;
    std::vector<LevelItem> m_editing;
    int m_activeField = -1;
    std::string m_fieldText;
    std::string m_newKey;
    std::string m_newValue;
    EntityOutput m_newOutput;
    std::string m_problem;
};

} // namespace ct
