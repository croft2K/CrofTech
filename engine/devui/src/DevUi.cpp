#include "croftech/devui/DevUi.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <cfloat>

namespace ct {
namespace {

constexpr float kFontSize = 15.0f; // before display scaling

} // namespace

DevUi::DevUi(SDL_Window* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr; // layouts are not saved yet; the editor will choose where they go
    // ct::Renderer honours vertex offsets and serves texture requests (see UiRenderer).
    io.BackendFlags |=
        ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    io.BackendRendererName = "CrofTech Vulkan";

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    const float displayScale = SDL_GetWindowDisplayScale(window);
    if (displayScale > 0.0f) {
        style.ScaleAllSizes(displayScale);
        style.FontScaleDpi = displayScale;
    }
    ImFontConfig fontConfig;
    fontConfig.SizePixels = kFontSize;
    io.Fonts->AddFontDefaultVector(&fontConfig);
    style.FontSizeBase = kFontSize;

    ImGui_ImplSDL3_InitForVulkan(window);
}

DevUi::~DevUi() {
    ImGui_ImplSDL3_Shutdown();
    ImGui::GetIO().BackendRendererName = nullptr;
    ImGui::DestroyContext();
}

void DevUi::processEvent(const SDL_Event& event) {
    // Button releases always go through. The SDL backend tracks which buttons are down and asks
    // SDL to capture the mouse while any is; a release it never saw (the button that started a
    // capture, let go during it) left the window capturing the mouse for good, which turns clicks
    // on the title bar's minimise, maximise and close buttons into ordinary clicks in the window.
    if (m_mouseCaptured) {
        switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_WHEEL:
            return;
        default:
            break;
        }
    }
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void DevUi::setMouseCaptured(bool captured) {
    m_mouseCaptured = captured;
    ImGuiIO& io = ImGui::GetIO();
    if (captured) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    } else {
        io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange);
        // Leave ImGui with no buttons held and the real cursor position. A button it still thinks
        // is held, pressed outside its windows, stops every window being hovered: menus and panels
        // would ignore the mouse until that button happened to be released over the UI.
        for (int button = 0; button < ImGuiMouseButton_COUNT; ++button) {
            io.AddMouseButtonEvent(button, false);
        }
        float x = 0.0f;
        float y = 0.0f;
        SDL_GetMouseState(&x, &y);
        io.AddMousePosEvent(x, y);
    }
}

void DevUi::beginFrame() {
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

const ImDrawData* DevUi::endFrame() {
    ImGui::Render();
    return ImGui::GetDrawData();
}

} // namespace ct
