#pragma once

#include "croftech/editor/EditorDocument.h"
#include "croftech/editor/LevelItem.h"
#include "croftech/editor/LogicGraph.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ax::NodeEditor {
struct EditorContext;
}

namespace ct {

class ScriptLibrary;

// The Logic window: the level's wiring as a graph (LogicGraph), each entity a node with its inputs
// on the left and its events on the right, each output a link between them. Dragging from an
// event to an input wires them (an output, as the inspector's outputs table has them, which is
// the wiring's text form); Delete takes the chosen links away, or the chosen entities. Nodes are
// laid out by the wiring until moved, and keep the places they are moved to in the level. The
// graph's selection is the level's.
class LogicPanel {
public:
    LogicPanel();
    ~LogicPanel();
    LogicPanel(const LogicPanel&) = delete;
    LogicPanel& operator=(const LogicPanel&) = delete;

    // The scripts that give script entities their events and inputs.
    void setScripts(const ScriptLibrary* scripts) { m_scripts = scripts; }
    // Returns whether it asks for the view to frame the selection (a node was double-clicked).
    bool draw(EditorDocument& document);
    // Whether the window had the focus as of its last draw, when the keys are its.
    bool hasFocus() const { return m_focused; }

private:
    struct Pin {
        std::string name;
        bool known = true; // false: wired, but the entity has no such signal
    };
    struct Node {
        std::size_t entity = 0;
        std::vector<Pin> inputs;
        std::vector<Pin> events;
        std::vector<std::string> problems; // outputs to names no entity has
    };
    struct Link {
        std::size_t from = 0; // entities
        std::size_t to = 0;
        std::size_t output = 0; // in from's outputs
        std::size_t fromNode = 0;
        std::size_t toNode = 0;
        std::size_t event = 0; // pins of those nodes
        std::size_t input = 0;
        bool known = true;
    };

    // Works out the nodes and links again if the level or the scripts changed.
    void rebuild(const EditorDocument& document);
    void drawToolbar(EditorDocument& document);
    void drawGraph(EditorDocument& document);
    void drawNode(const Level& level, std::size_t node);
    void handleCreate(EditorDocument& document);
    void handleKeys(EditorDocument& document);
    void syncSelection(EditorDocument& document);
    void drawDetails(EditorDocument& document);
    void drawLinkDetails(EditorDocument& document, const Link& link);
    std::vector<std::size_t> selectedNodes() const;
    std::vector<std::size_t> selectedLinks() const;

    const ScriptLibrary* m_scripts = nullptr;
    ax::NodeEditor::EditorContext* m_editor = nullptr;
    bool m_everything = false;
    std::uint64_t m_seenChange = ~0ull;
    std::uint64_t m_seenScripts = ~0ull;
    bool m_seenEverything = false;

    std::vector<Node> m_nodes;
    std::vector<Link> m_links;
    std::vector<std::size_t> m_nodeOf; // each entity's node, or kNoNode
    std::vector<GraphSize> m_sizes;    // as last drawn
    std::vector<GraphPoint> m_places;  // laid out
    // Where each node was put last frame; one not there is being dragged.
    std::vector<ImVec2> m_placed;
    // Each pin's point on its node's edge as last drawn, for the delays drawn on links.
    std::vector<std::vector<ImVec2>> m_inputAt;
    std::vector<std::vector<ImVec2>> m_eventAt;
    int m_frameAll = 3;
    bool m_frameAllNow = false;
    bool m_shownAll = false; // once, at the graph's settled size
    ImVec2 m_screenSize{0.0f, 0.0f};

    // The selections as last matched, to tell which side changed.
    std::vector<LevelItem> m_syncedLevel;
    std::vector<std::size_t> m_syncedGraph;
    // What the graph had chosen as it was drawn this frame, for the panel beside it.
    std::vector<std::size_t> m_chosenLinks;
    std::vector<std::size_t> m_chosenNodes;
    std::uint64_t m_chosenChange = ~0ull;
    std::optional<LevelEntity> m_entityBefore; // while the delay is dragged
    bool m_frameView = false;
    bool m_focused = false;
    std::string m_problem;
};

} // namespace ct
