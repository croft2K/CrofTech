#pragma once

#include "croftech/core/Math.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

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
    void drawFrame(const Camera& camera);

    struct Impl;
    // Use create(). Impl is only defined inside the renderer, so nothing else can build one.
    explicit Renderer(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace ct
