#pragma once

#include "GameDataPanel.h"
#include "LevelViewport.h"
#include "ScriptPanel.h"

#include "croftech/editor/EditorActions.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/level/EntityClasses.h"
#include "croftech/script/ScriptLibrary.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ct {

// What the menu bar asked the application to do this frame.
struct MenuRequests {
    bool open = false;
    bool save = false;
    bool quit = false;
    bool frameSelection = false;
    const EntityClassInfo* addEntity = nullptr; // a point entity, at the placement point
    const EntityPreset* addPreset = nullptr;    // a ready-made brush entity, there too
    std::string addEnemy;                       // an enemy of this type, there too
    bool play = false;
    bool playFromHere = false; // from the 3D camera
};

// The editor's panels: menu bar, tool bar (over the 3D view), hierarchy (groups, world brushes and
// entities with their brushes) and inspector (the selection's details, editable). Inspector edits
// preview live and finish as one undoable command.
class EditorPanels {
public:
    MenuRequests drawMenuBar(EditorDocument& document, EditorSettings& settings,
                             LevelViewport& viewport);
    // True when Play was clicked.
    bool drawToolbar(LevelViewport& viewport, EditorSettings& settings, const ViewRect& view);
    void drawHierarchy(EditorDocument& document);
    void drawInspector(EditorDocument& document, const EditorSettings& settings);
    // The game's data: the weapons pickups choose from and the enemy types, and the window that
    // edits them. Both must outlive these panels.
    void setGameData(const GameData* data, GameDataPanel* panel) {
        m_gameData = data;
        m_gameDataPanel = panel;
    }
    // The scripts the script property offers, whose events and inputs script entities have, and
    // the panel that edits them. Both must outlive these panels.
    void setScripts(const ScriptLibrary* scripts, ScriptPanel* panel) {
        m_scripts = scripts;
        m_scriptPanel = panel;
    }

private:
    // Sides, steps, thickness and facing for the shape being drawn.
    void drawShapeSettings(EditorSettings& settings);
    // Hierarchy rows. Each row selects what it shows: a group all its members, an entity itself
    // with its brushes, a brush just itself.
    void drawGroupRow(EditorDocument& document, std::uint32_t group,
                      std::span<const LevelItem> members,
                      std::span<const std::vector<std::size_t>> owned);
    void drawEntityRow(EditorDocument& document, std::size_t index,
                       std::span<const std::size_t> brushes);
    void drawBrushRow(EditorDocument& document, std::size_t index);
    // A click on a row: Ctrl adds or takes away, Shift selects the rows from the last one clicked.
    void rowClicked(EditorDocument& document, LevelItem item, SelectScope scope);
    void startRename(const EditorDocument& document);
    // The text field that replaces a row being renamed. True while it is drawn in its place.
    bool drawRenameField(EditorDocument& document, bool isGroup, std::uint32_t group,
                         LevelItem item);
    void drawBrushInspector(EditorDocument& document, const EditorSettings& settings,
                            std::size_t index);
    void drawEntityInspector(EditorDocument& document, std::size_t index, float grid);
    // The entity's properties: those of its class, with their defaults, then any others.
    void drawEntityProperties(EditorDocument& document, std::size_t index);
    // A script entity's Edit script button, or New script for one that names none.
    void drawScriptButtons(EditorDocument& document, std::size_t index);
    // An enemy's or a spawner's type, described, with a button to edit it in the Game data window.
    void drawEnemyType(const LevelEntity& entity);
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
    const GameData* m_gameData = nullptr;
    GameDataPanel* m_gameDataPanel = nullptr;
    const ScriptLibrary* m_scripts = nullptr;
    ScriptPanel* m_scriptPanel = nullptr;
    std::string m_newScript; // the name being typed for a new script

    // The rows shown in the hierarchy, in order, for Shift-click ranges: this frame's so far and
    // last frame's, which every click sees in full.
    std::vector<LevelItem> m_rows;
    std::vector<LevelItem> m_shownRows;
    std::optional<LevelItem> m_anchor; // the row a Shift-click range starts from
    // Renaming in the hierarchy (F2 or a double-click): a group, or an item.
    bool m_renaming = false;
    bool m_renameIsGroup = false;
    std::uint32_t m_renameGroup = 0;
    LevelItem m_renameItem;
    std::string m_renameText;
    std::string m_renameProblem;
    bool m_renameFocus = false;
};

} // namespace ct
