#include "LevelViewport.h"

#include "croftech/editor/BrushOperations.h"
#include "croftech/editor/EditorActions.h"
#include "croftech/editor/LevelCommands.h"
#include "croftech/level/BrushGeometry.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <string>
#include <tuple>
#include <vector>

namespace ct {
namespace {

constexpr float kLookRadiansPerCount = 0.0035f;
constexpr float kFlySpeed = 8.0f; // m/s
constexpr float kFastFlyFactor = 4.0f;
constexpr float kMaxPitch = 1.55f;
constexpr float kPlacementDistance = 4.0f;
constexpr float kMarqueeMinPixels = 4.0f; // a smaller drag on empty space is a click
// Gizmo size as a fraction of its distance from the camera, so it keeps one size on screen.
constexpr float kGizmoScale = 0.18f;
constexpr float kPlaneHandleNear = 0.25f; // plane squares span this to kPlaneHandleFar of size
constexpr float kPlaneHandleFar = 0.45f;
constexpr int kRingSegments = 64;

constexpr ImU32 kHoverColor = IM_COL32(255, 255, 255, 140);
constexpr ImU32 kSelectedColor = IM_COL32(255, 210, 40, 255);
constexpr ImU32 kFaceColor = IM_COL32(80, 200, 255, 255);
constexpr ImU32 kSpawnColor = IM_COL32(90, 230, 120, 230);
constexpr ImU32 kEntityColor = IM_COL32(255, 150, 60, 230);
constexpr ImU32 kInvalidColor = IM_COL32(255, 80, 60, 255);
constexpr ImU32 kMarqueeColor = IM_COL32(120, 180, 255, 255);
constexpr ImU32 kMarqueeFill = IM_COL32(120, 180, 255, 40);
constexpr ImU32 kExtentColor = IM_COL32(255, 255, 255, 110);
constexpr ImU32 kHandleColor = IM_COL32(230, 230, 230, 200);
constexpr ImU32 kHandleHoverColor = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kGizmoActiveColor = IM_COL32(255, 230, 80, 255);
constexpr std::array<ImU32, 3> kAxisColors{IM_COL32(235, 75, 75, 255), IM_COL32(115, 205, 65, 255),
                                           IM_COL32(75, 135, 245, 255)};

float component(Vec3 v, std::size_t axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

void setComponent(Vec3& v, std::size_t axis, float value) {
    (axis == 0 ? v.x : (axis == 1 ? v.y : v.z)) = value;
}

Vec3 axisVector(std::size_t axis) {
    Vec3 v;
    setComponent(v, axis, 1.0f);
    return v;
}

float distanceSquared(ImVec2 a, ImVec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const ImVec2 ab{b.x - a.x, b.y - a.y};
    const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
    float t =
        lengthSquared > 0.0f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lengthSquared : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return std::sqrt(distanceSquared(p, {a.x + ab.x * t, a.y + ab.y * t}));
}

bool insideConvex(ImVec2 p, const std::array<ImVec2, 4>& quad) {
    bool anyPositive = false;
    bool anyNegative = false;
    for (std::size_t i = 0; i < quad.size(); ++i) {
        const ImVec2 a = quad[i];
        const ImVec2 b = quad[(i + 1) % quad.size()];
        const float side = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        anyPositive |= side > 0.0f;
        anyNegative |= side < 0.0f;
    }
    return !(anyPositive && anyNegative);
}

std::array<Vec3, 8> boxCorners(const Aabb& box) {
    std::array<Vec3, 8> corners;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        corners[i] = {(i & 1) != 0 ? box.max.x : box.min.x, (i & 2) != 0 ? box.max.y : box.min.y,
                      (i & 4) != 0 ? box.max.z : box.min.z};
    }
    return corners;
}

// The two axes spanning the plane square or ring around an axis, in right-handed order.
void otherAxes(std::size_t axis, std::size_t& u, std::size_t& v) {
    u = (axis + 1) % 3;
    v = (axis + 2) % 3;
}

Vec3 ringPoint(Vec3 centre, std::size_t axis, float radius, int segment) {
    std::size_t u = 0;
    std::size_t v = 0;
    otherAxes(axis, u, v);
    const float angle =
        2.0f * kPi * static_cast<float>(segment) / static_cast<float>(kRingSegments);
    return centre + axisVector(u) * (radius * std::cos(angle)) +
           axisVector(v) * (radius * std::sin(angle));
}

std::array<Vec3, 4> planeSquare(Vec3 centre, std::size_t axis, float size) {
    std::size_t u = 0;
    std::size_t v = 0;
    otherAxes(axis, u, v);
    const Vec3 a = axisVector(u) * size;
    const Vec3 b = axisVector(v) * size;
    return {centre + a * kPlaneHandleNear + b * kPlaneHandleNear,
            centre + a * kPlaneHandleFar + b * kPlaneHandleNear,
            centre + a * kPlaneHandleFar + b * kPlaneHandleFar,
            centre + a * kPlaneHandleNear + b * kPlaneHandleFar};
}

Vec3 sideHandlePoint(const Aabb& extent, std::size_t axis, bool maxSide) {
    Vec3 point = (extent.min + extent.max) * 0.5f;
    setComponent(point, axis, component(maxSide ? extent.max : extent.min, axis));
    return point;
}

struct Clip {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
};

// World to screen for the 3D view.
class Projector {
public:
    Projector(const Camera& camera, const ViewRect& rect)
        : m_viewProjection(makeViewProjection(camera, rect.aspect())), m_rect(rect) {}

    Clip clip(Vec3 p) const {
        // Rows 0, 1 and 3 of the column-major matrix; depth isn't needed.
        const Mat4& m = m_viewProjection;
        return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
                m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
                m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15]};
    }

    ImVec2 toScreen(const Clip& clip) const {
        return {m_rect.x + (clip.x / clip.w * 0.5f + 0.5f) * m_rect.width,
                m_rect.y + (clip.y / clip.w * 0.5f + 0.5f) * m_rect.height};
    }

    // Nullopt for points behind the camera.
    std::optional<ImVec2> screen(Vec3 p) const {
        const Clip c = clip(p);
        if (c.w < kCameraNearPlane) {
            return std::nullopt;
        }
        return toScreen(c);
    }

    const ViewRect& rect() const { return m_rect; }

private:
    Mat4 m_viewProjection;
    ViewRect m_rect;
};

// Draws world-space lines over the view, clipped against the near plane.
class LinePainter {
public:
    LinePainter(const Camera& camera, const ViewRect& rect)
        : m_projector(camera, rect), m_drawList(ImGui::GetBackgroundDrawList()) {
        m_drawList->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height});
    }
    ~LinePainter() { m_drawList->PopClipRect(); }
    LinePainter(const LinePainter&) = delete;
    LinePainter& operator=(const LinePainter&) = delete;

    const Projector& projector() const { return m_projector; }
    ImDrawList& drawList() const { return *m_drawList; }

    void line(Vec3 a, Vec3 b, ImU32 color, float thickness = 1.0f) const {
        Clip ca = m_projector.clip(a);
        Clip cb = m_projector.clip(b);
        if (ca.w < kCameraNearPlane && cb.w < kCameraNearPlane) {
            return;
        }
        const auto clipToNear = [](Clip& behind, const Clip& front) {
            const float t = (kCameraNearPlane - behind.w) / (front.w - behind.w);
            behind = {behind.x + (front.x - behind.x) * t, behind.y + (front.y - behind.y) * t,
                      kCameraNearPlane};
        };
        if (ca.w < kCameraNearPlane) {
            clipToNear(ca, cb);
        } else if (cb.w < kCameraNearPlane) {
            clipToNear(cb, ca);
        }
        m_drawList->AddLine(m_projector.toScreen(ca), m_projector.toScreen(cb), color, thickness);
    }

    void box(const Aabb& box, ImU32 color, float thickness = 1.0f) const {
        const std::array<Vec3, 8> c = boxCorners(box);
        // Corner i has bit 0 for x, bit 1 for y, bit 2 for z: edges join corners one bit apart.
        for (std::size_t i = 0; i < c.size(); ++i) {
            for (std::size_t bit = 1; bit < 8; bit <<= 1) {
                if ((i & bit) == 0) {
                    line(c[i], c[i | bit], color, thickness);
                }
            }
        }
    }

    void polygon(const std::vector<Vec3>& points, ImU32 color, float thickness = 1.0f) const {
        for (std::size_t i = 0; i < points.size(); ++i) {
            line(points[i], points[(i + 1) % points.size()], color, thickness);
        }
    }

    void brush(const Brush& brush, ImU32 color, float thickness = 1.0f) const {
        for (std::size_t i = 0; i < brush.faces.size(); ++i) {
            polygon(faceVertices(brush, i), color, thickness);
        }
    }

private:
    Projector m_projector;
    ImDrawList* m_drawList;
};

bool isGizmoTool(EditorTool tool) {
    return tool == EditorTool::Move || tool == EditorTool::Rotate || tool == EditorTool::Scale;
}

double screenAngle(ImVec2 centre, ImVec2 point) {
    return std::atan2(static_cast<double>(point.y - centre.y),
                      static_cast<double>(point.x - centre.x));
}

} // namespace

void LevelViewport::fly(const FlyInput& input) {
    m_camera.yawRadians =
        std::remainder(m_camera.yawRadians - input.lookX * kLookRadiansPerCount, 2.0f * kPi);
    m_camera.pitchRadians = std::clamp(m_camera.pitchRadians - input.lookY * kLookRadiansPerCount,
                                       -kMaxPitch, kMaxPitch);
    const CameraBasis basis = cameraBasis(m_camera);
    Vec3 move;
    move += basis.forward * (static_cast<float>(input.forward) - static_cast<float>(input.back));
    move += basis.right * (static_cast<float>(input.right) - static_cast<float>(input.left));
    move.z += static_cast<float>(input.up) - static_cast<float>(input.down);
    const float speed = kFlySpeed * (input.fast ? kFastFlyFactor : 1.0f);
    m_camera.position += normalize(move) * (speed * input.seconds);
}

void LevelViewport::frame(Vec3 point, float distance) {
    m_camera.position = point - cameraBasis(m_camera).forward * distance;
}

Vec3 LevelViewport::forwardOnGrid() const {
    const Vec3 forward = cameraBasis(m_camera).forward;
    if (std::fabs(forward.x) >= std::fabs(forward.y)) {
        return {forward.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f};
    }
    return {0.0f, forward.y < 0.0f ? -1.0f : 1.0f, 0.0f};
}

LevelViewport::Ray LevelViewport::mouseRay(const ViewRect& rect) const {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float x = (mouse.x - rect.x) / rect.width * 2.0f - 1.0f;
    const float y = (mouse.y - rect.y) / rect.height * 2.0f - 1.0f;
    return {m_camera.position, cameraRayDirection(m_camera, rect.aspect(), x, y)};
}

std::optional<LevelViewport::Pick> LevelViewport::pick(const EditorDocument& document,
                                                       const EditorSettings& settings,
                                                       const Ray& ray) const {
    std::optional<Pick> best;
    const Level& level = document.level();
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        const auto hit = rayCastBrush(level.brushes[i], ray.origin, ray.direction);
        if (hit && (!best || hit->distance < best->distance)) {
            best = Pick{LevelItem::brush(i), hit->distance, hit->face};
        }
    }
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        const std::optional<Aabb> marker =
            entityMarker(level.entities[i], settings.playerHalfExtents);
        if (!marker) {
            continue;
        }
        const auto distance = rayCastBox(*marker, ray.origin, ray.direction);
        // Entities win ties with the brush they stand in, so spawns inside walls stay clickable.
        if (distance && (!best || *distance <= best->distance)) {
            best = Pick{LevelItem::entity(i), *distance, 0};
        }
    }
    return best;
}

std::optional<Vec3> LevelViewport::surfacePoint(const EditorDocument& document,
                                                const Ray& ray) const {
    std::optional<float> nearest;
    for (const Brush& brush : document.level().brushes) {
        const auto hit = rayCastBrush(brush, ray.origin, ray.direction);
        if (hit && (!nearest || hit->distance < *nearest)) {
            nearest = hit->distance;
        }
    }
    if (!nearest) {
        nearest = rayCastPlane({{0.0f, 0.0f, 1.0f}, 0.0f}, ray.origin, ray.direction);
    }
    if (!nearest) {
        return std::nullopt;
    }
    return ray.origin + ray.direction * *nearest;
}

Vec3 LevelViewport::placementPoint(const EditorDocument& document,
                                   const EditorSettings& settings) const {
    const Ray ray{m_camera.position, cameraBasis(m_camera).forward};
    const std::optional<Vec3> surface = surfacePoint(document, ray);
    const Vec3 point = surface && length(*surface - ray.origin) < 50.0f
                           ? *surface
                           : ray.origin + ray.direction * kPlacementDistance;
    return snapToGrid(point, settings.grid());
}

std::optional<LevelViewport::GizmoFrame>
LevelViewport::gizmoFrame(const EditorDocument& document) const {
    if (!isGizmoTool(m_tool)) {
        return std::nullopt;
    }
    const std::optional<Aabb> extent = selectionExtent(document);
    if (!extent) {
        return std::nullopt;
    }
    GizmoFrame frame;
    frame.extent = *extent;
    frame.centre = (extent->min + extent->max) * 0.5f;
    const float depth = dot(frame.centre - m_camera.position, cameraBasis(m_camera).forward);
    frame.size = std::max(depth, 1.0f) * kGizmoScale;
    return frame;
}

std::optional<LevelViewport::GizmoHandle> LevelViewport::pickGizmo(const EditorDocument& document,
                                                                   const ViewRect& rect) const {
    const std::optional<GizmoFrame> frame = gizmoFrame(document);
    if (!frame) {
        return std::nullopt;
    }
    const Projector projector(m_camera, rect);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float reach = ImGui::GetFontSize() * 0.6f;
    std::optional<GizmoHandle> best;
    float bestDistance = reach;
    const auto consider = [&](GizmoHandle handle, float distance) {
        if (distance <= bestDistance) {
            best = handle;
            bestDistance = distance;
        }
    };

    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (m_tool == EditorTool::Move) {
            const auto from = projector.screen(frame->centre);
            const auto to = projector.screen(frame->centre + axisVector(axis) * frame->size);
            if (from && to) {
                consider({GizmoHandle::Kind::Arrow, axis}, distanceToSegment(mouse, *from, *to));
            }
            std::array<ImVec2, 4> quad;
            bool visible = true;
            const std::array<Vec3, 4> square = planeSquare(frame->centre, axis, frame->size);
            for (std::size_t i = 0; i < quad.size(); ++i) {
                const auto corner = projector.screen(square[i]);
                visible &= corner.has_value();
                quad[i] = corner.value_or(ImVec2{});
            }
            if (visible && insideConvex(mouse, quad)) {
                consider({GizmoHandle::Kind::Plane, axis}, 0.0f);
            }
        } else if (m_tool == EditorTool::Rotate) {
            std::optional<ImVec2> previous =
                projector.screen(ringPoint(frame->centre, axis, frame->size, 0));
            for (int segment = 1; segment <= kRingSegments; ++segment) {
                const auto point =
                    projector.screen(ringPoint(frame->centre, axis, frame->size, segment));
                if (previous && point) {
                    consider({GizmoHandle::Kind::Ring, axis},
                             distanceToSegment(mouse, *previous, *point));
                }
                previous = point;
            }
        } else if (m_tool == EditorTool::Scale &&
                   component(frame->extent.max - frame->extent.min, axis) > 0.0f) {
            for (const bool maxSide : {false, true}) {
                const auto point = projector.screen(sideHandlePoint(frame->extent, axis, maxSide));
                if (point) {
                    consider({GizmoHandle::Kind::Side, axis, maxSide},
                             std::sqrt(distanceSquared(mouse, *point)));
                }
            }
        }
    }
    return best;
}

void LevelViewport::update(EditorDocument& document, const EditorSettings& settings,
                           const ViewRect& rect, bool mouseOverView) {
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    const Ray ray = mouseRay(rect);
    if (m_drag.kind == DragKind::ShapeHeight) {
        // The height follows the mouse up the vertical through the rectangle's far corner.
        const float grid = settings.grid();
        if (const auto s =
                closestAlongAxis(m_drag.boxEnd, {0.0f, 0.0f, 1.0f}, ray.origin, ray.direction)) {
            if (!m_drag.heightStart) {
                m_drag.heightStart = *s;
            }
            m_drag.height =
                std::max(grid, snapToGrid(m_shapeHeight + *s - *m_drag.heightStart, grid));
        }
        const bool enter =
            !ImGui::GetIO().WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                              ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        if ((mouseOverView && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || enter) {
            placeShape(document, settings);
        }
        return;
    }
    if (isDragging()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            continueDrag(document, settings, ray);
        } else {
            endDrag(document, settings, rect);
        }
        return;
    }

    adjustPlacedShape(document, settings);
    // Corners that are no longer corners of a selected brush (after an undo, say) drop out.
    std::erase_if(m_corners, [&](Vec3 corner) {
        return std::none_of(
            document.selection().begin(), document.selection().end(), [&](const LevelItem& item) {
                if (item.kind != LevelItem::Kind::Brush) {
                    return false;
                }
                const auto& vertices = document.level().brushes[item.index].vertices;
                return std::find(vertices.begin(), vertices.end(), corner) != vertices.end();
            });
    });
    if (m_tool == EditorTool::Clip) {
        clipKeys(document);
    }
    m_hoverHandle = mouseOverView ? pickGizmo(document, rect) : std::nullopt;
    m_hoverVertex = mouseOverView ? pickVertexHandle(document, rect) : std::nullopt;
    m_hover = mouseOverView && !m_hoverHandle && !m_hoverVertex ? pick(document, settings, ray)
                                                                : std::nullopt;
    if (mouseOverView && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        beginDrag(document, settings, rect, ray);
    }
    if (mouseOverView && ImGui::GetIO().MouseWheel != 0.0f) {
        m_camera.position += cameraBasis(m_camera).forward * ImGui::GetIO().MouseWheel;
    }
}

void LevelViewport::beginDrag(EditorDocument& document, const EditorSettings& settings,
                              const ViewRect& rect, const Ray& ray) {
    const ImGuiIO& io = ImGui::GetIO();
    if (m_hoverHandle) {
        beginGizmoDrag(document, *m_hoverHandle, rect, ray);
        return;
    }
    if (m_hoverVertex) {
        beginCornerDrag(document, *m_hoverVertex);
        return;
    }
    if (m_tool == EditorTool::Clip) {
        // A fourth point starts a new cut.
        if (const std::optional<Vec3> point = surfacePoint(document, ray)) {
            if (m_clipPoints.size() == 3) {
                m_clipPoints.clear();
            }
            m_clipPoints.push_back(snapToGrid(*point, settings.grid()));
        }
        return;
    }
    if (m_tool == EditorTool::Draw) {
        const std::optional<Vec3> point = surfacePoint(document, ray);
        if (point) {
            m_drag = {};
            m_drag.kind = DragKind::ShapeBase;
            m_drag.boxStart = snapToGrid(*point, settings.grid());
            m_drag.boxEnd = m_drag.boxStart;
            m_drag.height = m_shapeHeight;
            m_drag.forward = forwardOnGrid();
        }
        return;
    }

    m_drag = {};
    // Dragging a selected item moves the selection. Anywhere else, including unselected items
    // (in a level nearly every point is on some brush, the floor at least), a drag draws a
    // selection box and a click selects what is under the mouse. The Vertex tool moves corners
    // only by their handles; its selection box picks corners.
    const bool vertexTool = m_tool == EditorTool::Vertex;
    if (m_hover && io.KeyCtrl && io.KeyShift && !vertexTool &&
        m_hover->target.kind == LevelItem::Kind::Brush &&
        document.singleSelected() == m_hover->target) {
        m_drag.kind = DragKind::Extrude;
        m_drag.target = m_hover->target;
        m_drag.grabPoint = ray.origin + ray.direction * m_hover->distance;
        m_drag.brushBefore = document.level().brushes[m_hover->target.index];
        m_drag.face = m_hover->face;
        return;
    }
    if (m_hover && !io.KeyCtrl && !vertexTool && document.isSelected(m_hover->target)) {
        const Pick hit = *m_hover;
        m_drag.target = hit.target;
        m_drag.grabPoint = ray.origin + ray.direction * hit.distance;
        if (io.KeyShift && hit.target.kind == LevelItem::Kind::Brush &&
            document.singleSelected() == hit.target) {
            m_drag.kind = DragKind::Face;
            m_drag.brushBefore = document.level().brushes[hit.target.index];
            m_drag.face = hit.face;
            m_drag.movedFace = hit.face;
            return;
        }
        m_drag.kind = io.KeyAlt ? DragKind::MoveVertical : DragKind::Move;
        m_drag.selection.emplace(document);
        return;
    }
    m_drag.kind = DragKind::Marquee;
    m_drag.screenStartX = io.MousePos.x;
    m_drag.screenStartY = io.MousePos.y;
    m_drag.additive = io.KeyCtrl;
    m_drag.selectCorners =
        vertexTool &&
        std::any_of(document.selection().begin(), document.selection().end(),
                    [](const LevelItem& item) { return item.kind == LevelItem::Kind::Brush; });
    if (m_hover) {
        m_drag.clicked = m_hover->target;
    }
}

void LevelViewport::beginCornerDrag(EditorDocument& document, const VertexHandle& handle) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool allSelected = std::all_of(handle.corners.begin(), handle.corners.end(),
                                         [&](Vec3 corner) { return isCornerSelected(corner); });
    if (io.KeyCtrl) {
        if (allSelected) {
            std::erase_if(m_corners, [&](Vec3 corner) {
                return std::find(handle.corners.begin(), handle.corners.end(), corner) !=
                       handle.corners.end();
            });
        } else {
            for (const Vec3& corner : handle.corners) {
                if (!isCornerSelected(corner)) {
                    m_corners.push_back(corner);
                }
            }
        }
        return;
    }
    if (!allSelected) {
        m_corners = handle.corners;
    }
    m_drag = {};
    m_drag.kind = DragKind::Corners;
    m_drag.grabPoint = handle.position;
    m_drag.corners = m_corners;
    m_drag.cornerOnGrid = handle.kind == VertexHandle::Kind::Corner;
    m_drag.vertical = io.KeyAlt;
    m_drag.selection.emplace(document);
}

void LevelViewport::continueCornerDrag(EditorDocument& document, const EditorSettings& settings,
                                       const Ray& ray) {
    const float grid = settings.grid();
    const Vec3 grab = m_drag.grabPoint;
    // A grabbed corner lands on the grid; the middle of an edge or face moves in grid steps.
    const auto snapped = [&](float start, float moved) {
        return m_drag.cornerOnGrid ? snapToGrid(start + moved, grid) - start
                                   : snapToGrid(moved, grid);
    };
    Vec3 offset;
    if (m_drag.vertical) {
        const auto s = closestAlongAxis(grab, {0.0f, 0.0f, 1.0f}, ray.origin, ray.direction);
        if (!s) {
            return;
        }
        offset.z = snapped(grab.z, *s);
    } else {
        const auto t = rayCastPlane({{0.0f, 0.0f, 1.0f}, grab.z}, ray.origin, ray.direction);
        if (!t) {
            return;
        }
        const Vec3 moved = ray.origin + ray.direction * *t - grab;
        offset = {snapped(grab.x, moved.x), snapped(grab.y, moved.y), 0.0f};
    }
    // A move the brushes refuse leaves them as they were at the last one they took.
    if (!(offset == m_drag.cornerOffset) &&
        m_drag.selection->previewCorners(document, m_drag.corners, offset)) {
        m_drag.cornerOffset = offset;
    }
}

bool LevelViewport::clearToolState() {
    if (!m_clipPoints.empty()) {
        m_clipPoints.clear();
        return true;
    }
    if (!m_corners.empty()) {
        m_corners.clear();
        return true;
    }
    return false;
}

std::optional<DPlane> LevelViewport::clipPlane() const {
    if (m_clipPoints.size() < 2) {
        return std::nullopt;
    }
    const DVec3 a = toDouble(m_clipPoints[0]);
    const DVec3 b = toDouble(m_clipPoints[1]);
    DVec3 c;
    if (m_clipPoints.size() == 3) {
        c = toDouble(m_clipPoints[2]);
    } else {
        // Two points cut straight down through their line; a vertical line, facing the camera.
        const bool vertical = a.x == b.x && a.y == b.y;
        c = a + (vertical ? toDouble(cameraBasis(m_camera).right) : DVec3{0.0, 0.0, 1.0});
    }
    const DPlane plane = planeThroughPoints(a, b, c);
    if (!(length(plane.normal) > 0.0)) {
        return std::nullopt;
    }
    return plane;
}

void LevelViewport::clipKeys(EditorDocument& document) {
    if (ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        m_clipKeep = m_clipKeep == ClipKeep::Back
                         ? ClipKeep::Front
                         : (m_clipKeep == ClipKeep::Front ? ClipKeep::Both : ClipKeep::Back);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !m_clipPoints.empty()) {
        m_clipPoints.pop_back();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        const std::optional<DPlane> plane = clipPlane();
        if (plane && clipSelection(document, *plane, m_clipKeep)) {
            m_clipPoints.clear();
        }
    }
}

bool LevelViewport::isCornerSelected(Vec3 corner) const {
    return std::find(m_corners.begin(), m_corners.end(), corner) != m_corners.end();
}

std::vector<LevelViewport::VertexHandle>
LevelViewport::vertexHandles(const EditorDocument& document) const {
    std::vector<VertexHandle> handles;
    if (m_tool != EditorTool::Vertex) {
        return handles;
    }
    // Brushes that touch share handles, so moving one keeps them sealed.
    std::map<std::tuple<VertexHandle::Kind, float, float, float>, std::size_t> seen;
    const auto add = [&](VertexHandle::Kind kind, Vec3 position, std::vector<Vec3> corners) {
        if (seen.emplace(std::tuple{kind, position.x, position.y, position.z}, handles.size())
                .second) {
            handles.push_back({kind, position, std::move(corners)});
        }
    };
    for (const LevelItem& item : document.selection()) {
        if (item.kind != LevelItem::Kind::Brush) {
            continue;
        }
        const Brush& brush = document.level().brushes[item.index];
        for (const Vec3& corner : brush.vertices) {
            add(VertexHandle::Kind::Corner, corner, {corner});
        }
        for (const auto& [a, b] : brushEdges(brush)) {
            const Vec3 from = brush.vertices[a];
            const Vec3 to = brush.vertices[b];
            add(VertexHandle::Kind::Edge, (from + to) * 0.5f, {from, to});
        }
        for (std::size_t face = 0; face < brush.faces.size(); ++face) {
            add(VertexHandle::Kind::Face, faceCentre(brush, face), faceVertices(brush, face));
        }
    }
    return handles;
}

std::optional<LevelViewport::VertexHandle>
LevelViewport::pickVertexHandle(const EditorDocument& document, const ViewRect& rect) const {
    const Projector projector(m_camera, rect);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float reach = ImGui::GetFontSize() * 0.5f;
    // Corners win over edge middles, and edge middles over face middles, wherever they overlap.
    std::optional<VertexHandle> best;
    float bestDistance = 0.0f;
    for (VertexHandle& handle : vertexHandles(document)) {
        const auto point = projector.screen(handle.position);
        if (!point) {
            continue;
        }
        const float distance = std::sqrt(distanceSquared(mouse, *point));
        if (distance > reach) {
            continue;
        }
        if (!best || handle.kind < best->kind ||
            (handle.kind == best->kind && distance < bestDistance)) {
            best = std::move(handle);
            bestDistance = distance;
        }
    }
    return best;
}

void LevelViewport::beginGizmoDrag(EditorDocument& document, const GizmoHandle& handle,
                                   const ViewRect& rect, const Ray& ray) {
    const std::optional<GizmoFrame> frame = gizmoFrame(document);
    if (!frame) {
        return;
    }
    Drag drag;
    drag.kind = DragKind::Gizmo;
    drag.handle = handle;
    drag.extent = frame->extent;
    drag.gizmoCentre = frame->centre;
    const Vec3 axis = axisVector(handle.axis);
    switch (handle.kind) {
    case GizmoHandle::Kind::Arrow: {
        const auto s = closestAlongAxis(frame->centre, axis, ray.origin, ray.direction);
        if (!s) {
            return;
        }
        drag.axisStart = *s;
        break;
    }
    case GizmoHandle::Kind::Plane: {
        const auto t = rayCastPlane({axis, dot(axis, frame->centre)}, ray.origin, ray.direction);
        if (!t) {
            return;
        }
        drag.grabPoint = ray.origin + ray.direction * *t;
        break;
    }
    case GizmoHandle::Kind::Ring: {
        const auto centre = Projector(m_camera, rect).screen(frame->centre);
        if (!centre) {
            return;
        }
        drag.screenStartX = centre->x;
        drag.screenStartY = centre->y;
        drag.lastScreenAngle = screenAngle(*centre, ImGui::GetIO().MousePos);
        break;
    }
    case GizmoHandle::Kind::Side: {
        drag.grabPoint = sideHandlePoint(frame->extent, handle.axis, handle.maxSide);
        const auto s = closestAlongAxis(drag.grabPoint, axis, ray.origin, ray.direction);
        if (!s) {
            return;
        }
        drag.axisStart = *s;
        break;
    }
    }
    drag.selection.emplace(document);
    m_drag = std::move(drag);
}

void LevelViewport::continueDrag(EditorDocument& document, const EditorSettings& settings,
                                 const Ray& ray) {
    const float grid = settings.grid();
    switch (m_drag.kind) {
    case DragKind::ShapeBase: {
        const Plane base{{0.0f, 0.0f, 1.0f}, m_drag.boxStart.z};
        if (const auto t = rayCastPlane(base, ray.origin, ray.direction)) {
            const Vec3 point = snapToGrid(ray.origin + ray.direction * *t, grid);
            m_drag.boxEnd = {point.x, point.y, m_drag.boxStart.z};
        }
        return;
    }
    case DragKind::Move: {
        const Plane level{{0.0f, 0.0f, 1.0f}, m_drag.grabPoint.z};
        if (const auto t = rayCastPlane(level, ray.origin, ray.direction)) {
            Vec3 offset = snapToGrid(ray.origin + ray.direction * *t - m_drag.grabPoint, grid);
            offset.z = 0.0f;
            m_drag.selection->preview(document, AffineTransform::moveBy(offset));
        }
        return;
    }
    case DragKind::MoveVertical: {
        if (const auto s =
                closestAlongAxis(m_drag.grabPoint, {0.0f, 0.0f, 1.0f}, ray.origin, ray.direction)) {
            m_drag.selection->preview(document,
                                      AffineTransform::moveBy({0.0f, 0.0f, snapToGrid(*s, grid)}));
        }
        return;
    }
    case DragKind::Face: {
        const Plane before = facePlane(m_drag.brushBefore, m_drag.face);
        const auto s = closestAlongAxis(m_drag.grabPoint, before.normal, ray.origin, ray.direction);
        if (!s) {
            return;
        }
        // An axis-aligned face lands on the grid; a sloped one moves in grid steps.
        const float distance = isAxial(before.normal) ? snapToGrid(before.distance + *s, grid)
                                                      : before.distance + snapToGrid(*s, grid);
        if (const auto resized =
                withFacePushed(m_drag.brushBefore, m_drag.face, distance, &m_drag.movedFace)) {
            document.previewBrush(m_drag.target.index, *resized);
        }
        return;
    }
    case DragKind::Corners:
        continueCornerDrag(document, settings, ray);
        return;
    case DragKind::Extrude: {
        const Plane face = facePlane(m_drag.brushBefore, m_drag.face);
        if (const auto s =
                closestAlongAxis(m_drag.grabPoint, face.normal, ray.origin, ray.direction)) {
            m_drag.extrudeDistance = std::max(0.0f, snapToGrid(*s, grid));
        }
        return;
    }
    case DragKind::Gizmo:
        continueGizmoDrag(document, settings, ray);
        return;
    case DragKind::Marquee:
    case DragKind::ShapeHeight:
    case DragKind::None:
        return;
    }
}

void LevelViewport::continueGizmoDrag(EditorDocument& document, const EditorSettings& settings,
                                      const Ray& ray) {
    const float grid = settings.grid();
    const std::size_t axisIndex = m_drag.handle.axis;
    const Vec3 axis = axisVector(axisIndex);
    switch (m_drag.handle.kind) {
    case GizmoHandle::Kind::Arrow: {
        if (const auto s = closestAlongAxis(m_drag.gizmoCentre, axis, ray.origin, ray.direction)) {
            m_drag.selection->preview(
                document, AffineTransform::moveBy(axis * snapToGrid(*s - m_drag.axisStart, grid)));
        }
        return;
    }
    case GizmoHandle::Kind::Plane: {
        const Plane plane{axis, dot(axis, m_drag.gizmoCentre)};
        if (const auto t = rayCastPlane(plane, ray.origin, ray.direction)) {
            Vec3 offset = snapToGrid(ray.origin + ray.direction * *t - m_drag.grabPoint, grid);
            setComponent(offset, axisIndex, 0.0f);
            m_drag.selection->preview(document, AffineTransform::moveBy(offset));
        }
        return;
    }
    case GizmoHandle::Kind::Ring: {
        const double current =
            screenAngle({m_drag.screenStartX, m_drag.screenStartY}, ImGui::GetIO().MousePos);
        const double delta =
            std::remainder(current - m_drag.lastScreenAngle, 2.0 * std::numbers::pi);
        m_drag.lastScreenAngle = current;
        // Screen Y points down, so a turn that looks anticlockwise from the axis's tip lowers the
        // screen angle when the axis points at the camera, and raises it when it points away.
        const bool facingCamera = dot(axis, cameraBasis(m_camera).forward) < 0.0f;
        m_drag.angle += (facingCamera ? -delta : delta) * 180.0 / std::numbers::pi;
        const double degrees = snapAngle(m_drag.angle, settings.rotationStep());
        m_drag.selection->preview(
            document, AffineTransform::rotation(axisIndex, degrees, m_drag.gizmoCentre));
        return;
    }
    case GizmoHandle::Kind::Side: {
        const auto s = closestAlongAxis(m_drag.grabPoint, axis, ray.origin, ray.direction);
        if (!s) {
            return;
        }
        const Aabb& from = m_drag.extent;
        Aabb to = from;
        const float side = component(m_drag.handle.maxSide ? from.max : from.min, axisIndex);
        float moved = snapToGrid(side + *s - m_drag.axisStart, grid);
        // Never thinner than one grid step, and never turned inside out.
        if (m_drag.handle.maxSide) {
            moved = std::max(moved, component(from.min, axisIndex) + grid);
            setComponent(to.max, axisIndex, moved);
        } else {
            moved = std::min(moved, component(from.max, axisIndex) - grid);
            setComponent(to.min, axisIndex, moved);
        }
        m_drag.selection->preview(document, AffineTransform::boxToBox(from, to));
        return;
    }
    }
}

void LevelViewport::endDrag(EditorDocument& document, const EditorSettings& settings,
                            const ViewRect& rect) {
    Drag drag = std::move(m_drag);
    m_drag = {};
    switch (drag.kind) {
    case DragKind::ShapeBase: {
        const Vec3 size = drag.boxEnd - drag.boxStart;
        if (std::fabs(size.x) < settings.grid() || std::fabs(size.y) < settings.grid()) {
            return; // a click, not a drag
        }
        m_drag = std::move(drag);
        m_drag.kind = DragKind::ShapeHeight;
        return;
    }
    case DragKind::ShapeHeight:
        return;
    case DragKind::Move:
    case DragKind::MoveVertical:
        // A click on one item of several, without moving, selects just that one.
        if (!drag.selection->commit(document, "Move " + describeSelection(document))) {
            document.select(drag.target);
        }
        return;
    case DragKind::Gizmo: {
        const char* verb = "Move";
        if (drag.handle.kind == GizmoHandle::Kind::Ring) {
            verb = "Rotate";
        } else if (drag.handle.kind == GizmoHandle::Kind::Side) {
            verb = "Scale";
        }
        drag.selection->commit(document, std::format("{} {}", verb, describeSelection(document)));
        return;
    }
    case DragKind::Face: {
        const Brush& after = document.level().brushes[drag.target.index];
        if (!(after == drag.brushBefore)) {
            document.execute(
                makeSetBrush("Resize brush", drag.target.index, drag.brushBefore, after));
        }
        return;
    }
    case DragKind::Extrude:
        if (drag.extrudeDistance > 0.0f) {
            if (auto block = extrudeFace(drag.brushBefore, drag.face, drag.extrudeDistance)) {
                addBrushes(document, {std::move(*block)}, "Extrude face");
            }
        }
        return;
    case DragKind::Corners:
        if (drag.selection->commit(document,
                                   drag.corners.size() == 1 ? "Move corner" : "Move corners")) {
            m_corners = drag.corners;
            for (Vec3& corner : m_corners) {
                corner += drag.cornerOffset;
            }
        }
        return;
    case DragKind::Marquee:
        finishMarquee(drag, document, settings, rect);
        return;
    case DragKind::None:
        return;
    }
}

Aabb LevelViewport::shapeBox() const {
    Vec3 min = ct::min(m_drag.boxStart, m_drag.boxEnd);
    Vec3 max = ct::max(m_drag.boxStart, m_drag.boxEnd);
    max.z = min.z + m_drag.height;
    return {min, max};
}

ShapeSettings LevelViewport::shapeSettings(const EditorSettings& settings, Vec3 awayForward) const {
    ShapeSettings result = settings.shapeSettings;
    switch (settings.facing) {
    case ShapeFacing::AwayFromView:
        result.forward = awayForward;
        break;
    case ShapeFacing::PlusX:
        result.forward = {1.0f, 0.0f, 0.0f};
        break;
    case ShapeFacing::PlusY:
        result.forward = {0.0f, 1.0f, 0.0f};
        break;
    case ShapeFacing::MinusX:
        result.forward = {-1.0f, 0.0f, 0.0f};
        break;
    case ShapeFacing::MinusY:
        result.forward = {0.0f, -1.0f, 0.0f};
        break;
    }
    return clampedSettings(settings.shape, result);
}

void LevelViewport::placeShape(EditorDocument& document, const EditorSettings& settings) {
    const Aabb box = shapeBox();
    const ShapeSettings wanted = shapeSettings(settings, m_drag.forward);
    std::vector<Brush> brushes = makeShape(settings.shape, box, wanted);
    if (brushes.empty()) {
        return; // shown in red; the height or settings must change first
    }
    const std::string name = std::format("Create {}", shapeName(settings.shape));
    if (!addBrushes(document, std::move(brushes), name)) {
        return;
    }
    m_shapeHeight = m_drag.height;
    m_placedShape = PlacedShape{box, settings.shape, wanted, document.history().stateId()};
    m_drag = {};
}

void LevelViewport::adjustPlacedShape(EditorDocument& document, const EditorSettings& settings) {
    if (!m_placedShape) {
        return;
    }
    // Anything edited since, including an undo, ends the chance to adjust it.
    if (document.history().stateId() != m_placedShape->stateId) {
        m_placedShape.reset();
        return;
    }
    if (settings.shape != m_placedShape->kind) {
        return;
    }
    const ShapeSettings wanted = shapeSettings(settings, m_placedShape->settings.forward);
    if (wanted == m_placedShape->settings) {
        return;
    }
    std::vector<Brush> brushes = makeShape(m_placedShape->kind, m_placedShape->box, wanted);
    if (brushes.empty()) {
        return;
    }
    // Replace it as if it had been made this way: one command in the history either way.
    document.undo();
    addBrushes(document, std::move(brushes),
               std::format("Create {}", shapeName(m_placedShape->kind)));
    m_placedShape->settings = wanted;
    m_placedShape->stateId = document.history().stateId();
}

void LevelViewport::finishMarquee(const Drag& drag, EditorDocument& document,
                                  const EditorSettings& settings, const ViewRect& rect) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 from{std::min(drag.screenStartX, mouse.x), std::min(drag.screenStartY, mouse.y)};
    const ImVec2 to{std::max(drag.screenStartX, mouse.x), std::max(drag.screenStartY, mouse.y)};
    if (to.x - from.x < kMarqueeMinPixels && to.y - from.y < kMarqueeMinPixels) {
        // A click: select what was under the mouse, or add it to or take it from the selection
        // with Ctrl. A click on nothing clears the selection.
        if (drag.clicked) {
            if (drag.additive) {
                document.toggleSelected(*drag.clicked);
            } else {
                document.select(*drag.clicked);
            }
        } else if (!drag.additive) {
            document.clearSelection();
        }
        return;
    }

    const Projector projector(m_camera, rect);
    const auto inside = [&](Vec3 p) {
        const auto point = projector.screen(p);
        return point && point->x >= from.x && point->x <= to.x && point->y >= from.y &&
               point->y <= to.y;
    };
    if (drag.selectCorners) {
        std::vector<Vec3> corners = drag.additive ? m_corners : std::vector<Vec3>{};
        for (const VertexHandle& handle : vertexHandles(document)) {
            if (handle.kind == VertexHandle::Kind::Corner && inside(handle.position) &&
                std::find(corners.begin(), corners.end(), handle.position) == corners.end()) {
                corners.push_back(handle.position);
            }
        }
        m_corners = std::move(corners);
        return;
    }

    // Everything whose bounds are wholly inside the rectangle, so a drag across a room picks its
    // contents rather than the floor and walls it overlaps.
    const Level& level = document.level();
    std::vector<LevelItem> items;
    if (drag.additive) {
        items = document.selection();
    }
    const auto consider = [&](LevelItem item) {
        const std::optional<Aabb> bounds = itemBounds(level, item, settings.playerHalfExtents);
        if (!bounds) {
            return;
        }
        for (const Vec3& corner : boxCorners(*bounds)) {
            if (!inside(corner)) {
                return;
            }
        }
        items.push_back(item);
    };
    for (std::size_t i = 0; i < level.brushes.size(); ++i) {
        consider(LevelItem::brush(i));
    }
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        consider(LevelItem::entity(i));
    }
    document.setSelection(std::move(items));
}

void LevelViewport::cancelDrag(EditorDocument& document) {
    if (m_drag.selection) {
        m_drag.selection->cancel(document);
    } else if (m_drag.kind == DragKind::Face) {
        document.previewBrush(m_drag.target.index, m_drag.brushBefore);
    }
    m_drag = {};
}

void LevelViewport::drawOverlay(const EditorDocument& document, const EditorSettings& settings,
                                const ViewRect& rect) const {
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    const LinePainter painter(m_camera, rect);
    const Level& level = document.level();
    const Vec3 halfExtents = settings.playerHalfExtents;

    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        if (const auto marker = entityMarker(level.entities[i], halfExtents)) {
            const bool spawn = level.entities[i].className == kPlayerSpawnClass;
            painter.box(*marker, spawn ? kSpawnColor : kEntityColor, 1.5f);
        }
    }

    const auto outline = [&](const LevelItem& item, ImU32 color, float thickness) {
        if (!exists(level, item)) {
            return;
        }
        if (item.kind == LevelItem::Kind::Brush) {
            painter.brush(level.brushes[item.index], color, thickness);
        } else if (const auto marker = entityMarker(level.entities[item.index], halfExtents)) {
            painter.box(*marker, color, thickness);
        }
    };
    if (m_hover && !isDragging() && !document.isSelected(m_hover->target)) {
        outline(m_hover->target, kHoverColor, 1.0f);
    }
    for (const LevelItem& item : document.selection()) {
        outline(item, kSelectedColor, 2.0f);
    }

    // The face Shift-drag would move, or is moving.
    const std::optional<LevelItem> single = document.singleSelected();
    const bool showFace = m_drag.kind == DragKind::Face || m_drag.kind == DragKind::Extrude ||
                          (!isDragging() && ImGui::GetIO().KeyShift && m_hover && single &&
                           m_hover->target == *single);
    if (showFace && single && single->kind == LevelItem::Kind::Brush) {
        const Brush& brush = level.brushes[single->index];
        std::size_t face = m_hover ? m_hover->face : 0;
        if (m_drag.kind == DragKind::Face) {
            face = m_drag.movedFace;
        } else if (m_drag.kind == DragKind::Extrude) {
            face = m_drag.face;
        }
        if (face < brush.faces.size()) {
            painter.polygon(faceVertices(brush, face), kFaceColor, 3.0f);
        }
    }

    if (m_drag.kind == DragKind::Extrude && m_drag.extrudeDistance > 0.0f) {
        if (const auto block =
                extrudeFace(m_drag.brushBefore, m_drag.face, m_drag.extrudeDistance)) {
            painter.brush(*block, kSelectedColor, 2.0f);
        }
    }

    const bool drawingShape =
        m_drag.kind == DragKind::ShapeBase || m_drag.kind == DragKind::ShapeHeight;
    if (drawingShape) {
        const Aabb box = shapeBox();
        const std::vector<Brush> shape =
            makeShape(settings.shape, box, shapeSettings(settings, m_drag.forward));
        for (const Brush& brush : shape) {
            painter.brush(brush, kSelectedColor, 2.0f);
        }
        if (shape.empty()) {
            painter.box(box, kInvalidColor, 2.0f);
        }
    }

    ImDrawList& drawList = painter.drawList();
    if (drawingShape) {
        // The size next to the mouse, and what to do next.
        const Aabb box = shapeBox();
        const Vec3 size = box.max - box.min;
        const std::string label = std::format("{:g} x {:g} x {:g} m\n{}", size.x, size.y, size.z,
                                              m_drag.kind == DragKind::ShapeBase
                                                  ? "release, then move up for the height"
                                                  : "click or Enter to place, Esc to cancel");
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        drawList.AddText({mouse.x + ImGui::GetFontSize(), mouse.y + ImGui::GetFontSize()},
                         kSelectedColor, label.c_str());
    }
    if (m_drag.kind == DragKind::Marquee) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 from{std::min(m_drag.screenStartX, mouse.x),
                          std::min(m_drag.screenStartY, mouse.y)};
        const ImVec2 to{std::max(m_drag.screenStartX, mouse.x),
                        std::max(m_drag.screenStartY, mouse.y)};
        drawList.AddRectFilled(from, to, kMarqueeFill);
        drawList.AddRect(from, to, kMarqueeColor);
    }

    if (m_tool == EditorTool::Clip) {
        // Each selected brush the plane crosses: the part kept, and the part cut away in red.
        if (const std::optional<DPlane> plane = clipPlane()) {
            for (const LevelItem& item : document.selection()) {
                if (item.kind != LevelItem::Kind::Brush) {
                    continue;
                }
                const auto split = splitBrush(level.brushes[item.index], *plane);
                if (!split || !split->back || !split->front) {
                    continue;
                }
                const bool keepBack = m_clipKeep != ClipKeep::Front;
                const bool keepFront = m_clipKeep != ClipKeep::Back;
                painter.brush(*split->back, keepBack ? kSelectedColor : kInvalidColor,
                              keepBack ? 2.5f : 1.5f);
                painter.brush(*split->front, keepFront ? kSelectedColor : kInvalidColor,
                              keepFront ? 2.5f : 1.5f);
            }
        }
        for (std::size_t i = 0; i < m_clipPoints.size(); ++i) {
            if (i > 0) {
                painter.line(m_clipPoints[i - 1], m_clipPoints[i], kFaceColor, 2.0f);
            }
            if (const auto point = painter.projector().screen(m_clipPoints[i])) {
                drawList.AddCircleFilled(*point, ImGui::GetFontSize() * 0.3f, kFaceColor);
            }
        }
        static constexpr const char* kKeepNames[]{"back", "front", "both"};
        const std::string label =
            m_clipPoints.size() < 2
                ? std::string("click 2 or 3 points on surfaces")
                : std::format("keep {} (Tab)\nEnter to cut, Backspace to undo a point",
                              kKeepNames[static_cast<int>(m_clipKeep)]);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        drawList.AddText({mouse.x + ImGui::GetFontSize(), mouse.y + ImGui::GetFontSize()},
                         kFaceColor, label.c_str());
    }

    if (m_tool == EditorTool::Vertex) {
        const bool movingCorners = m_drag.kind == DragKind::Corners;
        // While corners move, the selection still holds where they started.
        const auto selected = [&](Vec3 corner) {
            return isCornerSelected(movingCorners ? corner - m_drag.cornerOffset : corner);
        };
        const float half = ImGui::GetFontSize() * 0.22f;
        for (const VertexHandle& handle : vertexHandles(document)) {
            const auto point = painter.projector().screen(handle.position);
            if (!point) {
                continue;
            }
            const bool hovered = m_hoverVertex && m_hoverVertex->kind == handle.kind &&
                                 m_hoverVertex->position == handle.position;
            ImU32 color = kHandleColor;
            if (std::all_of(handle.corners.begin(), handle.corners.end(), selected)) {
                color = kSelectedColor;
            } else if (hovered) {
                color = kHandleHoverColor;
            }
            const ImVec2 low{point->x - half, point->y - half};
            const ImVec2 high{point->x + half, point->y + half};
            switch (handle.kind) {
            case VertexHandle::Kind::Corner:
                drawList.AddRectFilled(low, high, color);
                break;
            case VertexHandle::Kind::Edge:
                drawList.AddCircleFilled(*point, half * 0.8f, color);
                break;
            case VertexHandle::Kind::Face:
                drawList.AddRect(low, high, color, 0.0f, 0, 1.5f);
                break;
            }
        }
        if (movingCorners) {
            const Vec3 offset = m_drag.cornerOffset;
            const std::string label =
                std::format("{:+g} {:+g} {:+g} m", offset.x, offset.y, offset.z);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            drawList.AddText({mouse.x + ImGui::GetFontSize(), mouse.y + ImGui::GetFontSize()},
                             kSelectedColor, label.c_str());
        }
    }

    const std::optional<GizmoFrame> frame = gizmoFrame(document);
    if (!frame) {
        return;
    }
    const Projector& projector = painter.projector();
    const auto handleColor = [&](const GizmoHandle& handle) {
        const bool active =
            m_drag.kind == DragKind::Gizmo ? m_drag.handle == handle : m_hoverHandle == handle;
        return active ? kGizmoActiveColor : kAxisColors[handle.axis];
    };
    const float thickness = 3.0f;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (m_tool == EditorTool::Move) {
            const GizmoHandle arrow{GizmoHandle::Kind::Arrow, axis};
            const Vec3 tip = frame->centre + axisVector(axis) * frame->size;
            painter.line(frame->centre, tip, handleColor(arrow), thickness);
            const auto base = projector.screen(frame->centre);
            const auto end = projector.screen(tip);
            if (base && end) {
                // An arrowhead in screen space, pointing along the projected axis.
                float dx = end->x - base->x;
                float dy = end->y - base->y;
                const float lengthOnScreen = std::sqrt(dx * dx + dy * dy);
                if (lengthOnScreen > 1.0f) {
                    dx /= lengthOnScreen;
                    dy /= lengthOnScreen;
                    const float head = ImGui::GetFontSize() * 0.7f;
                    const ImVec2 back{end->x - dx * head, end->y - dy * head};
                    drawList.AddTriangleFilled(
                        {end->x + dx * head * 0.3f, end->y + dy * head * 0.3f},
                        {back.x - dy * head * 0.4f, back.y + dx * head * 0.4f},
                        {back.x + dy * head * 0.4f, back.y - dx * head * 0.4f}, handleColor(arrow));
                }
            }
            const GizmoHandle plane{GizmoHandle::Kind::Plane, axis};
            std::array<ImVec2, 4> quad;
            bool visible = true;
            const std::array<Vec3, 4> square = planeSquare(frame->centre, axis, frame->size);
            for (std::size_t i = 0; i < quad.size(); ++i) {
                const auto corner = projector.screen(square[i]);
                visible &= corner.has_value();
                quad[i] = corner.value_or(ImVec2{});
            }
            if (visible) {
                const ImU32 color = handleColor(plane);
                drawList.AddConvexPolyFilled(quad.data(), static_cast<int>(quad.size()),
                                             (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 90));
                drawList.AddPolyline(quad.data(), static_cast<int>(quad.size()), color,
                                     ImDrawFlags_Closed, 1.5f);
            }
        } else if (m_tool == EditorTool::Rotate) {
            const GizmoHandle ring{GizmoHandle::Kind::Ring, axis};
            for (int segment = 0; segment < kRingSegments; ++segment) {
                painter.line(ringPoint(frame->centre, axis, frame->size, segment),
                             ringPoint(frame->centre, axis, frame->size, segment + 1),
                             handleColor(ring), thickness - 0.5f);
            }
        } else if (m_tool == EditorTool::Scale &&
                   component(frame->extent.max - frame->extent.min, axis) > 0.0f) {
            for (const bool maxSide : {false, true}) {
                const GizmoHandle side{GizmoHandle::Kind::Side, axis, maxSide};
                if (const auto point =
                        projector.screen(sideHandlePoint(frame->extent, axis, maxSide))) {
                    const float half = ImGui::GetFontSize() * 0.3f;
                    drawList.AddRectFilled({point->x - half, point->y - half},
                                           {point->x + half, point->y + half}, handleColor(side));
                }
            }
        }
    }
    if (m_tool == EditorTool::Scale) {
        painter.box(frame->extent, kExtentColor, 1.0f);
    }
    if (m_drag.kind == DragKind::Gizmo && m_drag.handle.kind == GizmoHandle::Kind::Ring) {
        const std::string angle =
            std::format("{:+g}\xC2\xB0", snapAngle(m_drag.angle, settings.rotationStep()));
        drawList.AddText({m_drag.screenStartX + ImGui::GetFontSize(),
                          m_drag.screenStartY - ImGui::GetFontSize() * 2.0f},
                         kGizmoActiveColor, angle.c_str());
    }
}

} // namespace ct
