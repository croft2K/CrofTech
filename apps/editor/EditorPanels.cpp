#include "EditorPanels.h"

#include "croftech/editor/EditorActions.h"
#include "croftech/editor/LevelCommands.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace ct {
namespace {

std::string brushLabel(const Brush& brush, std::size_t index) {
    if (const auto box = boxBounds(brush)) {
        const Vec3 size = box->max - box->min;
        return std::format("Brush {}  box {:g} x {:g} x {:g} m##{}", index, size.x, size.y, size.z,
                           index);
    }
    return std::format("Brush {}  {} faces##{}", index, brush.faces.size(), index);
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
        ImGui::Separator();
        requests.addPlayerSpawn = ImGui::MenuItem("Player spawn");
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

void EditorPanels::drawToolbar(LevelViewport& viewport, EditorSettings& settings,
                               const ViewRect& view) {
    if (view.width <= 0.0f || view.height <= 0.0f) {
        return;
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
        return;
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
    if (drawing) {
        drawShapeSettings(settings);
    }
    ImGui::End();
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
    // Click selects one item; Ctrl-click adds or removes it.
    const auto clicked = [&](LevelItem item) {
        if (ImGui::GetIO().KeyCtrl) {
            document.toggleSelected(item);
        } else {
            document.select(item);
        }
    };
    if (ImGui::CollapsingHeader(std::format("Brushes ({})###brushes", level.brushes.size()).c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(level.brushes.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto i = static_cast<std::size_t>(row);
                if (ImGui::Selectable(brushLabel(level.brushes[i], i).c_str(),
                                      document.isSelected(LevelItem::brush(i)))) {
                    clicked(LevelItem::brush(i));
                }
            }
        }
    }
    if (ImGui::CollapsingHeader(
            std::format("Entities ({})###entities", level.entities.size()).c_str(),
            ImGuiTreeNodeFlags_DefaultOpen)) {
        for (std::size_t i = 0; i < level.entities.size(); ++i) {
            const std::string label =
                std::format("Entity {}  {}##entity{}", i, level.entities[i].className, i);
            if (ImGui::Selectable(label.c_str(), document.isSelected(LevelItem::entity(i)))) {
                clicked(LevelItem::entity(i));
            }
        }
    }
    ImGui::End();
}

void EditorPanels::drawInspector(EditorDocument& document, const EditorSettings& settings) {
    if (document.selection() != m_editing) {
        m_editing = document.selection();
        m_brushBefore.reset();
        m_activeField = -1;
        m_problem.clear();
    }
    if (ImGui::Begin("Inspector")) {
        const std::optional<LevelItem> single = document.singleSelected();
        if (document.selection().empty()) {
            ImGui::TextDisabled("Nothing selected.");
            ImGui::TextDisabled("Click a brush or entity in the view or the hierarchy;");
            ImGui::TextDisabled("Ctrl-click or drag a box to select several.");
        } else if (!single) {
            drawMultipleInspector(document);
        } else if (single->kind == LevelItem::Kind::Brush) {
            drawBrushInspector(document, settings, single->index);
        } else {
            drawEntityInspector(document, single->index);
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
    ImGui::Separator();

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

    ImGui::SeparatorText("Brushes");
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
                             std::string& result) {
    const bool editing = m_activeField == field;
    std::string scratch = current;
    std::string& text = editing ? m_fieldText : scratch;
    ImGui::InputText(label, &text);
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
    const LevelEntity& entity = document.level().entities[index];
    ImGui::Text("Entity %zu", index);
    ImGui::Separator();

    // Text fields apply when focus leaves them, as one command each.
    constexpr int kClassField = -2;
    std::string className;
    if (textField("Class", kClassField, entity.className, className)) {
        LevelEntity edited = entity;
        edited.className = className;
        commitEntity(document, index, edited, "Change entity class");
        return;
    }

    if (ImGui::BeginTable("properties", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed);
        for (std::size_t i = 0; i < entity.properties.size(); ++i) {
            const EntityProperty& property = entity.properties[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(property.key.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string value;
            const bool apply = textField("##value", static_cast<int>(i), property.value, value);
            ImGui::TableNextColumn();
            const bool remove = ImGui::SmallButton("x");
            ImGui::PopID();
            if (apply || remove) {
                LevelEntity edited = entity;
                if (remove) {
                    edited.properties.erase(edited.properties.begin() +
                                            static_cast<std::ptrdiff_t>(i));
                } else {
                    edited.properties[i].value = value;
                }
                commitEntity(document, index, edited,
                             remove ? "Remove entity property" : "Edit entity property");
                ImGui::EndTable();
                return;
            }
        }
        ImGui::EndTable();
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
            LevelEntity edited = entity;
            edited.properties.push_back({m_newKey, m_newValue});
            commitEntity(document, index, edited, "Add entity property");
            if (m_problem.empty()) {
                m_newKey.clear();
                m_newValue.clear();
            }
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Drag in the view to move, Alt-drag to raise or lower.");
}

} // namespace ct
