#pragma once

#include "PlaySession.h"

#include "croftech/editor/BehaviourEditing.h"
#include "croftech/editor/GameData.h"
#include "croftech/sim/EntityId.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ax::NodeEditor {
struct EditorContext;
}

namespace ct {

class ScriptLibrary;
class ScriptPanel;

// The Behaviours window: a behaviour tree (behaviours/<name>.behaviour) as a graph laid out left
// to right, as its file reads top to bottom, edited through GameData with the Game data window's
// undo and saving, so changes play at once. Nodes are added by dragging from a node's right edge,
// moved under another by linking them, reordered by dragging them up or down, and changed through
// their right-click menu; the panel beside the graph edits the chosen node's setting and note.
// A tree still being built opens as a draft, its problems shown on its nodes, along with those of
// the scripts its script nodes name. While playing, the nodes the watched enemy ran on its last
// tick light up.
class BehaviourPanel {
public:
    explicit BehaviourPanel(GameData& data);
    ~BehaviourPanel();
    BehaviourPanel(const BehaviourPanel&) = delete;
    BehaviourPanel& operator=(const BehaviourPanel&) = delete;

    // play is the game being played in the editor, if any.
    void draw(const PlaySession* play);
    // Shows the behaviour and brings the window forward.
    void open(std::string_view name);
    // The scripts that script nodes may name, and the window that edits them.
    void setScripts(const ScriptLibrary* scripts, ScriptPanel* panel) {
        m_scripts = scripts;
        m_scriptPanel = panel;
    }
    // Whether the window had the focus as of its last draw, when the keys are its.
    bool hasFocus() const { return m_focused; }

private:
    struct PendingEdit {
        BehaviourTree tree;
        std::string name;
        bool merge = false;
        std::optional<std::uint32_t> select; // the node to choose once it is applied
    };
    // What the watched enemy's tree did on its last tick, for each node.
    enum class NodeLight : std::uint8_t { Off, Failed, Ran };

    // Reads the shown behaviour again if it changed.
    void refresh();
    void drawToolbar(const PlaySession* play);
    void drawGraph();
    void drawNode(std::uint32_t node, const std::vector<BehaviourProblem>& problems);
    void drawLinks();
    void handleCreate();
    void handleDelete();
    void handleShortcuts();
    void drawMenus();
    void drawDetails(const PlaySession* play);
    void drawScriptSetting(std::uint32_t node);
    void drawNewPopup();
    // Edits wait until the graph has been drawn.
    void edit(BehaviourTree tree, std::string name, std::optional<std::uint32_t> select,
              bool merge = false);
    void applyPending();
    void watch(const PlaySession* play);

    void copy(std::uint32_t node);
    // Under the node if it takes children, or else after it.
    void paste(std::uint32_t node);
    void duplicate(std::uint32_t node);
    void remove(std::vector<std::uint32_t> nodes);
    std::vector<std::uint32_t> selectedNodes() const;

    GameData& m_data;
    const ScriptLibrary* m_scripts = nullptr;
    ScriptPanel* m_scriptPanel = nullptr;
    ax::NodeEditor::EditorContext* m_editor = nullptr;
    std::string m_name = "standard";
    std::uint64_t m_seenChange = ~0ull;
    std::uint64_t m_seenScripts = ~0ull;
    std::string m_seenName;
    std::optional<BehaviourTree> m_tree;
    std::string m_readError;
    std::vector<BehaviourProblem> m_problems; // those that keep the tree from playing
    // Of the scripts that script nodes name, whose nodes fail until they are put right.
    std::vector<BehaviourProblem> m_scriptProblems;
    std::vector<BehaviourProblem> m_shownProblems; // both, in node order
    std::vector<std::uint32_t> m_parents;
    // Each node's size as last drawn, and its box as laid out from them.
    std::vector<BehaviourBox> m_sizes;
    std::vector<BehaviourBox> m_boxes;
    // Where each node was put last frame; one not there is being dragged.
    std::vector<ImVec2> m_placed;
    int m_frameAll = 3; // frames until the whole tree is shown (kFramesToShowAll)

    std::optional<PendingEdit> m_pending;
    std::optional<std::uint32_t> m_select;
    std::uint32_t m_shown = kNoBehaviourParent; // the node the details show
    ImGuiID m_mergeWith = 0;
    std::uint32_t m_noteNode = kNoBehaviourParent;
    std::string m_note;
    bool m_typingNote = false; // as of the last frame

    std::uint32_t m_menuNode = 0;
    // A node being added by dragging from a pin: under the node, or above it (wrapping it).
    std::uint32_t m_addAt = 0;
    bool m_addAbove = false;
    float m_addY = 0.0f;
    std::string m_filter;
    bool m_focusFilter = false;

    std::string m_newName;
    std::string m_newScript;
    bool m_openNewPopup = false;
    std::string m_problem;
    bool m_focusWindow = false;
    bool m_focused = false;

    // Play: the enemy watched, or none to watch the one looked at.
    std::optional<EntityId> m_watchChosen;
    std::optional<EntityId> m_watched;
    std::string m_watchedName;
    std::vector<NodeLight> m_lights;
    std::vector<double> m_litUntil; // a node stays lit a moment, so a flicker can be seen
    std::vector<double> m_failedUntil;
};

} // namespace ct
