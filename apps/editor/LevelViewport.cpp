#include "LevelViewport.h"

#include "croftech/editor/EditorActions.h"
#include "croftech/editor/LevelCommands.h"
#include "croftech/level/BrushGeometry.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace ct {
namespace {

constexpr float kLookRadiansPerCount = 0.0035f;
constexpr float kFlySpeed = 8.0f; // m/s
constexpr float kFastFlyFactor = 4.0f;
constexpr float kMaxPitch = 1.55f;
constexpr float kPlacementDistance = 4.0f;
constexpr float kEntityMarkerHalfSize = 0.25f;

constexpr ImU32 kHoverColor = IM_COL32(255, 255, 255, 140);
constexpr ImU32 kSelectedColor = IM_COL32(255, 210, 40, 255);
constexpr ImU32 kFaceColor = IM_COL32(80, 200, 255, 255);
constexpr ImU32 kSpawnColor = IM_COL32(90, 230, 120, 230);
constexpr ImU32 kEntityColor = IM_COL32(255, 150, 60, 230);

struct Clip {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
};

Clip toClip(const Mat4& m, Vec3 p) {
    // Rows 0, 1 and 3 of the column-major matrix; depth isn't needed for drawing lines.
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15]};
}

// Draws world-space lines over the view, clipped against the near plane.
class LinePainter {
public:
    LinePainter(const Camera& camera, const ViewRect& rect)
        : m_viewProjection(makeViewProjection(camera, rect.aspect())), m_rect(rect),
          m_drawList(ImGui::GetBackgroundDrawList()) {
        m_drawList->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height});
    }
    ~LinePainter() { m_drawList->PopClipRect(); }
    LinePainter(const LinePainter&) = delete;
    LinePainter& operator=(const LinePainter&) = delete;

    void line(Vec3 a, Vec3 b, ImU32 color, float thickness = 1.0f) const {
        Clip ca = toClip(m_viewProjection, a);
        Clip cb = toClip(m_viewProjection, b);
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
        m_drawList->AddLine(toScreen(ca), toScreen(cb), color, thickness);
    }

    void box(const Aabb& box, ImU32 color, float thickness = 1.0f) const {
        const Vec3 a = box.min;
        const Vec3 b = box.max;
        const std::array<Vec3, 8> c{{{a.x, a.y, a.z},
                                     {b.x, a.y, a.z},
                                     {b.x, b.y, a.z},
                                     {a.x, b.y, a.z},
                                     {a.x, a.y, b.z},
                                     {b.x, a.y, b.z},
                                     {b.x, b.y, b.z},
                                     {a.x, b.y, b.z}}};
        for (std::size_t i = 0; i < 4; ++i) {
            line(c[i], c[(i + 1) % 4], color, thickness);
            line(c[i + 4], c[(i + 1) % 4 + 4], color, thickness);
            line(c[i], c[i + 4], color, thickness);
        }
    }

    void polygon(const std::vector<Vec3>& points, ImU32 color, float thickness = 1.0f) const {
        for (std::size_t i = 0; i < points.size(); ++i) {
            line(points[i], points[(i + 1) % points.size()], color, thickness);
        }
    }

    void brush(const Brush& brush, ImU32 color, float thickness = 1.0f) const {
        for (const BrushFace& face : buildBrushFaces(brush)) {
            polygon(face.vertices, color, thickness);
        }
    }

private:
    ImVec2 toScreen(const Clip& clip) const {
        return {m_rect.x + (clip.x / clip.w * 0.5f + 0.5f) * m_rect.width,
                m_rect.y + (clip.y / clip.w * 0.5f + 0.5f) * m_rect.height};
    }

    Mat4 m_viewProjection;
    ViewRect m_rect;
    ImDrawList* m_drawList;
};

std::optional<BrushFace> faceOfPlane(const Brush& brush, std::size_t plane) {
    for (BrushFace& face : buildBrushFaces(brush)) {
        if (face.plane == brush.planes[plane]) {
            return std::move(face);
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<Aabb> entityMarker(const LevelEntity& entity, const EditorSettings& settings) {
    const std::optional<Vec3> origin = entityOrigin(entity);
    if (!origin) {
        return std::nullopt;
    }
    if (entity.className == kPlayerSpawnClass) {
        const Vec3 half = settings.playerHalfExtents;
        return Aabb{*origin - Vec3{half.x, half.y, 0.0f},
                    *origin + Vec3{half.x, half.y, half.z * 2.0f}};
    }
    const Vec3 half{kEntityMarkerHalfSize, kEntityMarkerHalfSize, kEntityMarkerHalfSize};
    return Aabb{*origin - half, *origin + half};
}

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
            best = Pick{Selection::brush(i), hit->distance, hit->plane};
        }
    }
    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        const std::optional<Aabb> marker = entityMarker(level.entities[i], settings);
        if (!marker) {
            continue;
        }
        const auto distance = rayCastBox(*marker, ray.origin, ray.direction);
        // Entities win ties with the brush they stand in, so spawns inside walls stay clickable.
        if (distance && (!best || *distance <= best->distance)) {
            best = Pick{Selection::entity(i), *distance, 0};
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

void LevelViewport::update(EditorDocument& document, const EditorSettings& settings,
                           const ViewRect& rect, bool mouseOverView) {
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    const Ray ray = mouseRay(rect);
    if (isDragging()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            continueDrag(document, settings, ray);
        } else {
            endDrag(document, settings);
        }
        return;
    }

    m_hover = mouseOverView ? pick(document, settings, ray) : std::nullopt;
    if (mouseOverView && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        beginDrag(document, settings, ray);
    }
    if (mouseOverView && ImGui::GetIO().MouseWheel != 0.0f) {
        m_camera.position += cameraBasis(m_camera).forward * ImGui::GetIO().MouseWheel;
    }
}

void LevelViewport::beginDrag(EditorDocument& document, const EditorSettings& settings,
                              const Ray& ray) {
    const float grid = settings.grid();
    if (m_tool == EditorTool::Box) {
        const std::optional<Vec3> point = surfacePoint(document, ray);
        if (point) {
            m_drag = {};
            m_drag.kind = DragKind::Box;
            m_drag.boxStart = snapToGrid(*point, grid);
            m_drag.boxEnd = m_drag.boxStart;
        }
        return;
    }

    if (!m_hover) {
        document.select({});
        return;
    }
    const Pick hit = *m_hover;
    const Level& level = document.level();
    const bool shift = ImGui::GetIO().KeyShift;
    m_drag = {};
    m_drag.target = hit.target;
    m_drag.grabPoint = ray.origin + ray.direction * hit.distance;
    if (hit.target.kind == Selection::Kind::Brush) {
        m_drag.brushBefore = level.brushes[hit.target.index];
        if (shift && document.selection() == hit.target) {
            m_drag.kind = DragKind::Face;
            m_drag.plane = hit.plane;
            return;
        }
    } else {
        m_drag.entityBefore = level.entities[hit.target.index];
    }
    document.select(hit.target);
    m_drag.kind = ImGui::GetIO().KeyAlt ? DragKind::MoveVertical : DragKind::Move;
}

void LevelViewport::continueDrag(EditorDocument& document, const EditorSettings& settings,
                                 const Ray& ray) {
    const float grid = settings.grid();
    if (m_drag.kind == DragKind::Box) {
        const Plane base{{0.0f, 0.0f, 1.0f}, m_drag.boxStart.z};
        if (const auto t = rayCastPlane(base, ray.origin, ray.direction)) {
            const Vec3 point = snapToGrid(ray.origin + ray.direction * *t, grid);
            m_drag.boxEnd = {point.x, point.y, m_drag.boxStart.z};
        }
        return;
    }

    Vec3 offset;
    if (m_drag.kind == DragKind::Move) {
        const Plane level{{0.0f, 0.0f, 1.0f}, m_drag.grabPoint.z};
        const auto t = rayCastPlane(level, ray.origin, ray.direction);
        if (!t) {
            return;
        }
        offset = snapToGrid(ray.origin + ray.direction * *t - m_drag.grabPoint, grid);
        offset.z = 0.0f;
    } else if (m_drag.kind == DragKind::MoveVertical) {
        const auto s =
            closestAlongAxis(m_drag.grabPoint, {0.0f, 0.0f, 1.0f}, ray.origin, ray.direction);
        if (!s) {
            return;
        }
        offset = {0.0f, 0.0f, snapToGrid(*s, grid)};
    } else if (m_drag.kind == DragKind::Face) {
        const Plane before = m_drag.brushBefore.planes[m_drag.plane];
        const auto s = closestAlongAxis(m_drag.grabPoint, before.normal, ray.origin, ray.direction);
        if (!s) {
            return;
        }
        // An axis-aligned face lands on the grid; a sloped one moves in grid steps.
        const float distance = isAxial(before.normal) ? snapToGrid(before.distance + *s, grid)
                                                      : before.distance + snapToGrid(*s, grid);
        if (const auto resized = withPlaneDistance(m_drag.brushBefore, m_drag.plane, distance)) {
            document.previewBrush(m_drag.target.index, *resized);
        }
        return;
    }

    if (m_drag.target.kind == Selection::Kind::Brush) {
        const Brush moved = translatedBrush(m_drag.brushBefore, offset);
        std::string problem;
        if (checkBrush(moved, problem)) {
            document.previewBrush(m_drag.target.index, moved);
        }
    } else if (const auto origin = entityOrigin(m_drag.entityBefore)) {
        LevelEntity moved = m_drag.entityBefore;
        setProperty(moved, "origin", formatVec3(*origin + offset));
        document.previewEntity(m_drag.target.index, moved);
    }
}

void LevelViewport::endDrag(EditorDocument& document, const EditorSettings& settings) {
    const Drag drag = m_drag;
    m_drag = {};
    const Level& level = document.level();
    switch (drag.kind) {
    case DragKind::Box: {
        const Vec3 min = ct::min(drag.boxStart, drag.boxEnd);
        Vec3 max = ct::max(drag.boxStart, drag.boxEnd);
        if (max.x - min.x < settings.grid() || max.y - min.y < settings.grid()) {
            return; // a click, not a drag
        }
        max.z = min.z + std::max(settings.grid(), 1.0f);
        addBox(document, min, max);
        m_tool = EditorTool::Select;
        return;
    }
    case DragKind::Move:
    case DragKind::MoveVertical:
    case DragKind::Face:
        if (drag.target.kind == Selection::Kind::Brush) {
            const Brush& after = level.brushes[drag.target.index];
            if (!(after == drag.brushBefore)) {
                document.execute(
                    makeSetBrush(drag.kind == DragKind::Face ? "Resize brush" : "Move brush",
                                 drag.target.index, drag.brushBefore, after));
            }
        } else {
            const LevelEntity& after = level.entities[drag.target.index];
            if (!(after == drag.entityBefore)) {
                document.execute(
                    makeSetEntity("Move entity", drag.target.index, drag.entityBefore, after));
            }
        }
        return;
    case DragKind::None:
        return;
    }
}

void LevelViewport::cancelDrag(EditorDocument& document) {
    if (m_drag.target.kind == Selection::Kind::Brush &&
        (m_drag.kind == DragKind::Move || m_drag.kind == DragKind::MoveVertical ||
         m_drag.kind == DragKind::Face)) {
        document.previewBrush(m_drag.target.index, m_drag.brushBefore);
    } else if (m_drag.target.kind == Selection::Kind::Entity) {
        document.previewEntity(m_drag.target.index, m_drag.entityBefore);
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
    const float grid = settings.grid();

    for (std::size_t i = 0; i < level.entities.size(); ++i) {
        if (const auto marker = entityMarker(level.entities[i], settings)) {
            const bool spawn = level.entities[i].className == kPlayerSpawnClass;
            painter.box(*marker, spawn ? kSpawnColor : kEntityColor, 1.5f);
        }
    }

    const auto outline = [&](const Selection& target, ImU32 color, float thickness) {
        if (target.kind == Selection::Kind::Brush && target.index < level.brushes.size()) {
            painter.brush(level.brushes[target.index], color, thickness);
        } else if (target.kind == Selection::Kind::Entity && target.index < level.entities.size()) {
            if (const auto marker = entityMarker(level.entities[target.index], settings)) {
                painter.box(*marker, color, thickness);
            }
        }
    };
    if (m_hover && !isDragging() && !(m_hover->target == document.selection())) {
        outline(m_hover->target, kHoverColor, 1.0f);
    }
    outline(document.selection(), kSelectedColor, 2.0f);

    // The face Shift-drag would move, or is moving.
    const Selection selected = document.selection();
    const bool showFace =
        m_drag.kind == DragKind::Face ||
        (!isDragging() && ImGui::GetIO().KeyShift && m_hover && m_hover->target == selected);
    if (showFace && selected.kind == Selection::Kind::Brush) {
        const std::size_t plane = m_drag.kind == DragKind::Face ? m_drag.plane : m_hover->plane;
        if (const auto face = faceOfPlane(level.brushes[selected.index], plane)) {
            painter.polygon(face->vertices, kFaceColor, 3.0f);
        }
    }

    if (m_drag.kind == DragKind::Box) {
        const Vec3 min = ct::min(m_drag.boxStart, m_drag.boxEnd);
        Vec3 max = ct::max(m_drag.boxStart, m_drag.boxEnd);
        max.z = min.z + std::max(grid, 1.0f);
        painter.box({min, max}, kSelectedColor, 2.0f);
    }
}

} // namespace ct
