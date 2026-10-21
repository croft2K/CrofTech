#version 450

layout(location = 0) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// The swapchain is UNORM, so colours are encoded to sRGB here, as world.frag does. Blending adds
// colour times alpha, so effects brighten what is behind them and never darken it.
vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main() {
    outColor = vec4(linearToSrgb(inColor.rgb), inColor.a);
}
