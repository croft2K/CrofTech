#pragma once

#include "croftech/core/Camera.h"
#include "croftech/core/Math.h"
#include "croftech/core/Matrix.h"

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

// A run of the world mesh's indices that moves as one: the brushes of a mover.
struct WorldMeshPart {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

// A region of the window in pixels, for drawing the world into part of it (an editor viewport).
struct PixelRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// One view of the world drawn into part of the window: an editor pane, or the whole game view.
struct WorldView {
    Mat4 viewProjection; // world to clip space, reverse-Z (see makeViewProjection)
    PixelRect rect;
    // Cleared to this colour (as shown, sRGB) first; otherwise the sky colour shows behind.
    bool clearBackground = false;
    Vec3 background;
    bool drawWorld = true; // false leaves just the background, for an overlay to draw on
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

    // Parts follow the indices that never move, in order. Each is drawn moved by its offset from
    // setWorldPartOffsets, zero until set; its surface grid moves with it.
    void setWorldMesh(std::span<const WorldVertex> vertices, std::span<const std::uint32_t> indices,
                      std::span<const WorldMeshPart> parts = {});
    // One offset per part of the current mesh, in order.
    void setWorldPartOffsets(std::span<const Vec3> offsets);
    // Spacing of the grid lines drawn on every world surface, in metres (1 and 4 by default).
    void setWorldGrid(float minorSpacing, float majorSpacing);
    // ui (may be null) is this frame's ImGui::GetDrawData(), drawn over the world. The world fills
    // worldRect, or the whole window if it is null; its aspect ratio comes from that rectangle.
    void drawFrame(const Camera& camera, const ImDrawData* ui = nullptr,
                   const PixelRect* worldRect = nullptr);
    // Several views of the world side by side (they must not overlap), then ui over them.
    void drawFrame(std::span<const WorldView> views, const ImDrawData* ui);

    struct Impl;
    // Use create(). Impl is only defined inside the renderer, so nothing else can build one.
    explicit Renderer(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace ct
