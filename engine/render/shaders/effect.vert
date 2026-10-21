#version 450

// Effects: unlit, see-through triangles (tracers, flashes, glows), added over the scene.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inAlpha;

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(inColor, inAlpha);
    gl_Position = pc.viewProjection * vec4(inPosition, 1.0);
}
