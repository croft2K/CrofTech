#version 450

// ImGui colours are already display-encoded and the swapchain is UNORM, so they are written as is.

layout(set = 0, binding = 0) uniform sampler2D uiTexture;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = inColor * texture(uiTexture, inUv);
}
