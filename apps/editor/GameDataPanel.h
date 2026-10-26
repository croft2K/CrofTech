#pragma once

#include "croftech/editor/EditorDocument.h"
#include "croftech/editor/GameData.h"

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ct {

class BehaviourPanel;

// The Game data window: the game's enemy types, weapons, movement and rules, each a settings file
// (GameData) edited here with its own undo and saved when asked. The settings follow every edit,
// so markers in the view, and a game being played in the editor, change at once. A new enemy type
// or weapon starts as a copy of another (the charger and gunner, the shotgun and the rest, to
// begin with).
class GameDataPanel {
public:
    // Paths are shown relative to shownRoot.
    GameDataPanel(GameData& data, std::filesystem::path shownRoot);

    // Types and weapons can't be made, deleted or moved while playing: the game numbers them as
    // play starts.
    void draw(const EditorDocument& document, bool playing);
    // Shows the file ("enemies/gunner.cfg") and brings the window forward.
    void open(std::string_view path);
    // Where an enemy type's Edit behaviour opens its tree.
    void setBehaviourPanel(BehaviourPanel* panel) { m_behaviours = panel; }
    // Asks for the name of a new enemy type made from the one given.
    void newEnemyType(std::string_view from) { askNewName(false, from); }
    // Whether the window had the focus as of its last draw, when Ctrl+Z, Ctrl+Y and Ctrl+S are its.
    bool hasFocus() const { return m_focused; }
    // Logs what happened.
    bool save();

private:
    void drawList();
    void drawFile(const GameConfigFile& file, const EditorDocument& document, bool playing);
    void drawSetting(const GameConfigFile& file, const GameDataSetting& setting, bool owned);
    // The value's widget; returns whether it changed, with the new text in value.
    bool drawValue(const GameConfigFile& file, const GameDataSetting& setting, std::string& value);
    void drawEnemyButtons(const std::string& name, bool owned, const EditorDocument& document,
                          bool playing);
    void drawWeaponButtons(const std::string& name, bool owned, const EditorDocument& document,
                           bool playing);
    void askNewName(bool weapon, std::string_view from);
    void drawNewPopup(bool playing);
    void drawDeletePopup(const EditorDocument& document);
    // Edits wait until the file has been drawn: each replaces the settings being drawn.
    void applyPending();
    std::string shown(const std::filesystem::path& path) const;

    struct PendingEdit {
        GameConfigFile file;
        std::string key;
        std::string value;
        bool reset = false;
        ImGuiID row = 0;
    };

    GameData& m_data;
    BehaviourPanel* m_behaviours = nullptr;
    std::filesystem::path m_shownRoot;
    std::string m_selected; // the file shown, by its path
    bool m_focusWindow = false;
    bool m_focused = false;
    std::optional<PendingEdit> m_pending;
    // The row whose edit the next change carries on, while its widget is held, so a drag or a
    // stretch of typing is one undo step.
    ImGuiID m_mergeWith = 0;
    // A text setting being typed, kept as typed (the file keeps it trimmed).
    std::string m_textKey;
    std::string m_text;
    std::string m_newName;
    std::string m_newFrom;
    bool m_newWeapon = false; // else an enemy type
    bool m_openNewPopup = false;
    std::string m_problem;
};

} // namespace ct
