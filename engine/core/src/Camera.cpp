#include "croftech/core/Camera.h"

#include <cmath>

namespace ct {
namespace {

// Hor+: the FOV is defined horizontally at 16:9, so the vertical FOV is fixed.
float focalLength(const Camera& camera) {
    return 1.0f / (std::tan(camera.horizontalFovRadians16x9 * 0.5f) * (9.0f / 16.0f));
}

} // namespace

CameraBasis cameraBasis(const Camera& camera) {
    const float cy = std::cos(camera.yawRadians);
    const float sy = std::sin(camera.yawRadians);
    const float cp = std::cos(camera.pitchRadians);
    const float sp = std::sin(camera.pitchRadians);
    const Vec3 forward{cp * cy, cp * sy, sp};
    const Vec3 right{sy, -cy, 0.0f};
    return {forward, right, cross(right, forward)};
}

Mat4 makeViewProjection(const Camera& camera, float aspect) {
    const auto [forward, right, up] = cameraBasis(camera);
    const Vec3 eye = camera.position;

    Mat4 view;
    view.m = {right.x,          up.x,          -forward.x,        0.0f, right.y,    up.y,
              -forward.y,       0.0f,          right.z,           up.z, -forward.z, 0.0f,
              -dot(right, eye), -dot(up, eye), dot(forward, eye), 1.0f};

    const float focal = focalLength(camera);
    Mat4 projection;
    projection.m[0] = focal / aspect;
    projection.m[5] = -focal;
    projection.m[11] = -1.0f;
    projection.m[14] = kCameraNearPlane;
    return projection * view;
}

Vec3 cameraRayDirection(const Camera& camera, float aspect, float x, float y) {
    const auto [forward, right, up] = cameraBasis(camera);
    const float focal = focalLength(camera);
    return normalize(forward + right * (x * aspect / focal) - up * (y / focal));
}

} // namespace ct
