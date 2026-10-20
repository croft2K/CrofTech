#pragma once

struct ImDrawData;
struct SDL_Window;
union SDL_Event;

namespace ct {

// Owns the Dear ImGui context used for developer UI (console, overlays, and later the editor) and
// feeds it SDL input. ct::Renderer draws its output, so create the renderer after this and destroy
// it before.
//
// Each frame: processEvent() for every event, then beginFrame(), any ImGui calls, and endFrame()
// to get the draw data for Renderer::drawFrame().
class DevUi {
public:
    explicit DevUi(SDL_Window* window);
    ~DevUi();
    DevUi(const DevUi&) = delete;
    DevUi& operator=(const DevUi&) = delete;

    void processEvent(const SDL_Event& event);

    // While the game has the mouse (relative mode), ImGui must neither react to it nor change the
    // cursor.
    void setMouseCaptured(bool captured);

    void beginFrame();
    const ImDrawData* endFrame();

private:
    bool m_mouseCaptured = false;
};

} // namespace ct
