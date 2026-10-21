#include "EditorPanels.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/editor/EditorActions.h"
#include "croftech/editor/EntityLinks.h"
#include "croftech/editor/LevelCommands.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ct {
namespace {

std::string brushLabel(const Level& level, std::size_t index) {
    const Brush& brush = level.brushes[index];
    const std::string name = brush.name.empty() ? std::format("Brush {}", index) : brush.name;
    if (const auto box = boxBounds(brush)) {
        const Vec3 size = box->max - box->min;
        return std::format("{}  box {:g} x {:g} x {:g} m", name, size.x, size.y, size.z);
    }
    return std::format("{}  {} faces", name, brush.faces.size());
}

std::string entityLabel(const LevelEntity& entity, std::size_t index) {
    const std::string* name = findProperty(entity, "name");
    if (name != nullptr && !name->empty()) {
        return std::format("{}  {} (entity {})", *name, entity.className, index);
    }
    return std::format("Entity {}  {}", index, entity.className);
}

// The group whose members are exactly the selection, if there is one.
std::optional<std::uint32_t> selectedGroup(const EditorDocument& document) {
    if (document.selection().empty()) {
        return std::nullopt;
    }
    const std::uint32_t group = itemGroup(document.level(), document.selection().front());
    if (group == kNoGroup || groupMembers(document.level(), group) != document.selection()) {
        return std::nullopt;
    }
    return group;
}

void tooltip(std::string_view text) {
    if (!text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("%s", std::string(text).c_str());
    }
}

// The names outputs can target, sorted, without repeats.
std::vector<std::string> entityNames(const Level& level) {
    std::vector<std::string> names;
    for (const LevelEntity& entity : level.entities) {
        const std::string* name = findProperty(entity, "name");
        if (name != nullptr && !name->empty()) {
            names.push_back(*name);
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

// The inputs of the entities with the name, each once.
std::vector<SignalInfo> inputsOf(const Level& level, std::string_view name) {
    std::vector<SignalInfo> inputs;
    for (const LevelEntity& entity : level.entities) {
        const std::string* entityName = findProperty(entity, "name");
        const EntityClassInfo* entityClass = findEntityClass(entity.className);
        if (entityName == nullptr || *entityName != name || entityClass == nullptr) {
            continue;
        }
        for (const SignalInfo& input : entityClass->inputs) {
            if (findSignal(inputs, input.name) == nullptr) {
                inputs.push_back(input);
            }
        }
    }
    return inputs;
}

// A combo of signals (events or inputs); true with the chosen name when one is picked.
bool signalCombo(const char* label, std::string_view current, std::span<const SignalInfo> signals,
                 std::string& chosen) {
    bool picked = false;
    if (ImGui::BeginCombo(label, std::string(current).c_str())) {
        for (const SignalInfo& signal : signals) {
            const std::string name(signal.name);
            if (ImGui::Selectable(name.c_str(), signal.name == current)) {
                chosen = name;
                picked = true;
            }
            tooltip(signal.description);
        }
        ImGui::EndCombo();
    }
    return picked;
}

bool anyBrushSelected(const EditorDocument& document) {
    return std::any_of(document.selection().begin(), document.selection().end(),
                       [](const LevelItem& item) { return item.kind == LevelItem::Kind::Brush; });
}

// Menu items that make the selected brushes into an entity: the presets first, then each brush
// class with its defaults, for building one up from scratch.
void brushEntityMenuItems(EditorDocument& document) {
    for (const EntityPreset& preset : entityPresets()) {
        if (ImGui::MenuItem(std::string(preset.name).c_str())) {
            createBrushEntity(document, preset);
        }
        tooltip(preset.description);
    }
    ImGui::Separator();
    for (const EntityClassInfo& entityClass : entityClasses()) {
        if (entityClass.shape != EntityShape::Brush) {
            continue;
        }
        const std::string label = std::format("{} (class defaults)", entityClass.name);
        if (ImGui::MenuItem(label.c_str())) {
            const bool moves = findPropertyInfo(entityClass, "move") != nullptr;
            createBrushEntity(document, EntityPreset{entityClass.name,
                                                     entityClass.description,
                                                     entityClass.name,
                                                     {},
                                                     {},
                                                     moves,
                                                     {}});
        }
        tooltip(entityClass.description);
    }
}

std::string commandLabel(const char* verb, std::string_view name, const char* shortcut) {
    return name.empty() ? std::format("{}##{}", verb, shortcut) : std::format("{} {}", verb, name);
}

constexpr std::array<const char*, 3> kAxisNames{"X", "Y", "Z"};

struct ToolInfo {
    EditorTool tool;
    const char* name;
    const char* shortcut;
    const char* hint;
};

constexpr std::array<ToolInfo, 7> kTools{{
    {EditorTool::Select, "Select", "Q", "Select: click, Ctrl-click to add, drag a box"},
    {EditorTool::Move, "Move", "W", "Move: drag an arrow, or a square to move in its plane"},
    {EditorTool::Rotate, "Rotate", "E", "Rotate: drag a ring"},
    {EditorTool::Scale, "Scale", "R", "Scale: drag a handle on the selection's box"},
    {EditorTool::Vertex, "Vertex", "V",
     "Vertex: drag the corners, edge middles or face middles of the selected brushes; "
     "Ctrl-click or drag a box to pick several, Del removes corners"},
    {EditorTool::Clip, "Clip", "C",
     "Clip: click 2 or 3 points to cut the selected brushes; Tab picks the side, Enter cuts"},
    {EditorTool::Draw, "Shape", "B",
     "Shape: drag a rectangle, release, move up for the height, click to place"},
}};

const ToolInfo& toolInfo(EditorTool tool) {
    for (const ToolInfo& info : kTools) {
        if (info.tool == tool) {
            return info;
        }
    }
    return kTools.front();
}

} // namespace

MenuRequests EditorPanels::drawMenuBar(EditorDocument& document, EditorSettings& settings,
                                       LevelViewport& viewport) {
    MenuRequests requests;
    if (!ImGui::BeginMainMenuBar()) {
        return requests;
    }
    const bool hasSelection = !document.selection().empty();
    const float grid = settings.grid();
    if (ImGui::BeginMenu("File")) {
        requests.save = ImGui::MenuItem("Save", "Ctrl+S");
        ImGui::Separator();
        requests.quit = ImGui::MenuItem("Quit");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const CommandStack& history = document.history();
        if (ImGui::MenuItem(commandLabel("Undo", history.undoName(), "u").c_str(), "Ctrl+Z", false,
                            history.canUndo())) {
            viewport.cancelDrag(document);
            document.undo();
        }
        if (ImGui::MenuItem(commandLabel("Redo", history.redoName(), "r").c_str(), "Ctrl+Y", false,
                            history.canRedo())) {
            viewport.cancelDrag(document);
            document.redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", "Ctrl+A")) {
            selectAll(document);
        }
        if (ImGui::MenuItem("Deselect", "Esc", false, hasSelection)) {
            document.clearSelection();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection)) {
            duplicateSelection(document, {grid, grid, 0.0f});
        }
        if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) {
            deleteSelection(document);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Group", "Ctrl+G", false, hasSelection)) {
            groupSelection(document);
        }
        if (ImGui::MenuItem("Ungroup", "Ctrl+Shift+G", false, hasSelection)) {
            ungroupSelection(document);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Hollow", "Ctrl+H", false, hasSelection)) {
            hollowSelection(document, grid);
        }
        if (ImGui::MenuItem("Merge", "Ctrl+J", false, document.selection().size() >= 2)) {
            mergeSelection(document);
        }
        if (ImGui::MenuItem("Subtract", "Ctrl+K", false, hasSelection)) {
            subtractSelection(document);
        }
        if (ImGui::BeginMenu("Transform", hasSelection)) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (ImGui::MenuItem(
                        std::format("Turn +90\xC2\xB0 about {}", kAxisNames[axis]).c_str())) {
                    rotateSelection(document, axis, 1, grid);
                }
                if (ImGui::MenuItem(
                        std::format("Turn -90\xC2\xB0 about {}", kAxisNames[axis]).c_str())) {
                    rotateSelection(document, axis, -1, grid);
                }
            }
            ImGui::Separator();
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (ImGui::MenuItem(std::format("Flip along {}", kAxisNames[axis]).c_str())) {
                    flipSelection(document, axis, grid);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        for (const ToolInfo& info : kTools) {
            if (ImGui::MenuItem(info.name, info.shortcut, viewport.tool() == info.tool)) {
                viewport.setTool(info.tool);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Create")) {
        for (const ShapeKind kind : kShapeKinds) {
            const bool drawing = viewport.tool() == EditorTool::Draw && settings.shape == kind;
            if (ImGui::MenuItem(std::string(shapeName(kind)).c_str(), nullptr, drawing)) {
                settings.shape = kind;
                viewport.setTool(EditorTool::Draw);
            }
        }
        ImGui::SeparatorText("Ready-made");
        for (const EntityPreset& preset : entityPresets()) {
            if (ImGui::MenuItem(std::string(preset.name).c_str())) {
                requests.addPreset = &preset;
            }
            tooltip(preset.description);
        }
        ImGui::SeparatorText("Entities");
        for (const EntityClassInfo& entityClass : entityClasses()) {
            if (entityClass.shape == EntityShape::Point &&
                ImGui::MenuItem(std::string(entityClass.name).c_str())) {
                requests.addEntity = &entityClass;
            }
            if (entityClass.shape == EntityShape::Point) {
                tooltip(entityClass.description);
            }
        }
        if (ImGui::BeginMenu("Make selected brushes into", anyBrushSelected(document))) {
            brushEntityMenuItems(document);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Play")) {
        requests.play = ImGui::MenuItem("Play level", "F5");
        ImGui::SetItemTooltip("Run the level as it is now, without saving it; Esc stops");
        requests.playFromHere = ImGui::MenuItem("Play from here", "Shift+F5");
        ImGui::SetItemTooltip("Start the player where the 3D camera is, facing the same way");
        ImGui::TextDisabled("While playing: F6 saves, F9 loads the newest save or checkpoint");
        ImGui::SeparatorText("Network conditions");
        LinkConditions& conditions = settings.playConditions;
        const float width = ImGui::GetFontSize() * 12.0f;
        ImGui::SetNextItemWidth(width);
        ImGui::SliderFloat("Latency (ms)", &conditions.latencyMilliseconds, 0.0f, 500.0f, "%.0f");
        ImGui::SetNextItemWidth(width);
        ImGui::SliderFloat("Jitter (ms)", &conditions.jitterMilliseconds, 0.0f, 200.0f, "%.0f");
        ImGui::SetNextItemWidth(width);
        ImGui::SliderFloat("Packet loss (%)", &conditions.lossPercent, 0.0f, 50.0f, "%.1f");
        ImGui::TextDisabled("Each way; the round trip is twice the latency.");
        if (ImGui::MenuItem("Perfect network", nullptr, false, !(conditions == LinkConditions{}))) {
            conditions = {};
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        requests.frameSelection = ImGui::MenuItem("Frame selection", "F", false, hasSelection);
        ImGui::Separator();
        if (ImGui::MenuItem("One pane", nullptr, viewport.layout() == ViewLayout::One)) {
            viewport.setLayout(ViewLayout::One);
        }
        if (ImGui::MenuItem("Four panes", nullptr, viewport.layout() == ViewLayout::Four)) {
            viewport.setLayout(ViewLayout::Four);
        }
        if (ImGui::BeginMenu("Active pane shows")) {
            for (const ViewMode mode : kViewModes) {
                if (ImGui::MenuItem(viewModeName(mode), nullptr, viewport.activeMode() == mode)) {
                    viewport.setActiveMode(mode);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Hide handles behind surfaces", nullptr,
                        &settings.hideHandlesBehindSurfaces);
        ImGui::SetItemTooltip("In 3D panes, the vertex tool shows and picks only handles in "
                              "plain sight");
        ImGui::MenuItem("Show all wires", nullptr, &settings.showAllWires);
        ImGui::SetItemTooltip("Draw every entity's outputs, not just the selected entities'");
        ImGui::Separator();
        if (ImGui::BeginMenu("Grid")) {
            for (std::size_t i = 0; i < kGridSizes.size(); ++i) {
                if (ImGui::MenuItem(std::format("{:g} m", kGridSizes[i]).c_str(), nullptr,
                                    settings.gridIndex == i)) {
                    settings.gridIndex = i;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Rotation snap")) {
            for (std::size_t i = 0; i < kRotationSteps.size(); ++i) {
                if (ImGui::MenuItem(std::format("{:g}\xC2\xB0", kRotationSteps[i]).c_str(), nullptr,
                                    settings.rotationIndex == i)) {
                    settings.rotationIndex = i;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    // Status on the right: what the tool does, the snapping, and how to fly.
    const std::string status =
        std::format("{}   grid {:g} m  ([ ])   turn {:g}\xC2\xB0   right-drag to fly",
                    toolInfo(viewport.tool()).hint, settings.grid(), settings.rotationStep());
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(status.c_str()).x -
                    ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::EndMainMenuBar();
    return requests;
}

bool EditorPanels::drawToolbar(LevelViewport& viewport, EditorSettings& settings,
                               const ViewRect& view) {
    if (view.width <= 0.0f || view.height <= 0.0f) {
        return false;
    }
    const float padding = ImGui::GetStyle().WindowPadding.x;
    ImGui::SetNextWindowPos({view.x + padding, view.y + padding});
    ImGui::SetNextWindowBgAlpha(0.85f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
    if (!ImGui::Begin("Tools", nullptr, kFlags)) {
        ImGui::End();
        return false;
    }
    const auto toolButton = [](const std::string& label, bool active, const char* hint) {
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        const bool clicked = ImGui::Button(label.c_str());
        if (active) {
            ImGui::PopStyleColor();
        }
        ImGui::SetItemTooltip("%s", hint);
        return clicked;
    };
    for (const ToolInfo& info : kTools) {
        if (info.tool == EditorTool::Draw) {
            continue; // the shape buttons below
        }
        if (info.tool != EditorTool::Select) {
            ImGui::SameLine();
        }
        if (toolButton(std::format("{} ({})", info.name, info.shortcut),
                       viewport.tool() == info.tool, info.hint)) {
            viewport.setTool(info.tool);
        }
    }
    // Shapes make something new, so they stand a little apart from the selection tools.
    const bool drawing = viewport.tool() == EditorTool::Draw;
    for (const ShapeKind kind : kShapeKinds) {
        ImGui::SameLine(0.0f, kind == kShapeKinds[0] ? ImGui::GetFontSize() : -1.0f);
        if (toolButton(std::string(shapeName(kind)), drawing && settings.shape == kind,
                       toolInfo(EditorTool::Draw).hint)) {
            settings.shape = kind;
            viewport.setTool(EditorTool::Draw);
        }
    }
    // Play stands apart at the end.
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    const bool play =
        toolButton("Play (F5)", false, "Run the level as it is now, without saving it; Esc stops");
    if (drawing) {
        drawShapeSettings(settings);
    }
    ImGui::End();
    return play;
}

void EditorPanels::drawShapeSettings(EditorSettings& settings) {
    ShapeSettings& shape = settings.shapeSettings;
    const ShapeKind kind = settings.shape;
    const float width = ImGui::GetFontSize() * 8.0f;
    bool any = false;
    const auto next = [&] {
        if (any) {
            ImGui::SameLine(0.0f, ImGui::GetFontSize());
        }
        any = true;
        ImGui::SetNextItemWidth(width);
    };
    if (kind == ShapeKind::Cylinder || kind == ShapeKind::Cone || kind == ShapeKind::Sphere) {
        next();
        const bool sphere = kind == ShapeKind::Sphere;
        ImGui::SliderInt("Sides", &shape.sides, sphere ? kMinSphereSides : kMinShapeSides,
                         sphere ? kMaxSphereSides : kMaxShapeSides, "%d",
                         ImGuiSliderFlags_AlwaysClamp);
    }
    if (kind == ShapeKind::Arch) {
        next();
        ImGui::SliderInt("Segments", &shape.sides, kMinShapeSides, kMaxShapeSides, "%d",
                         ImGuiSliderFlags_AlwaysClamp);
        next();
        const float grid = settings.grid();
        if (ImGui::DragFloat("Thickness", &shape.thickness, grid * 0.25f, grid, 64.0f, "%.3g m",
                             ImGuiSliderFlags_AlwaysClamp)) {
            shape.thickness = std::max(grid, snapToGrid(shape.thickness, grid));
        }
    }
    if (kind == ShapeKind::Stairs) {
        next();
        ImGui::SliderInt("Steps", &shape.steps, 1, kMaxStairSteps, "%d",
                         ImGuiSliderFlags_AlwaysClamp);
    }
    if (kind == ShapeKind::Wedge || kind == ShapeKind::Stairs || kind == ShapeKind::Arch) {
        next();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("Away from view").x + ImGui::GetFrameHeight() +
                                ImGui::GetStyle().FramePadding.x * 2.0f);
        constexpr const char* kFacings[]{"Away from view", "+X", "+Y", "-X", "-Y"};
        int facing = static_cast<int>(settings.facing);
        if (ImGui::Combo(kind == ShapeKind::Arch ? "Opening" : "Rises to", &facing, kFacings,
                         IM_ARRAYSIZE(kFacings))) {
            settings.facing = static_cast<ShapeFacing>(facing);
        }
    }
    if (any) {
        ImGui::TextDisabled("Changing these reshapes the shape you just placed.");
    }
}

void EditorPanels::drawHierarchy(EditorDocument& document) {
    if (!ImGui::Begin("Hierarchy")) {
        ImGui::End();
        return;
    }
    const Level& level = document.level();
    m_shownRows.swap(m_rows);
    m_rows.clear();
    if (m_renaming &&
        (m_renameIsGroup ? m_renameGroup >= level.groups.size() : !exists(level, m_renameItem))) {
        m_renaming = false; // undone away
    }

    // Each entity's brushes, each group's members, and what is in no group, gathered once.
    std::vector<std::vector<std::size_t>> owned(level.entities.size());
    std::vector<std::vector<LevelItem>> grouped(level.groups.size());
    std::vector<std::size_t> looseBrushes;
    std::vector<std::size_t> looseEntities;
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        const Brush& brush = level.brushes[i];
        if (brush.entity < owned.size()) {
            owned[brush.entity].push_back(i);
        } else if (brush.group < grouped.size()) {
            grouped[brush.group].push_back(LevelItem::brush(i));
        } else {
            looseBrushes.push_back(i);
        }
    }
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        if (level.entities[i].group < grouped.size()) {
            grouped[level.entities[i].group].push_back(LevelItem::entity(i));
        } else {
            looseEntities.push_back(i);
        }
    }

    if (!level.groups.empty() &&
        ImGui::CollapsingHeader(std::format("Groups ({})###groups", level.groups.size()).c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        for (std::uint32_t group = 0; group < level.groups.size(); ++group) {
            drawGroupRow(document, group, grouped[group], owned);
        }
    }
    if (ImGui::CollapsingHeader(std::format("Brushes ({})###brushes", looseBrushes.size()).c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        // Levels can have many thousands, so only the rows on screen are drawn; all are rows for
        // Shift-click ranges.
        const std::size_t first = m_rows.size();
        for (const std::size_t index : looseBrushes) {
            m_rows.push_back(LevelItem::brush(index));
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(looseBrushes.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                drawBrushRow(document, looseBrushes[static_cast<std::size_t>(row)]);
            }
        }
        // drawBrushRow adds its row too; the ones added above already stand for them.
        m_rows.resize(first + looseBrushes.size());
    }
    if (ImGui::CollapsingHeader(
            std::format("Entities ({})###entities", looseEntities.size()).c_str(),
            ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const std::size_t index : looseEntities) {
            drawEntityRow(document, index, owned[index]);
        }
    }

    const bool any = !document.selection().empty();
    if (ImGui::BeginPopupContextWindow("hierarchy")) {
        if (ImGui::MenuItem("Group", "Ctrl+G", false, any)) {
            groupSelection(document);
        }
        if (ImGui::MenuItem("Ungroup", "Ctrl+Shift+G", false, any)) {
            ungroupSelection(document);
        }
        if (ImGui::MenuItem("Rename", "F2", false, any)) {
            startRename(document);
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && !ImGui::GetIO().WantTextInput) {
        startRename(document);
    }
    ImGui::End();
}

void EditorPanels::drawGroupRow(EditorDocument& document, std::uint32_t group,
                                std::span<const LevelItem> members,
                                std::span<const std::vector<std::size_t>> owned) {
    if (drawRenameField(document, true, group, {})) {
        return;
    }
    const bool selected =
        !members.empty() && std::all_of(members.begin(), members.end(), [&](LevelItem member) {
            return document.isSelected(member);
        });
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (members.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    const std::string id = std::format("group{}", group);
    const bool open =
        ImGui::TreeNodeEx(id.c_str(), flags, "%s  (%zu)",
                          document.level().groups[group].name.c_str(), members.size());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selectGroup(document, group, ImGui::GetIO().KeyCtrl);
        m_anchor.reset();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        selectGroup(document, group, false);
        startRename(document);
    }
    if (!open) {
        return;
    }
    for (const LevelItem& member : members) {
        if (member.kind == LevelItem::Kind::Entity) {
            drawEntityRow(document, member.index, owned[member.index]);
        } else {
            drawBrushRow(document, member.index);
        }
    }
    ImGui::TreePop();
}

void EditorPanels::drawEntityRow(EditorDocument& document, std::size_t index,
                                 std::span<const std::size_t> brushes) {
    const LevelItem item = LevelItem::entity(index);
    m_rows.push_back(item);
    if (drawRenameField(document, false, 0, item)) {
        return;
    }
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (document.isSelected(item)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (brushes.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    const std::string id = std::format("entity{}", index);
    const bool open = ImGui::TreeNodeEx(
        id.c_str(), flags, "%s", entityLabel(document.level().entities[index], index).c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        rowClicked(document, item, SelectScope::Entity);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        startRename(document);
    }
    if (!open) {
        return;
    }
    for (const std::size_t brush : brushes) {
        drawBrushRow(document, brush);
    }
    ImGui::TreePop();
}

void EditorPanels::drawBrushRow(EditorDocument& document, std::size_t index) {
    const LevelItem item = LevelItem::brush(index);
    m_rows.push_back(item);
    if (drawRenameField(document, false, 0, item)) {
        return;
    }
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (document.isSelected(item)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const std::string id = std::format("brush{}", index);
    ImGui::TreeNodeEx(id.c_str(), flags, "%s", brushLabel(document.level(), index).c_str());
    if (ImGui::IsItemClicked()) {
        rowClicked(document, item, SelectScope::Item);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        startRename(document);
    }
}

void EditorPanels::rowClicked(EditorDocument& document, LevelItem item, SelectScope scope) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && m_anchor) {
        const auto from = std::find(m_shownRows.begin(), m_shownRows.end(), *m_anchor);
        const auto to = std::find(m_shownRows.begin(), m_shownRows.end(), item);
        if (from != m_shownRows.end() && to != m_shownRows.end()) {
            std::vector<LevelItem> range(std::min(from, to), std::max(from, to) + 1);
            // Entity rows bring their brushes, as a click on one does.
            std::vector<LevelItem> entities;
            for (const LevelItem& row : range) {
                if (row.kind == LevelItem::Kind::Entity) {
                    entities.push_back(row);
                }
            }
            for (const LevelItem& brush : withWholeEntities(document.level(), entities)) {
                range.push_back(brush);
            }
            if (io.KeyCtrl) {
                range.insert(range.end(), document.selection().begin(), document.selection().end());
            }
            document.setSelection(std::move(range));
            return;
        }
    }
    selectClicked(document, item, io.KeyCtrl, scope);
    m_anchor = item;
}

void EditorPanels::startRename(const EditorDocument& document) {
    m_renameProblem.clear();
    if (const std::optional<std::uint32_t> group = selectedGroup(document)) {
        m_renaming = true;
        m_renameIsGroup = true;
        m_renameGroup = *group;
        m_renameText = document.level().groups[*group].name;
    } else if (const std::optional<std::size_t> entity = selectedEntity(document)) {
        m_renaming = true;
        m_renameIsGroup = false;
        m_renameItem = LevelItem::entity(*entity);
        const std::string* name = findProperty(document.level().entities[*entity], "name");
        m_renameText = name != nullptr ? *name : std::string();
    } else if (const std::optional<LevelItem> single = document.singleSelected()) {
        m_renaming = true;
        m_renameIsGroup = false;
        m_renameItem = *single;
        m_renameText = document.level().brushes[single->index].name;
    } else {
        return;
    }
    m_renameFocus = true;
}

bool EditorPanels::drawRenameField(EditorDocument& document, bool isGroup, std::uint32_t group,
                                   LevelItem item) {
    if (!m_renaming || m_renameIsGroup != isGroup ||
        (isGroup ? m_renameGroup != group : !(m_renameItem == item))) {
        return false;
    }
    if (m_renameFocus) {
        ImGui::SetKeyboardFocusHere();
        m_renameFocus = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool entered = ImGui::InputTextWithHint(
        "##rename", isGroup ? "group name" : "name", &m_renameText,
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool left = ImGui::IsItemDeactivated();
    if (!m_renameProblem.empty()) {
        ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "%s", m_renameProblem.c_str());
    }
    if (entered || left) {
        if (!entered && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_renaming = false;
        } else if (isGroup ? renameGroup(document, group, m_renameText, m_renameProblem)
                           : renameItem(document, item, m_renameText, m_renameProblem)) {
            m_renaming = false;
        } else if (entered) {
            m_renameFocus = true; // stay, and say why
        } else {
            m_renaming = false;
        }
    }
    return true;
}

void EditorPanels::drawInspector(EditorDocument& document, const EditorSettings& settings) {
    if (document.selection() != m_editing) {
        m_editing = document.selection();
        m_brushBefore.reset();
        m_activeField = -1;
        m_problem.clear();
        m_newOutput = {};
    }
    if (ImGui::Begin("Inspector")) {
        const std::optional<LevelItem> single = document.singleSelected();
        const std::optional<std::size_t> entity = selectedEntity(document);
        if (document.selection().empty()) {
            ImGui::TextDisabled("Nothing selected.");
            ImGui::TextDisabled("Click a brush or entity in the view or the hierarchy;");
            ImGui::TextDisabled("Ctrl-click or drag a box to select several.");
        } else if (entity) {
            drawEntityInspector(document, *entity);
        } else if (!single) {
            drawMultipleInspector(document);
        } else {
            drawBrushInspector(document, settings, single->index);
        }
        if (!document.selection().empty()) {
            drawSelectionActions(document, settings);
        }
        if (!m_problem.empty()) {
            ImGui::Spacing();
            ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "%s", m_problem.c_str());
        }
    }
    ImGui::End();
}

void EditorPanels::drawBrushInspector(EditorDocument& document, const EditorSettings& settings,
                                      std::size_t index) {
    const Brush& brush = document.level().brushes[index];
    ImGui::Text("Brush %zu", index);
    if (brush.entity < document.level().entities.size()) {
        ImGui::SameLine();
        ImGui::TextDisabled("part of %s %u",
                            document.level().entities[brush.entity].className.c_str(),
                            brush.entity);
    }
    ImGui::Separator();
    constexpr int kBrushNameField = -3;
    std::string name;
    if (textField("Name", kBrushNameField, brush.name, name, "optional")) {
        renameItem(document, LevelItem::brush(index), name, m_problem);
    }

    if (const auto box = boxBounds(brush)) {
        Aabb edited = *box;
        bool changed =
            ImGui::DragFloat3("Min", &edited.min.x, settings.grid() * 0.25f, 0.0f, 0.0f, "%.3f m");
        const bool activated = ImGui::IsItemActivated();
        const bool finished = ImGui::IsItemDeactivatedAfterEdit();
        changed |=
            ImGui::DragFloat3("Max", &edited.max.x, settings.grid() * 0.25f, 0.0f, 0.0f, "%.3f m");
        if (activated || ImGui::IsItemActivated()) {
            m_brushBefore = brush;
        }
        if (changed) {
            const Brush resized = makeBoxBrush(edited.min, edited.max);
            std::string problem;
            if (checkBrush(resized, problem)) {
                document.previewBrush(index, resized);
                m_problem.clear();
            } else {
                m_problem = problem;
            }
        }
        if ((finished || ImGui::IsItemDeactivatedAfterEdit()) && m_brushBefore) {
            const Brush& after = document.level().brushes[index];
            if (!(after == *m_brushBefore)) {
                document.execute(makeSetBrush("Resize brush", index, *m_brushBefore, after));
            }
            m_brushBefore.reset();
        }
        const Vec3 size = edited.max - edited.min;
        ImGui::TextDisabled("Size %.3f x %.3f x %.3f m", static_cast<double>(size.x),
                            static_cast<double>(size.y), static_cast<double>(size.z));
    } else {
        ImGui::TextDisabled("%zu faces, %zu corners:", brush.faces.size(), brush.vertices.size());
        for (const Vec3& vertex : brush.vertices) {
            ImGui::Text("  %.4g  %.4g  %.4g", static_cast<double>(vertex.x),
                        static_cast<double>(vertex.y), static_cast<double>(vertex.z));
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Drag to move, Alt-drag to raise or lower,");
    ImGui::TextDisabled("Shift-drag a face to resize, or press V");
    ImGui::TextDisabled("to drag corners, edges and faces.");
}

void EditorPanels::drawMultipleInspector(EditorDocument& document) {
    if (const std::optional<std::uint32_t> group = selectedGroup(document)) {
        constexpr int kGroupNameField = -4;
        std::string name;
        if (textField("Group", kGroupNameField, document.level().groups[*group].name, name)) {
            renameGroup(document, *group, name, m_problem);
        }
        ImGui::Separator();
    }
    std::size_t brushes = 0;
    for (const LevelItem& item : document.selection()) {
        brushes += item.kind == LevelItem::Kind::Brush ? 1 : 0;
    }
    const std::size_t entities = document.selection().size() - brushes;
    ImGui::Text("%zu brushes, %zu entities", brushes, entities);
    ImGui::Separator();
    if (const auto extent = selectionExtent(document)) {
        const Vec3 size = extent->max - extent->min;
        ImGui::TextDisabled("Extent %.3f x %.3f x %.3f m", static_cast<double>(size.x),
                            static_cast<double>(size.y), static_cast<double>(size.z));
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Drag any of them to move them all.");
}

void EditorPanels::drawSelectionActions(EditorDocument& document, const EditorSettings& settings) {
    ImGui::SeparatorText("Transform");
    const float grid = settings.grid();
    // One row of X, Y and Z buttons per action, each about the centre of the selection.
    const auto axisRow = [&](const char* label, const char* tooltip, auto action) {
        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            ImGui::SameLine(axis == 0 ? ImGui::GetFontSize() * 6.0f : 0.0f);
            if (ImGui::Button(kAxisNames[axis])) {
                action(axis);
            }
            ImGui::SetItemTooltip("%s%s", tooltip, kAxisNames[axis]);
        }
        ImGui::PopID();
    };
    axisRow("Turn +90\xC2\xB0", "Anticlockwise, seen from +",
            [&](std::size_t axis) { rotateSelection(document, axis, 1, grid); });
    axisRow("Turn -90\xC2\xB0", "Clockwise, seen from +",
            [&](std::size_t axis) { rotateSelection(document, axis, -1, grid); });
    axisRow("Flip", "Mirror along ",
            [&](std::size_t axis) { flipSelection(document, axis, grid); });

    ImGui::Spacing();
    if (ImGui::Button("Duplicate")) {
        duplicateSelection(document, {grid, grid, 0.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        deleteSelection(document);
    }

    if (!anyBrushSelected(document)) {
        return;
    }
    ImGui::SeparatorText("Brushes");
    if (ImGui::Button("Make into...")) {
        ImGui::OpenPopup("makeEntity");
    }
    ImGui::SetItemTooltip("Make the selected brushes a door, trigger or other brush entity");
    if (ImGui::BeginPopup("makeEntity")) {
        brushEntityMenuItems(document);
        ImGui::EndPopup();
    }
    const Level& level = document.level();
    const bool anyOwned = std::any_of(document.selection().begin(), document.selection().end(),
                                      [&](const LevelItem& item) {
                                          return item.kind == LevelItem::Kind::Brush &&
                                                 level.brushes[item.index].entity != kNoEntity;
                                      });
    if (anyOwned) {
        ImGui::SameLine();
        if (ImGui::Button("To world")) {
            moveSelectionToWorld(document);
        }
        ImGui::SetItemTooltip("Make the selected brushes world geometry again");
    }
    if (ImGui::Button("Hollow")) {
        hollowSelection(document, grid);
    }
    ImGui::SetItemTooltip("Walls one grid step thick around an empty inside (Ctrl+H)");
    ImGui::SameLine();
    if (ImGui::Button("Merge")) {
        mergeSelection(document);
    }
    ImGui::SetItemTooltip("One brush around all the selected ones (Ctrl+J)");
    ImGui::SameLine();
    if (ImGui::Button("Subtract")) {
        subtractSelection(document);
    }
    ImGui::SetItemTooltip("Cut these out of the brushes they overlap, then delete them (Ctrl+K)");
}

void EditorPanels::commitEntity(EditorDocument& document, std::size_t index,
                                const LevelEntity& edited, const char* name) {
    std::string problem;
    if (!checkEntity(edited, problem)) {
        m_problem = problem;
        return;
    }
    m_problem.clear();
    document.execute(makeSetEntity(name, index, document.level().entities[index], edited));
}

bool EditorPanels::textField(const char* label, int field, const std::string& current,
                             std::string& result, const char* hint) {
    const bool editing = m_activeField == field;
    std::string scratch = current;
    std::string& text = editing ? m_fieldText : scratch;
    if (hint != nullptr) {
        ImGui::InputTextWithHint(label, hint, &text);
    } else {
        ImGui::InputText(label, &text);
    }
    if (ImGui::IsItemActivated()) {
        m_activeField = field;
        m_fieldText = text;
    }
    if (!ImGui::IsItemDeactivated() || !editing) {
        return false;
    }
    m_activeField = -1;
    if (m_fieldText == current) {
        return false;
    }
    result = std::move(m_fieldText);
    return true;
}

void EditorPanels::drawEntityInspector(EditorDocument& document, std::size_t index) {
    const Level& level = document.level();
    const LevelEntity& entity = level.entities[index];
    const EntityClassInfo* entityClass = findEntityClass(entity.className);
    const std::size_t brushCount = entityBrushes(level, index).size();
    ImGui::Text("Entity %zu", index);
    if (brushCount > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%zu %s", brushCount, brushCount == 1 ? "brush" : "brushes");
    }
    ImGui::Separator();

    // Text fields apply when focus leaves them, as one command each. The arrow beside the class
    // lists the classes of the same kind, point or brush.
    constexpr int kClassField = -2;
    const float classWidth = ImGui::CalcItemWidth() - ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(classWidth);
    std::string className;
    bool changeClass = textField("##class", kClassField, entity.className, className);
    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::BeginCombo("Class", nullptr,
                          ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft)) {
        const EntityShape shape = brushCount > 0 ? EntityShape::Brush : EntityShape::Point;
        for (const EntityClassInfo& option : entityClasses()) {
            if (option.shape != shape) {
                continue;
            }
            if (ImGui::Selectable(std::string(option.name).c_str(),
                                  option.name == entity.className)) {
                className = option.name;
                changeClass = true;
            }
            tooltip(option.description);
        }
        ImGui::EndCombo();
    }
    if (changeClass) {
        LevelEntity edited = entity;
        edited.className = className;
        commitEntity(document, index, edited, "Change entity class");
        return;
    }
    if (entityClass != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", std::string(entityClass->description).c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Not a class the engine knows; the game ignores it.");
    }

    drawEntityProperties(document, index);
    drawEntityOutputs(document, index);

    std::string problem;
    if (entityClass != nullptr &&
        !checkEntityClassProperties(document.level().entities[index], *entityClass, problem)) {
        ImGui::Spacing();
        ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "Won't play: %s", problem.c_str());
    }
    ImGui::Spacing();
    if (brushCount > 0) {
        ImGui::TextDisabled("Alt-click one of its brushes to edit just that one.");
    } else {
        ImGui::TextDisabled("Drag in the view to move, Alt-drag to raise or lower.");
    }
}

void EditorPanels::drawEntityProperties(EditorDocument& document, std::size_t index) {
    const LevelEntity& entity = document.level().entities[index];
    const EntityClassInfo* entityClass = findEntityClass(entity.className);
    std::optional<LevelEntity> edited;
    const char* editName = "Edit entity property";
    // Empty text puts a property of the class back to its default.
    const auto setValue = [&](std::string_view key, std::string value) {
        edited = entity;
        if (value.empty()) {
            std::erase_if(edited->properties,
                          [&](const EntityProperty& property) { return property.key == key; });
        } else {
            setProperty(*edited, key, std::move(value));
        }
    };
    const auto keyCell = [](const std::string& key, std::string_view description, bool set) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (set) {
            ImGui::TextUnformatted(key.c_str());
        } else {
            ImGui::TextDisabled("%s", key.c_str());
        }
        tooltip(description);
    };

    if (ImGui::BeginTable("properties", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed);
        const std::span<const PropertyInfo> known =
            entityClass != nullptr ? entityClass->properties : std::span<const PropertyInfo>{};
        for (std::size_t i = 0; i < known.size(); ++i) {
            const PropertyInfo& property = known[i];
            const std::string key(property.key);
            const std::string* value = findProperty(entity, key);
            const int field = 1000 + static_cast<int>(i);
            ImGui::PushID(field);
            keyCell(key, property.description, value != nullptr);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (property.type == PropertyType::Flag) {
                bool on = propertyValue(entity, property) == "1";
                if (ImGui::Checkbox("##value", &on)) {
                    setValue(key, on ? "1" : "0");
                }
            } else if (property.type == PropertyType::Choice) {
                const std::string current(propertyValue(entity, property));
                if (ImGui::BeginCombo("##value", current.c_str())) {
                    for (const std::string_view choice : property.choices) {
                        if (ImGui::Selectable(std::string(choice).c_str(), choice == current)) {
                            setValue(key, std::string(choice));
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                const std::string hint(property.defaultValue);
                std::string text;
                if (textField("##value", field, value != nullptr ? *value : std::string(), text,
                              hint.c_str())) {
                    std::string problem;
                    if (!text.empty() && !checkPropertyValue(property, text, problem)) {
                        m_problem = problem;
                    } else {
                        setValue(key, std::move(text));
                    }
                }
            }
            ImGui::TableNextColumn();
            if (value != nullptr && ImGui::SmallButton("x")) {
                setValue(key, "");
                editName = "Reset entity property";
            }
            if (value != nullptr) {
                ImGui::SetItemTooltip("Back to the default");
            }
            ImGui::PopID();
        }
        // Properties the class doesn't define, or all of them for a class the engine doesn't know.
        for (std::size_t i = 0; i < entity.properties.size(); ++i) {
            const EntityProperty& property = entity.properties[i];
            if (entityClass != nullptr && findPropertyInfo(*entityClass, property.key) != nullptr) {
                continue;
            }
            ImGui::PushID(static_cast<int>(i));
            keyCell(property.key, {}, true);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string value;
            const bool apply = textField("##value", static_cast<int>(i), property.value, value);
            ImGui::TableNextColumn();
            const bool remove = ImGui::SmallButton("x");
            ImGui::PopID();
            if (apply || remove) {
                edited = entity;
                if (remove) {
                    edited->properties.erase(edited->properties.begin() +
                                             static_cast<std::ptrdiff_t>(i));
                    editName = "Remove entity property";
                } else {
                    edited->properties[i].value = value;
                }
            }
        }
        ImGui::EndTable();
    }
    if (edited) {
        commitEntity(document, index, *edited, editName);
        return;
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::InputTextWithHint("##newKey", "key", &m_newKey);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::InputTextWithHint("##newValue", "value", &m_newValue);
    ImGui::SameLine();
    if (ImGui::Button("Add") && !m_newKey.empty()) {
        if (findProperty(entity, m_newKey) != nullptr) {
            m_problem = std::format("'{}' is already set", m_newKey);
        } else {
            LevelEntity added = entity;
            added.properties.push_back({m_newKey, m_newValue});
            commitEntity(document, index, added, "Add entity property");
            if (m_problem.empty()) {
                m_newKey.clear();
                m_newValue.clear();
            }
        }
    }
}

void EditorPanels::drawEntityOutputs(EditorDocument& document, std::size_t index) {
    const Level& level = document.level();
    const LevelEntity& entity = level.entities[index];
    const EntityClassInfo* entityClass = findEntityClass(entity.className);
    const std::span<const SignalInfo> events =
        entityClass != nullptr ? entityClass->events : std::span<const SignalInfo>{};
    if (events.empty() && entity.outputs.empty()) {
        return;
    }
    ImGui::SeparatorText("Outputs");
    ImGui::TextDisabled("When this happens, send the input to every entity with the name.");
    const std::vector<std::string> names = entityNames(level);

    // A name field, with an arrow listing the names in the level.
    const auto targetField = [&](int field, const std::string& current, std::string& chosen) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight());
        bool picked = textField("##target", field, current, chosen, "name");
        ImGui::SameLine(0.0f, 0.0f);
        if (ImGui::BeginCombo("##targets", nullptr,
                              ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft)) {
            for (const std::string& name : names) {
                if (ImGui::Selectable(name.c_str(), name == current)) {
                    chosen = name;
                    picked = true;
                }
            }
            if (names.empty()) {
                ImGui::TextDisabled("No entity has a name yet");
            }
            ImGui::EndCombo();
        }
        return picked;
    };
    // The inputs the target's entities accept, or free text when no entity has its name.
    const auto inputField = [&](int field, const std::string& target, const std::string& current,
                                std::string& chosen) {
        const std::vector<SignalInfo> inputs = inputsOf(level, target);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (inputs.empty()) {
            return textField("##input", field, current, chosen, "input");
        }
        return signalCombo("##input", current, inputs, chosen);
    };
    const auto delayField = [&](int field, float delay, float& chosen) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        std::string text;
        if (!textField("##delay", field, formatFloat(delay), text)) {
            return false;
        }
        float seconds = 0.0f;
        if (!parseFloat(text, seconds) || !(seconds >= 0.0f && seconds <= kMaxOutputDelay)) {
            m_problem = std::format("A delay is 0 to {} seconds.", kMaxOutputDelay);
            return false;
        }
        chosen = seconds;
        return true;
    };

    std::optional<LevelEntity> edited;
    const char* editName = "Edit output";
    bool adding = false;
    if (ImGui::BeginTable("outputs", 5, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Delay", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < entity.outputs.size(); ++i) {
            const EntityOutput& output = entity.outputs[i];
            const int field = 3000 + static_cast<int>(i) * 4;
            ImGui::PushID(field);
            ImGui::TableNextRow();
            EntityOutput changed = output;
            bool change = false;
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            change |= signalCombo("##event", output.event, events, changed.event);
            ImGui::TableNextColumn();
            change |= targetField(field, output.target, changed.target);
            ImGui::TableNextColumn();
            change |= inputField(field + 1, output.target, output.input, changed.input);
            ImGui::TableNextColumn();
            change |= delayField(field + 2, output.delay, changed.delay);
            ImGui::TableNextColumn();
            const bool remove = ImGui::SmallButton("x");
            ImGui::PopID();
            if (change || remove) {
                edited = entity;
                if (remove) {
                    edited->outputs.erase(edited->outputs.begin() + static_cast<std::ptrdiff_t>(i));
                    editName = "Remove output";
                } else {
                    edited->outputs[i] = changed;
                }
            }
        }

        // The last row makes a new output.
        if (m_newOutput.event.empty() && !events.empty()) {
            m_newOutput.event = events.front().name;
        }
        ImGui::PushID("new");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        signalCombo("##event", m_newOutput.event, events, m_newOutput.event);
        ImGui::TableNextColumn();
        targetField(4000, m_newOutput.target, m_newOutput.target);
        ImGui::TableNextColumn();
        inputField(4001, m_newOutput.target, m_newOutput.input, m_newOutput.input);
        ImGui::TableNextColumn();
        delayField(4002, m_newOutput.delay, m_newOutput.delay);
        ImGui::TableNextColumn();
        adding = ImGui::SmallButton("+");
        ImGui::SetItemTooltip("Add this output");
        ImGui::PopID();
        ImGui::EndTable();
    }
    if (adding && !edited) {
        edited = entity;
        edited->outputs.push_back(m_newOutput);
        editName = "Add output";
    }
    if (edited) {
        commitEntity(document, index, *edited, editName);
        if (adding && m_problem.empty()) {
            m_newOutput = {};
        }
    }
}

} // namespace ct
