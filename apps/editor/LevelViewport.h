#pragma once

#include "croftech/core/Camera.h"
#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/EditorDocument.h"

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

struct EditorSettings {
    std::size_t gridIndex = 3; // 0.25 m
    // The player's collision box, so spawn markers show the size the game uses.
    Vec3 playerHalfExtents{0.4f, 0.4f, 0.9f};

    float grid() const { return kGridSizes[gridIndex]; }
};

enum class EditorTool { Select, Box };

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
// Select tool: click selects a brush or entity; drag moves it across the grid (hold Alt to move
// it up and down); Shift-drag a face of the selected brush to move just that face. Box tool:
// drag out a rectangle on the surface under the mouse to create a box brush. Every drag shows
// live and finishes as one undoable command; Esc cancels it.
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
    // Entity markers, selection outlines and tool previews over the 3D view.
    void drawOverlay(const EditorDocument& document, const EditorSettings& settings,
                     const ViewRect& rect) const;

    // Where a new entity goes: the surface under the middle of the view, or a few metres ahead.
    Vec3 placementPoint(const EditorDocument& document, const EditorSettings& settings) const;

private:
    enum class DragKind { None, Move, MoveVertical, Face, Box };

    struct Pick {
        Selection target;
        float distance = 0.0f;
        std::size_t plane = 0; // brushes only
    };

    struct Drag {
        DragKind kind = DragKind::None;
        Selection target;
        Brush brushBefore;
        LevelEntity entityBefore;
        Vec3 grabPoint;
        std::size_t plane = 0;
        Vec3 boxStart;
        Vec3 boxEnd;
    };

    struct Ray {
        Vec3 origin;
        Vec3 direction;
    };

    Ray mouseRay(const ViewRect& rect) const;
    std::optional<Pick> pick(const EditorDocument& document, const EditorSettings& settings,
                             const Ray& ray) const;
    // A point on the surface under the ray: a brush, else the ground plane at z = 0.
    std::optional<Vec3> surfacePoint(const EditorDocument& document, const Ray& ray) const;

    void beginDrag(EditorDocument& document, const EditorSettings& settings, const Ray& ray);
    void continueDrag(EditorDocument& document, const EditorSettings& settings, const Ray& ray);
    void endDrag(EditorDocument& document, const EditorSettings& settings);

    Camera m_camera;
    EditorTool m_tool = EditorTool::Select;
    Drag m_drag;
    std::optional<Pick> m_hover;
};

// The box drawn for an entity: a player-sized box standing on a spawn point, a small cube for
// anything else. Nullopt if the entity has no readable origin.
std::optional<Aabb> entityMarker(const LevelEntity& entity, const EditorSettings& settings);

} // namespace ct
