// CrofTech editor: opens a level, edits it with undo and redo, and saves it as a .level file.
//
// The window is a dockspace: the 3D view fills the middle, with the hierarchy, inspector and
// console docked around it. Hold the right mouse button in the view to fly (WASD, Q/E down/up,
// Shift faster). Shortcuts: Ctrl+S save, Ctrl+Z/Ctrl+Y undo/redo, Ctrl+D duplicate, Del delete,
// B box tool, F frame selection, [ and ] grid size, Esc cancel or deselect.

#include "EditorPanels.h"
#include "LevelViewport.h"
#include "WorldMesh.h"

#include "croftech/core/Console.h"
#include "croftech/core/Content.h"
#include "croftech/core/Log.h"
#include "croftech/devui/ConsoleWindow.h"
#include "croftech/devui/DevUi.h"
#include "croftech/editor/EditorActions.h"
#include "croftech/level/BrushGeometry.h"
#include "croftech/level/LevelLoad.h"
#include "croftech/render/Renderer.h"
#include "croftech/sim/MovementConfig.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

const std::filesystem::path kSourceDirectory = CROFTECH_SOURCE_DIR;
constexpr float kFrameDistance = 6.0f;

struct CommandLine {
    std::string game = "sandbox";
    std::string map; // level name or path; empty means the game's start_map
    int frameLimit = -1;
};

CommandLine parseCommandLine(int argc, char** argv) {
    CommandLine commandLine;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--game") {
            commandLine.game = argv[++i];
        } else if (option == "--map") {
            commandLine.map = argv[++i];
        } else if (option == "--frames") {
            commandLine.frameLimit = std::atoi(argv[++i]);
        }
    }
    return commandLine;
}

// The game's movement settings, for the player size spawn markers show.
ct::Vec3 playerHalfExtents(const ct::ContentPaths& content) {
    ct::Console console;
    ct::registerMovementCVars(console);
    ct::LayeredConfig config(content, "config/movement.cfg");
    if (const auto layers = config.reloadIfChanged()) {
        for (const std::string& problem :
             console.applyConfig(std::string(ct::kMovementCVarPrefix), *layers)) {
            ct::logWarning("{}", problem);
        }
    }
    return ct::movementSettingsFromCVars(console).halfExtents;
}

ct::Camera startingCamera(const ct::Level& level) {
    ct::Camera camera;
    camera.position = {0.0f, -8.0f, 4.0f};
    camera.yawRadians = ct::kPi * 0.5f;
    camera.pitchRadians = -0.3f;
    for (const ct::LevelEntity& entity : level.entities) {
        if (entity.className == ct::kPlayerSpawnClass) {
            if (const auto origin = ct::entityOrigin(entity)) {
                // Behind and above the spawn, looking along +X with it in view.
                camera.position = *origin + ct::Vec3{-4.0f, 0.0f, 3.0f};
                camera.yawRadians = 0.0f;
                camera.pitchRadians = -0.35f;
                break;
            }
        }
    }
    return camera;
}

// The first time: hierarchy on the left, inspector on the right, console along the bottom.
void buildDefaultLayout(ImGuiID dockspace) {
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
    ImGuiID centre = dockspace;
    const ImGuiID left =
        ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.18f, nullptr, &centre);
    const ImGuiID right =
        ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.25f, nullptr, &centre);
    const ImGuiID bottom =
        ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.22f, nullptr, &centre);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderFinish(dockspace);
}

std::optional<ct::Vec3> selectionCentre(const ct::EditorDocument& document,
                                        const ct::EditorSettings& settings) {
    const ct::Selection selected = document.selection();
    const ct::Level& level = document.level();
    std::optional<ct::Aabb> bounds;
    if (selected.kind == ct::Selection::Kind::Brush) {
        ct::Aabb box;
        if (ct::computeBrushBounds(level.brushes[selected.index], box)) {
            bounds = box;
        }
    } else if (selected.kind == ct::Selection::Kind::Entity) {
        bounds = ct::entityMarker(level.entities[selected.index], settings);
    }
    if (!bounds) {
        return std::nullopt;
    }
    return (bounds->min + bounds->max) * 0.5f;
}

void save(ct::EditorDocument& document) {
    std::string error;
    if (document.save(error)) {
        std::filesystem::path shown = document.path();
        ct::logInfo("Saved {}", shown.make_preferred().string());
    } else {
        ct::logError("Save failed: {}", error);
    }
}

} // namespace

int main(int argc, char** argv) {
    const CommandLine commandLine = parseCommandLine(argc, argv);

    std::string error;
    const std::optional<ct::GameManifest> game =
        ct::loadGameManifest(kSourceDirectory / "games", commandLine.game, error);
    if (!game) {
        ct::logError("{}", error);
        return EXIT_FAILURE;
    }
    ct::ContentPaths content;
    content.addLayer(kSourceDirectory / "content");
    content.addLayer(game->root / "content");

    const std::string& levelName = commandLine.map.empty() ? game->startMap : commandLine.map;
    const std::optional<std::filesystem::path> levelPath =
        ct::findLevelFile(content, levelName, error);
    std::vector<std::string> warnings;
    std::optional<ct::Level> level =
        levelPath ? ct::loadAnyLevelFile(*levelPath, warnings, error) : std::nullopt;
    for (const std::string& warning : warnings) {
        ct::logWarning("{}", warning);
    }
    if (!level) {
        ct::logError("map {}: {}", levelName, error);
        return EXIT_FAILURE;
    }

    ct::EditorSettings settings;
    settings.playerHalfExtents = playerHalfExtents(content);
    ct::LevelViewport viewport(startingCamera(*level));
    ct::EditorDocument document(std::move(*level), *levelPath);
    ct::logInfo("Editing {} ({} brushes, {} entities)", levelPath->string(),
                document.level().brushes.size(), document.level().entities.size());

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ct::logError("SDL: {}", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_Window* window = SDL_CreateWindow("CrofTech Editor", 1600, 900,
                                          SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                              SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED);
    if (window == nullptr) {
        ct::logError("window: {}", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }
    auto devUi = std::make_unique<ct::DevUi>(window);
    std::unique_ptr<ct::Renderer> renderer =
        ct::Renderer::create(window, std::filesystem::path(SDL_GetBasePath()) / "shaders", error);
    if (!renderer) {
        ct::logError("renderer: {}", error);
        devUi.reset();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    ct::Console console;
    ct::ConsoleWindow consoleWindow(console, ct::ConsoleStyle::Panel);
    ct::EditorPanels panels;
    bool running = true;
    bool quitRequested = false;
    bool flying = false;
    bool layoutBuilt = false;
    std::uint64_t meshChangeCount = ~0ull;
    std::string shownTitle;
    Clock::time_point lastFrame = Clock::now();
    int frameCount = 0;

    const auto setFlying = [&](bool fly) {
        flying = fly;
        SDL_SetWindowRelativeMouseMode(window, fly);
        devUi->setMouseCaptured(fly);
    };

    while (running) {
        ct::FlyInput flyInput;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            devUi->processEvent(event);
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quitRequested = true;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (flying) {
                    flyInput.lookX += event.motion.xrel;
                    flyInput.lookY += event.motion.yrel;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (flying && event.button.button == SDL_BUTTON_RIGHT) {
                    setFlying(false);
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (flying) {
                    setFlying(false);
                }
                break;
            default:
                break;
            }
        }
        const Clock::time_point now = Clock::now();
        flyInput.seconds = std::min(std::chrono::duration<float>(now - lastFrame).count(), 0.1f);
        lastFrame = now;
        if (flying) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            flyInput.forward = keys[SDL_SCANCODE_W];
            flyInput.back = keys[SDL_SCANCODE_S];
            flyInput.left = keys[SDL_SCANCODE_A];
            flyInput.right = keys[SDL_SCANCODE_D];
            flyInput.up = keys[SDL_SCANCODE_E];
            flyInput.down = keys[SDL_SCANCODE_Q];
            flyInput.fast = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
            viewport.fly(flyInput);
        }

        devUi->beginFrame();
        const ct::MenuRequests menu = panels.drawMenuBar(document, settings, viewport);
        const ImGuiID dockspace =
            ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        if (!layoutBuilt) {
            buildDefaultLayout(dockspace);
            layoutBuilt = true;
        }
        panels.drawHierarchy(document);
        panels.drawInspector(document, settings);
        consoleWindow.draw();

        ct::ViewRect view;
        if (const ImGuiDockNode* centre = ImGui::DockBuilderGetCentralNode(dockspace)) {
            view = {centre->Pos.x, centre->Pos.y, centre->Size.x, centre->Size.y};
        }
        const ImGuiIO& io = ImGui::GetIO();
        const bool mouseOverView =
            !flying && !io.WantCaptureMouse && view.contains(io.MousePos.x, io.MousePos.y);
        if (mouseOverView && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            setFlying(true);
        }
        if (!flying) {
            viewport.update(document, settings, view, mouseOverView);
        }
        viewport.drawOverlay(document, settings, view);

        // Shortcuts, unless a text field has the keyboard.
        bool frameSelection = menu.frameSelection;
        bool addSpawn = menu.addPlayerSpawn;
        if (!flying && !io.WantTextInput) {
            const float grid = settings.grid();
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
                save(document);
            } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
                viewport.cancelDrag(document);
                document.undo();
            } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
                       ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
                viewport.cancelDrag(document);
                document.redo();
            } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
                ct::duplicateSelection(document, {grid, grid, 0.0f});
            } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !viewport.isDragging()) {
                ct::deleteSelection(document);
            } else if (ImGui::IsKeyPressed(ImGuiKey_B, false)) {
                viewport.setTool(ct::EditorTool::Box);
            } else if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                frameSelection = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
                settings.gridIndex = settings.gridIndex > 0 ? settings.gridIndex - 1 : 0;
            } else if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
                settings.gridIndex = std::min(settings.gridIndex + 1, ct::kGridSizes.size() - 1);
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                if (viewport.isDragging()) {
                    viewport.cancelDrag(document);
                } else if (viewport.tool() != ct::EditorTool::Select) {
                    viewport.setTool(ct::EditorTool::Select);
                } else {
                    document.select({});
                }
            }
        }
        if (menu.save) {
            save(document);
        }
        if (addSpawn) {
            ct::addEntity(document,
                          ct::makePlayerSpawn(viewport.placementPoint(document, settings), 0.0f));
        }
        if (frameSelection) {
            if (const auto centre = selectionCentre(document, settings)) {
                viewport.frame(*centre, kFrameDistance);
            }
        }
        if (menu.quit) {
            quitRequested = true;
        }

        // Leaving with unsaved changes asks first.
        if (quitRequested) {
            if (document.hasUnsavedChanges()) {
                ImGui::OpenPopup("Unsaved changes");
            } else {
                running = false;
            }
            quitRequested = false;
        }
        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Save changes to %s before quitting?",
                        document.path().filename().string().c_str());
            if (ImGui::Button("Save and quit")) {
                save(document);
                running = document.hasUnsavedChanges() ? running : false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Quit without saving")) {
                running = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        const ImDrawData* ui = devUi->endFrame();
        if (document.changeCount() != meshChangeCount) {
            meshChangeCount = document.changeCount();
            const ct::WorldMesh mesh = ct::buildWorldMesh(document.level());
            renderer->setWorldMesh(mesh.vertices, mesh.indices);
        }
        const std::string title =
            std::format("CrofTech Editor - {}{}", document.path().filename().string(),
                        document.hasUnsavedChanges() ? " *" : "");
        if (title != shownTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            shownTitle = title;
        }
        const ct::PixelRect worldRect{
            view.x * io.DisplayFramebufferScale.x, view.y * io.DisplayFramebufferScale.y,
            view.width * io.DisplayFramebufferScale.x, view.height * io.DisplayFramebufferScale.y};
        const float grid = settings.grid();
        renderer->setWorldGrid(grid, grid < 1.0f ? 1.0f : grid * 8.0f);
        renderer->drawFrame(viewport.camera(), ui, &worldRect);

        ++frameCount;
        if (commandLine.frameLimit >= 0 && frameCount >= commandLine.frameLimit) {
            running = false;
        }
    }

    renderer.reset(); // before the ImGui context it serves
    devUi.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
