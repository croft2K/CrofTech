#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    vec2 gridSpacing; // used by world.frag
    vec4 offset;      // xyz: how far the part being drawn has moved (a mover)
} pc;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outColor;

void main() {
    // The grid is drawn from where the surface was built, so it moves with a mover.
    outWorldPosition = inPosition;
    outNormal = inNormal;
    outColor = inColor;
    gl_Position = pc.viewProjection * vec4(inPosition + pc.offset.xyz, 1.0);
}
