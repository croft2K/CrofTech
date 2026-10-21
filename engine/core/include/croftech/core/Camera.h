#pragma once

#include "croftech/core/Math.h"
#include "croftech/core/Matrix.h"

namespace ct {

// A perspective view, shared by the renderer and by tools that turn screen positions into rays,
// so picking always matches what is drawn. Presentation code only: uses std::sin and std::cos.
struct Camera {
    Vec3 position;
    float yawRadians = 0.0f;   // 0 faces +X, increasing towards +Y
    float pitchRadians = 0.0f; // positive looks up
    // Horizontal FOV at 16:9 (100 degrees by default); other aspects keep the same height.
    float horizontalFovRadians16x9 = 1.745f;
};

inline constexpr float kCameraNearPlane = 0.05f;

struct CameraBasis {
    Vec3 forward;
    Vec3 right;
    Vec3 up;
};

CameraBasis cameraBasis(const Camera& camera);

// World to clip space. View space is right-handed and looks down -Z; the projection is reverse-Z
// with an infinite far plane (depth 1 at the near plane, 0 at infinity) and flips Y for Vulkan.
Mat4 makeViewProjection(const Camera& camera, float aspect);

// The direction through a point of the view, given in normalized coordinates: x from -1 (left) to
// 1 (right), y from -1 (top) to 1 (bottom). Starts at camera.position. Unit length.
Vec3 cameraRayDirection(const Camera& camera, float aspect, float x, float y);

} // namespace ct
