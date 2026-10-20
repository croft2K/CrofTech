#pragma once

#include "croftech/core/Math.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

struct ImDrawData;
struct SDL_Window;

namespace ct {

struct WorldVertex {
    Vec3 position;
    Vec3 normal;
    Vec3 color; // linear RGB
};

struct Camera {
    Vec3 position;
    float yawRadians = 0.0f;   // 0 faces +X, increasing towards +Y
    float pitchRadians = 0.0f; // positive looks up
    // Horizontal FOV at 16:9 (100 degrees by default); other aspects keep the same height.
    float horizontalFovRadians16x9 = 1.745f;
};

// Vulkan renderer. Reads presentation data only; it never sees or writes simulation state.
// It also draws Dear ImGui output (see ct::DevUi): create it after the ImGui context and destroy it
// before, because it hands texture state back to ImGui on shutdown.
class Renderer {
public:
    // Returns null and sets error on failure. Shaders are loaded from shaderDirectory.
    static std::unique_ptr<Renderer>
    create(SDL_Window* window, const std::filesystem::path& shaderDirectory, std::string& error);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void setWorldMesh(std::span<const WorldVertex> vertices,
                      std::span<const std::uint32_t> indices);
    // ui (may be null) is this frame's ImGui::GetDrawData(), drawn over the world.
    void drawFrame(const Camera& camera, const ImDrawData* ui = nullptr);

    struct Impl;
    // Use create(). Impl is only defined inside the renderer, so nothing else can build one.
    explicit Renderer(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace ct
