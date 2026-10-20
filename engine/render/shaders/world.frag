#version 450

// Blockout shading: one sun plus sky light, and world-space grid lines (1 m minor, 4 m major) so
// speed, distance and heights read clearly while tuning movement.

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec4 outColor;

// The swapchain is UNORM, so lighting is done in linear space and encoded to sRGB here.
vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

float gridLine(vec2 coord) {
    vec2 width = max(fwidth(coord), vec2(1e-4));
    vec2 distanceToLine = abs(fract(coord - 0.5) - 0.5) / width;
    return 1.0 - min(min(distanceToLine.x, distanceToLine.y), 1.0);
}

void main() {
    vec3 n = normalize(inNormal);
    vec3 a = abs(n);
    vec2 uv = (a.z >= a.x && a.z >= a.y) ? inWorldPosition.xy
            : (a.x >= a.y)               ? inWorldPosition.yz
                                         : inWorldPosition.xz;

    const vec3 sunDirection = normalize(vec3(0.45, 0.3, 0.85));
    float sun = max(dot(n, sunDirection), 0.0);
    float sky = 0.55 + 0.45 * n.z;
    vec3 lit = inColor * (sun * vec3(1.0, 0.94, 0.84) * 0.85 + sky * vec3(0.32, 0.36, 0.45));

    float grid = max(gridLine(uv) * 0.18, gridLine(uv * 0.25) * 0.35);
    outColor = vec4(linearToSrgb(lit * (1.0 - grid)), 1.0);
}
