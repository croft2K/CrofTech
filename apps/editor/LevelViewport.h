#pragma once

#include "croftech/core/Camera.h"
#include "croftech/core/Matrix.h"
#include "croftech/editor/BrushEditing.h"
#include "croftech/editor/EditorActions.h"
#include "croftech/editor/EditorDocument.h"
#include "croftech/editor/EntityLinks.h"
#include "croftech/editor/SelectionDrag.h"
#include "croftech/editor/Shapes.h"
#include "croftech/net/LoopbackLink.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace ct {

// The part of the window a view covers, in ImGui coordinates.
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

// Which way a new wedge or stairs rise and an arch is walked through.
enum class ShapeFacing { AwayFromView, PlusX, PlusY, MinusX, MinusY };

struct EditorSettings {
    std::size_t gridIndex = 3;     // 0.25 m
    std::size_t rotationIndex = 2; // 15 degrees
    ShapeKind shape = ShapeKind::Box;
    ShapeSettings shapeSettings; // forward comes from facing when a shape is drawn
    ShapeFacing facing = ShapeFacing::AwayFromView;
    // The player's and each enemy's collision box, so spawn and enemy markers show the sizes the
    // game uses.
    BodySizes bodies;
    // Vertex tool handles behind a surface are hidden in 3D panes, and can't be picked there.
    bool hideHandlesBehindSurfaces = true;
    // Wires are drawn for the selected entities, or with this for every entity.
    bool showAllWires = false;
    // The navigation mesh enemies walk, drawn in 3D panes and in play.
    bool showNavMesh = false;
    // In play, what each enemy is thinking (EnemyThoughts.h).
    bool showEnemyThoughts = false;
    // Moving the mouse forward looks down, flying the 3D camera and in play; starts as
    // client.invert_mouse in config/client.cfg.
    bool invertMouse = false;
    // How the in-memory link behaves in play-in-editor.
    LinkConditions playConditions;

    float grid() const { return kGridSizes[gridIndex]; }
    double rotationStep() const { return kRotationSteps[rotationIndex]; }
};

// Select, Move, Rotate and Scale all select and drag items the same way; the last three add a
// gizmo on the selection. Vertex reshapes the selected brushes by their corners, edges and faces;
// Clip cuts them with a plane. Draw makes a new shape (EditorSettings::shape).
enum class EditorTool { Select, Move, Rotate, Scale, Vertex, Clip, Draw };

// What a pane of the view shows: the 3D fly camera, or the level seen straight along an axis
// without perspective: from above (X right, Y up), from the front (X right, Z up, looking along +Y)
// or from the side (Y right, Z up, looking along -X).
enum class ViewMode { Perspective, Top, Front, Side };
inline constexpr ViewMode kViewModes[]{ViewMode::Perspective, ViewMode::Top, ViewMode::Front,
                                       ViewMode::Side};
const char* viewModeName(ViewMode mode);

// One pane filling the view, or four: 3D, top, front and side.
enum class ViewLayout { One, Four };

// Camera movement while the right mouse button is held.
struct FlyInput {
    float lookX = 0.0f; // mouse counts this frame
    float lookY = 0.0f;
    bool invert = false; // moving the mouse forward looks down (orthographic panning is unchanged)
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool fast = false;
    float seconds = 0.0f;
};

// The view of the level being edited: one or four panes (each 3D or orthographic), picking, and
// the mouse tools, which work in whichever pane the mouse is in. In a 3D pane the right mouse
// button flies the camera; in an orthographic pane it pans, and the wheel zooms about the mouse.
// Drags in an orthographic pane move things across the screen, in the plane the pane shows.
//
// Click selects a brush or entity; Ctrl-click adds or removes it. Dragging a selected item moves
// the whole selection across the grid (Alt: up and down); dragging from anywhere else selects
// everything inside the rectangle (Ctrl adds). Shift-drag a face of a lone selected brush to move
// just that face, or Ctrl+Shift-drag it to pull a new brush out of it. The Move, Rotate and Scale
// gizmos act on the whole selection: arrows and squares move along an axis or plane, rings turn
// about an axis in snapped steps, and the handles on the selection's box drag its sides. Every drag
// shows live and finishes as one undoable command; Esc cancels it.
//
// Vertex: handles on the selected brushes' corners and on the middles of their edges and faces.
// Click one to select its corners (Ctrl adds or removes), drag a box to select the corners inside
// it, and drag a selected handle to move the selected corners across the grid (Alt: up and down).
// Corners at the same place in several selected brushes move together.
//
// Clip: click two or three points on surfaces (two make a vertical cut through their line). The
// selected brushes the plane crosses show the part kept and the part cut away; Tab switches between
// keeping the back, the front or both, Enter cuts, Backspace takes back the last point.
//
// Draw: drag a rectangle on the surface under the mouse, release, move the mouse up to set the
// height and click (or press Enter) to place the shape; in an orthographic pane the rectangle is
// drawn on the screen and the shape is placed on release, as deep as the last one was high,
// towards the camera from the surface under the rectangle's first corner. Until something else is
// edited, changing the shape settings remakes the shape just placed.
class LevelViewport {
public:
    // Every pane starts looking at what the camera sees.
    explicit LevelViewport(Camera camera);

    ViewLayout layout() const { return m_layout; }
    void setLayout(ViewLayout layout);
    // The pane the mouse was last over, which flying, framing and nudging act on.
    ViewMode activeMode() const { return m_panes[m_active].mode; }
    // The camera of the active pane if it is 3D, or else of the first 3D pane; null if none is.
    const Camera* camera3D() const;
    void setActiveMode(ViewMode mode) { m_panes[m_active].mode = mode; }

    // Flies the active pane's camera, or pans it if it is orthographic.
    void fly(const FlyInput& input);
    // Brings a point into the middle of the active pane (a 3D pane backs off by distance).
    void frame(Vec3 point, float distance);

    // What the renderer draws in each pane this frame. region is the whole view.
    struct PaneDraw {
        Mat4 viewProjection;
        ViewRect rect;
        bool orthographic = false;
    };
    std::vector<PaneDraw> paneDraws(const ViewRect& region) const;
    // A button in each pane's corner to choose what it shows.
    void drawPaneControls(const ViewRect& region);

    EditorTool tool() const { return m_tool; }
    // The Vertex tool's selected corners, by position.
    const std::vector<Vec3>& selectedCorners() const { return m_corners; }
    void setSelectedCorners(std::vector<Vec3> corners) { m_corners = std::move(corners); }
    // Clears the current tool's own selection (clip points, then selected corners), if it has
    // one: the first thing Esc does.
    bool clearToolState();
    void setTool(EditorTool tool) { m_tool = tool; }
    bool isDragging() const { return m_drag.kind != DragKind::None; }
    // Restores what the current drag changed.
    void cancelDrag(EditorDocument& document);

    // Mouse editing for this frame. Call between DevUi::beginFrame() and endFrame(), with
    // mouseOverView true only when the mouse is over the view and not over a panel. region is the
    // whole view, which the panes share.
    void update(EditorDocument& document, const EditorSettings& settings, const ViewRect& region,
                bool mouseOverView);
    // Entity markers, selection outlines, gizmos and tool previews over every pane.
    void drawOverlay(const EditorDocument& document, const EditorSettings& settings,
                     const ViewRect& region) const;
    // Entities outlined besides the selection: those running the script being edited.
    void setHighlighted(std::vector<std::uint32_t> entities) {
        m_highlighted = std::move(entities);
    }

    // Where a new entity goes: the surface under the middle of the active pane, or a few metres
    // ahead.
    Vec3 placementPoint(const EditorDocument& document, const EditorSettings& settings) const;
    // The grid axes the arrow keys nudge along in the active pane: up (away from the camera in 3D)
    // and right.
    std::pair<Vec3, Vec3> nudgeAxes() const;

private:
    // ShapeBase is the rectangle drag; ShapeHeight follows the mouse, with no button held,
    // until a click places the shape.
    enum class DragKind {
        None,
        Marquee,
        Move,
        MoveVertical,
        Face,
        Corners,
        Extrude,
        ShapeBase,
        ShapeHeight,
        Gizmo,
        MoverEnd
    };

    struct Pick {
        LevelItem target;
        float distance = 0.0f;
        std::size_t face = 0; // brushes only
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

    // A Vertex tool handle: a corner, or the middle of an edge or face, and the corners it moves.
    struct VertexHandle {
        enum class Kind { Corner, Edge, Face };
        Kind kind = Kind::Corner;
        Vec3 position;
        std::vector<Vec3> corners;
    };

    struct Drag {
        DragKind kind = DragKind::None;
        LevelItem target;
        Brush brushBefore; // Face
        std::optional<SelectionDrag> selection;
        Vec3 grabPoint;
        std::size_t face = 0;      // Face: in brushBefore
        std::size_t movedFace = 0; // Face: the same face in the previewed brush
        Vec3 boxStart;             // ShapeBase and ShapeHeight: the base rectangle's corners
        Vec3 boxEnd;
        float height = 0.0f;
        std::optional<float> heightStart; // where along the vertical the mouse began
        Vec3 forward;                     // the shape's facing, fixed when the drag began
        float screenStartX = 0.0f;        // Marquee, Ring
        float screenStartY = 0.0f;
        bool additive = false;            // Marquee with Ctrl
        bool single = false;              // Marquee with Alt: items, not their whole entities
        bool selectCorners = false;       // Marquee in the Vertex tool
        std::vector<Vec3> corners;        // Corners: the corners moving, where they started
        Vec3 cornerOffset;                // Corners: the move shown now
        bool cornerOnGrid = false;        // Corners: a corner was grabbed, so it lands on the grid
        bool vertical = false;            // Corners with Alt
        float extrudeDistance = 0.0f;     // Extrude: how far out the new brush reaches
        std::size_t depthAxis = 2;        // ShapeBase, ShapeHeight: the axis the height runs along
        float depthSign = 1.0f;           // and which way
        std::optional<LevelItem> clicked; // Marquee: the item under the mouse when it began
        GizmoHandle handle;
        Aabb extent; // the selection's extent when a gizmo drag began
        Vec3 gizmoCentre;
        float axisStart = 0.0f;
        double angle = 0.0; // Ring: degrees turned so far, unsnapped
        double lastScreenAngle = 0.0;
        std::size_t entity = 0;   // MoverEnd
        LevelEntity entityBefore; // MoverEnd
        Vec3 moveBefore;          // MoverEnd
    };

    // The shape placed last, which the shape settings can still remake.
    struct PlacedShape {
        Aabb box;
        ShapeKind kind = ShapeKind::Box;
        ShapeSettings settings;
        std::uint64_t stateId = 0; // the history state right after placing it
    };

    struct Ray {
        Vec3 origin;
        Vec3 direction;
    };

    struct ViewPane {
        ViewMode mode = ViewMode::Perspective;
        Camera camera;                // Perspective
        Vec3 centre;                  // orthographic: the point in the middle of the pane
        float metresPerPixel = 0.03f; // orthographic zoom
    };

    // How a pane maps the world to its part of the screen this frame.
    struct PaneView {
        Mat4 viewProjection;
        ViewRect rect;
        bool orthographic = false;
        Camera camera; // perspective
        Vec3 eye;      // the camera, or for an orthographic pane a point far behind the centre
        Vec3 forward;
        Vec3 right;
        Vec3 up;
        float metresPerPixel = 0.0f; // orthographic
        Vec3 centre;                 // orthographic

        Ray rayThrough(float screenX, float screenY) const;
    };

    // Where the gizmo sits and how big it is, for the current selection.
    struct GizmoFrame {
        Aabb extent;
        Vec3 centre;
        float size = 1.0f; // metres, so the gizmo keeps one size on screen
    };

    std::size_t paneCount() const { return m_layout == ViewLayout::Four ? 4 : 1; }
    ViewRect paneRect(const ViewRect& region, std::size_t pane) const;
    PaneView paneView(const ViewRect& region, std::size_t pane) const;
    PaneView activeView() const { return paneView(m_region, m_active); }
    // The plane drags move things in: horizontal in 3D, the screen in an orthographic pane.
    Vec3 dragPlaneNormal() const;
    void zoom(const ViewRect& region, float wheel);
    void drawPane(const EditorDocument& document, const EditorSettings& settings,
                  const PaneView& view, bool active) const;
    // Whether a point on the selected brushes can be seen from the pane, not hidden behind a
    // surface. Always true in orthographic panes.
    bool handleVisible(const EditorDocument& document, const PaneView& view, Vec3 point) const;

    Ray mouseRay() const;
    std::optional<Pick> pick(const EditorDocument& document, const EditorSettings& settings,
                             const Ray& ray) const;
    // A point on the surface under the ray: a brush, else the ground plane at z = 0.
    std::optional<Vec3> surfacePoint(const EditorDocument& document, const Ray& ray) const;

    std::optional<GizmoFrame> gizmoFrame(const EditorDocument& document,
                                         const PaneView& view) const;
    std::optional<GizmoHandle> pickGizmo(const EditorDocument& document) const;
    // The selected mover, when its end handle is shown: with every tool but Clip and Shape.
    std::optional<MoverPath> shownMover(const EditorDocument& document) const;
    bool pickMoverEnd(const EditorDocument& document) const;

    void beginDrag(EditorDocument& document, const EditorSettings& settings, const Ray& ray);
    void beginGizmoDrag(EditorDocument& document, const GizmoHandle& handle, const Ray& ray);
    void beginCornerDrag(EditorDocument& document, const VertexHandle& handle);
    void continueCornerDrag(EditorDocument& document, const EditorSettings& settings,
                            const Ray& ray);

    // Every handle of the selected brushes, one per place.
    std::vector<VertexHandle> vertexHandles(const EditorDocument& document) const;
    std::optional<VertexHandle> pickVertexHandle(const EditorDocument& document,
                                                 const EditorSettings& settings) const;
    bool isCornerSelected(Vec3 corner) const;

    // The plane through the clip points; nullopt until there are two that make one.
    std::optional<DPlane> clipPlane() const;
    void clipKeys(EditorDocument& document);
    void continueDrag(EditorDocument& document, const EditorSettings& settings, const Ray& ray);
    void continueGizmoDrag(EditorDocument& document, const EditorSettings& settings,
                           const Ray& ray);
    void endDrag(EditorDocument& document, const EditorSettings& settings);
    Aabb shapeBox() const;
    ShapeSettings shapeSettings(const EditorSettings& settings, Vec3 awayForward) const;
    void placeShape(EditorDocument& document, const EditorSettings& settings);
    // Remakes the shape placed last when the settings changed since.
    void adjustPlacedShape(EditorDocument& document, const EditorSettings& settings);
    void finishMarquee(const Drag& drag, EditorDocument& document, const EditorSettings& settings);

    std::array<ViewPane, 4> m_panes;
    ViewLayout m_layout = ViewLayout::One;
    std::size_t m_active = 0;
    ViewRect m_region; // the whole view, as of the last update
    EditorTool m_tool = EditorTool::Select;
    Drag m_drag;
    std::optional<Pick> m_hover;
    std::optional<GizmoHandle> m_hoverHandle;
    std::optional<VertexHandle> m_hoverVertex;
    bool m_hoverEnd = false; // the mouse is over the selected mover's end handle
    std::vector<Vec3> m_corners;
    std::vector<std::uint32_t> m_highlighted;
    std::vector<Vec3> m_clipPoints;
    ClipKeep m_clipKeep = ClipKeep::Back;
    float m_shapeHeight = 1.0f; // new shapes start as tall as the last one
    std::optional<PlacedShape> m_placedShape;
};

} // namespace ct
