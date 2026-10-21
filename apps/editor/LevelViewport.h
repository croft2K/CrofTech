#pragma once

#include "croftech/core/Camera.h"
#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/editor/SelectionDrag.h"

#include <array>
#include <cstddef>
#include <optional>

namespace ct {

// The part of the window the 3D view covers, in ImGui coordinates.
struct ViewRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool contains(float px, float py) const {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
    float aspect() const { return height > 0.0f ? width / height : 1.0f; }
};

// Rotation snapping steps in degrees, smallest first.
inline constexpr std::array<double, 5> kRotationSteps{1.0, 5.0, 15.0, 45.0, 90.0};

struct EditorSettings {
    std::size_t gridIndex = 3;     // 0.25 m
    std::size_t rotationIndex = 2; // 15 degrees
    // The player's collision box, so spawn markers show the size the game uses.
    Vec3 playerHalfExtents{0.4f, 0.4f, 0.9f};

    float grid() const { return kGridSizes[gridIndex]; }
    double rotationStep() const { return kRotationSteps[rotationIndex]; }
};

// Select, Move, Rotate and Scale all select and drag items the same way; the last three add a
// gizmo on the selection. Box draws a new box brush.
enum class EditorTool { Select, Move, Rotate, Scale, Box };

// Camera movement while the right mouse button is held.
struct FlyInput {
    float lookX = 0.0f; // mouse counts this frame
    float lookY = 0.0f;
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool fast = false;
    float seconds = 0.0f;
};

// The 3D view of the level being edited: a fly camera, picking, and the mouse tools.
//
// Click selects a brush or entity; Ctrl-click adds or removes it. Dragging a selected item moves
// the whole selection across the grid (Alt: up and down); dragging from anywhere else selects
// everything inside the rectangle (Ctrl adds). Shift-drag a face of a lone selected brush to move
// just that face. The Move, Rotate and Scale gizmos act on the whole selection: arrows and squares
// move along an axis or plane, rings turn about an axis in snapped steps, and the handles on the
// selection's box drag its sides. Every drag shows live and finishes as one undoable command; Esc cancels it.
class LevelViewport {
public:
    explicit LevelViewport(Camera camera) : m_camera(camera) {}

    const Camera& camera() const { return m_camera; }
    void fly(const FlyInput& input);
    // Moves the camera back from a point so it is in view.
    void frame(Vec3 point, float distance);

    EditorTool tool() const { return m_tool; }
    void setTool(EditorTool tool) { m_tool = tool; }
    bool isDragging() const { return m_drag.kind != DragKind::None; }
    // Restores what the current drag changed.
    void cancelDrag(EditorDocument& document);

    // Mouse editing for this frame. Call between DevUi::beginFrame() and endFrame(), with
    // mouseOverView true only when the mouse is over the view and not over a panel.
    void update(EditorDocument& document, const EditorSettings& settings, const ViewRect& rect,
                bool mouseOverView);
    // Entity markers, selection outlines, gizmos and tool previews over the 3D view.
    void drawOverlay(const EditorDocument& document, const EditorSettings& settings,
                     const ViewRect& rect) const;

    // Where a new entity goes: the surface under the middle of the view, or a few metres ahead.
    Vec3 placementPoint(const EditorDocument& document, const EditorSettings& settings) const;
    // The world axis (0 = X, 1 = Y) closest to the camera's horizontal facing, and its sign:
    // arrow keys nudge along it.
    Vec3 forwardOnGrid() const;

private:
    enum class DragKind { None, Marquee, Move, MoveVertical, Face, Box, Gizmo };

    struct Pick {
        LevelItem target;
        float distance = 0.0f;
        std::size_t plane = 0; // brushes only
    };

    // A part of the gizmo: an arrow or plane square (Move), a ring (Rotate) or a side handle
    // (Scale). axis is the arrow's or ring's axis, or the normal of the plane square.
    struct GizmoHandle {
        enum class Kind { Arrow, Plane, Ring, Side };
        Kind kind = Kind::Arrow;
        std::size_t axis = 0;
        bool maxSide = false; // Side: the max side of the box on this axis, else the min side

        friend bool operator==(const GizmoHandle&, const GizmoHandle&) = default;
    };

    struct Drag {
        DragKind kind = DragKind::None;
        LevelItem target;
        Brush brushBefore; // Face
        std::optional<SelectionDrag> selection;
        Vec3 grabPoint;
        std::size_t plane = 0;
        Vec3 boxStart;
        Vec3 boxEnd;
        float screenStartX = 0.0f; // Marquee, Ring
        float screenStartY = 0.0f;
        bool additive = false;            // Marquee with Ctrl
        std::optional<LevelItem> clicked; // Marquee: the item under the mouse when it began
        GizmoHandle handle;
        Aabb extent; // the selection's extent when a gizmo drag began
        Vec3 gizmoCentre;
        float axisStart = 0.0f;
        double angle = 0.0; // Ring: degrees turned so far, unsnapped
        double lastScreenAngle = 0.0;
    };

    struct Ray {
        Vec3 origin;
        Vec3 direction;
    };

    // Where the gizmo sits and how big it is, for the current selection.
    struct GizmoFrame {
        Aabb extent;
        Vec3 centre;
        float size = 1.0f; // metres, so the gizmo keeps one size on screen
    };

    Ray mouseRay(const ViewRect& rect) const;
    std::optional<Pick> pick(const EditorDocument& document, const EditorSettings& settings,
                             const Ray& ray) const;
    // A point on the surface under the ray: a brush, else the ground plane at z = 0.
    std::optional<Vec3> surfacePoint(const EditorDocument& document, const Ray& ray) const;

    std::optional<GizmoFrame> gizmoFrame(const EditorDocument& document) const;
    std::optional<GizmoHandle> pickGizmo(const EditorDocument& document,
                                         const ViewRect& rect) const;

    void beginDrag(EditorDocument& document, const EditorSettings& settings, const ViewRect& rect,
                   const Ray& ray);
    void beginGizmoDrag(EditorDocument& document, const GizmoHandle& handle, const ViewRect& rect,
                        const Ray& ray);
    void continueDrag(EditorDocument& document, const EditorSettings& settings, const Ray& ray);
    void continueGizmoDrag(EditorDocument& document, const EditorSettings& settings,
                           const Ray& ray);
    void endDrag(EditorDocument& document, const EditorSettings& settings, const ViewRect& rect);
    void finishMarquee(const Drag& drag, EditorDocument& document, const EditorSettings& settings,
                       const ViewRect& rect) const;

    Camera m_camera;
    EditorTool m_tool = EditorTool::Select;
    Drag m_drag;
    std::optional<Pick> m_hover;
    std::optional<GizmoHandle> m_hoverHandle;
};

} // namespace ct
