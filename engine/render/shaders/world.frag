#version 450

// Blockout shading: one sun plus sky light, and world-space grid lines (minor and major spacing
// from the renderer: 1 m and 4 m in the game, the snapping grid in the editor) so distances and
// heights read clearly.

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    vec2 gridSpacing; // minor, major
    vec4 offset;      // used by world.vert
} pc;

layout(location = 0) out vec4 outColor;

// The swapchain is UNORM, so lighting is done in linear space and encoded to sRGB here.
vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// 1 on a grid line, 0 between; fades out where cells shrink to a few pixels so a fine grid
// doesn't turn distant surfaces dark.
float gridLine(vec2 coord) {
    vec2 width = max(fwidth(coord), vec2(1e-4));
    vec2 distanceToLine = abs(fract(coord - 0.5) - 0.5) / width;
    float line = 1.0 - min(min(distanceToLine.x, distanceToLine.y), 1.0);
    return line * (1.0 - smoothstep(0.15, 0.4, max(width.x, width.y)));
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

    // Shapes that aren't level geometry (players, items) are drawn with no grid: spacing 0.
    float grid = pc.gridSpacing.x > 0.0
        ? max(gridLine(uv / pc.gridSpacing.x) * 0.18, gridLine(uv / pc.gridSpacing.y) * 0.35)
        : 0.0;
    outColor = vec4(linearToSrgb(lit * (1.0 - grid)), 1.0);
}
