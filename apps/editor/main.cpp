// CrofTech editor: opens a level, edits it with undo and redo, and saves it as a .level file.
//
// The window is a dockspace: the 3D view fills the middle, with the hierarchy, inspector and
// console docked around it, and the tool bar over the view. Hold the right mouse button in the view
// to fly (WASD, Q/E down/up, Shift faster); in an orthographic pane it pans and the wheel zooms.
// F5 plays the level (Esc stops). Shortcuts: Q/W/E/R select, move, rotate and scale tools, V vertex
// tool, C clip tool, B shape tool, Ctrl+H/J/K hollow, merge and subtract the selected brushes,
// Ctrl+S save, Ctrl+Z/Ctrl+Y undo/redo, Ctrl+A select all, Ctrl+D duplicate, Del delete (in the
// vertex tool, the selected corners), arrow keys and Page Up/Down nudge the selection (or selected
// corners) one grid step, F frame selection, [ and ] grid size, Esc cancel or deselect.

#include "EditorPanels.h"
#include "LevelViewport.h"
#include "PlaySession.h"
#include "WorldMesh.h"

#include "croftech/core/Console.h"
#include "croftech/core/Content.h"
#include "croftech/core/Log.h"
#include "croftech/devui/ConsoleWindow.h"
#include "croftech/devui/DevUi.h"
#include "croftech/editor/EditorActions.h"
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
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

const std::filesystem::path kSourceDirectory = CROFTECH_SOURCE_DIR;
constexpr float kFrameDistance = 6.0f;
constexpr ct::Vec3 kOrthoBackground{0.15f, 0.15f, 0.17f};

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

// The game's movement settings: the player size spawn markers show, and how play-in-editor moves.
ct::MovementSettings loadMovementSettings(const ct::ContentPaths& content) {
    ct::Console console;
    ct::registerMovementCVars(console);
    ct::LayeredConfig config(content, "config/movement.cfg");
    if (const auto layers = config.reloadIfChanged()) {
        for (const std::string& problem :
             console.applyConfig(std::string(ct::kMovementCVarPrefix), *layers)) {
            ct::logWarning("{}", problem);
        }
    }
    return ct::movementSettingsFromCVars(console);
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
    const std::optional<ct::Aabb> bounds =
        ct::itemBounds(document.level(), document.selection(), settings.playerHalfExtents);
    if (!bounds) {
        return std::nullopt;
    }
    return (bounds->min + bounds->max) * 0.5f;
}

// Tool keys work without modifiers only, so Ctrl+E and the like stay free.
std::optional<ct::EditorTool> toolShortcut() {
    if (ImGui::GetIO().KeyMods != ImGuiMod_None) {
        return std::nullopt;
    }
    constexpr std::pair<ImGuiKey, ct::EditorTool> kKeys[]{
        {ImGuiKey_Q, ct::EditorTool::Select}, {ImGuiKey_W, ct::EditorTool::Move},
        {ImGuiKey_E, ct::EditorTool::Rotate}, {ImGuiKey_R, ct::EditorTool::Scale},
        {ImGuiKey_V, ct::EditorTool::Vertex}, {ImGuiKey_C, ct::EditorTool::Clip},
        {ImGuiKey_B, ct::EditorTool::Draw}};
    for (const auto& [key, tool] : kKeys) {
        if (ImGui::IsKeyPressed(key, false)) {
            return tool;
        }
    }
    return std::nullopt;
}

// Arrow keys nudge along the grid axes of the active pane: up moves away from the camera in 3D and
// up the screen in an orthographic pane. Page Up and Page Down nudge vertically. Held keys repeat.
std::optional<ct::Vec3> nudgeShortcut(const std::pair<ct::Vec3, ct::Vec3>& axes) {
    if (ImGui::GetIO().KeyMods != ImGuiMod_None) {
        return std::nullopt;
    }
    const auto& [forward, right] = axes;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        return forward;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        return -forward;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        return right;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        return -right;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
        return ct::Vec3{0.0f, 0.0f, 1.0f};
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
        return ct::Vec3{0.0f, 0.0f, -1.0f};
    }
    return std::nullopt;
}

// What play-in-editor shows over the game: how to stop, and how the game is doing.
void drawPlayBanner(const ct::PlaySession& play, bool captured, const ct::EditorSettings& settings,
                    const ct::EditorDocument& document, const ct::ViewRect& view) {
    const float padding = ImGui::GetStyle().WindowPadding.x;
    ImGui::SetNextWindowPos({view.x + padding, view.y + padding});
    ImGui::SetNextWindowBgAlpha(0.7f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("Playing", nullptr, kFlags)) {
        ImGui::Text("Playing %s   Esc stops", document.path().filename().string().c_str());
        ImGui::TextDisabled("%.1f m/s   server tick %.2f ms", static_cast<double>(play.speed()),
                            static_cast<double>(play.game().serverTickMilliseconds()));
        const ct::LinkConditions& c = settings.playConditions;
        if (!(c == ct::LinkConditions{})) {
            ImGui::TextDisabled("network each way: %.0f ms + up to %.0f ms, %.1f%% lost",
                                static_cast<double>(c.latencyMilliseconds),
                                static_cast<double>(c.jitterMilliseconds),
                                static_cast<double>(c.lossPercent));
        }
        if (!captured) {
            ImGui::Text("Click to take control");
        }
    }
    ImGui::End();
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

    const ct::MovementSettings movement = loadMovementSettings(content);
    ct::EditorSettings settings;
    settings.playerHalfExtents = movement.halfExtents;
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

    // Play-in-editor. While it runs the view shows the game, and the panels and shortcuts are off
    // so the level can't change under it.
    std::unique_ptr<ct::PlaySession> play;
    bool playCaptured = false;
    const auto capturePlayMouse = [&](bool capture) {
        playCaptured = capture;
        SDL_SetWindowRelativeMouseMode(window, capture);
        devUi->setMouseCaptured(capture);
    };
    const auto startPlay = [&] {
        viewport.cancelDrag(document);
        if (flying) {
            setFlying(false);
        }
        std::string playError;
        play =
            ct::PlaySession::start(document.level(), movement, settings.playConditions, playError);
        if (!play) {
            ct::logError("Can't play: {}", playError);
            return;
        }
        ct::logInfo("Playing {}; Esc stops", document.path().filename().string());
        capturePlayMouse(true);
    };

    while (running) {
        ct::FlyInput flyInput;
        bool stopPlay = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            devUi->processEvent(event);
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quitRequested = true;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (play && playCaptured) {
                    play->look(event.motion.xrel, event.motion.yrel);
                } else if (flying) {
                    flyInput.lookX += event.motion.xrel;
                    flyInput.lookY += event.motion.yrel;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (play && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
                    stopPlay = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (play && !playCaptured) {
                    capturePlayMouse(true);
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
                if (play && playCaptured) {
                    capturePlayMouse(false);
                }
                break;
            default:
                break;
            }
        }
        if (stopPlay || (play && quitRequested)) {
            play.reset();
            capturePlayMouse(false);
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
        if (play) {
            play->update(playCaptured ? SDL_GetKeyboardState(nullptr) : nullptr, now);
        }
        const bool playing = play != nullptr;

        devUi->beginFrame();
        ImGui::BeginDisabled(playing);
        ct::MenuRequests menu = panels.drawMenuBar(document, settings, viewport);
        ImGui::EndDisabled();
        const ImGuiID dockspace =
            ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        if (!layoutBuilt) {
            buildDefaultLayout(dockspace);
            layoutBuilt = true;
        }
        ImGui::BeginDisabled(playing);
        panels.drawHierarchy(document);
        panels.drawInspector(document, settings);
        ImGui::EndDisabled();
        consoleWindow.draw();

        ct::ViewRect view;
        if (const ImGuiDockNode* centre = ImGui::DockBuilderGetCentralNode(dockspace)) {
            view = {centre->Pos.x, centre->Pos.y, centre->Size.x, centre->Size.y};
        }
        if (playing) {
            drawPlayBanner(*play, playCaptured, settings, document, view);
        } else {
            menu.play = panels.drawToolbar(viewport, settings, view) || menu.play;
            viewport.drawPaneControls(view);
        }
        const ImGuiIO& io = ImGui::GetIO();
        const bool mouseOverView = !flying && !playing && !io.WantCaptureMouse &&
                                   view.contains(io.MousePos.x, io.MousePos.y);
        // Clicking the view takes keyboard focus from the panels, so shortcuts reach the view and
        // releasing Alt after an Alt-drag doesn't move ImGui's focus to the menu bar.
        if (mouseOverView && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                              ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            ImGui::SetWindowFocus(nullptr);
        }
        if (mouseOverView && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            setFlying(true);
        }
        if (!flying && !playing) {
            viewport.update(document, settings, view, mouseOverView);
        }
        if (!playing) {
            viewport.drawOverlay(document, settings, view);
        }

        // Shortcuts, unless a text field has the keyboard.
        bool frameSelection = menu.frameSelection;
        bool startPlaying = menu.play;
        // Not on the frame play stops either: its Esc is not for the editor.
        if (!flying && !playing && !stopPlay && !io.WantTextInput) {
            const float grid = settings.grid();
            const bool editingCorners =
                viewport.tool() == ct::EditorTool::Vertex && !viewport.selectedCorners().empty();
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
            } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) {
                ct::selectAll(document);
            } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_H)) {
                ct::hollowSelection(document, grid);
            } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_J)) {
                ct::mergeSelection(document);
            } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K)) {
                ct::subtractSelection(document);
            } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !viewport.isDragging()) {
                if (editingCorners) {
                    if (ct::removeCorners(document, viewport.selectedCorners())) {
                        viewport.setSelectedCorners({});
                    }
                } else {
                    ct::deleteSelection(document);
                }
            } else if (const auto tool = toolShortcut()) {
                viewport.setTool(*tool);
            } else if (const auto nudge = nudgeShortcut(viewport.nudgeAxes());
                       nudge && !viewport.isDragging()) {
                const ct::Vec3 offset = *nudge * grid;
                if (!editingCorners) {
                    ct::transformSelection(document, ct::AffineTransform::moveBy(offset), "Move");
                } else if (ct::moveCorners(document, viewport.selectedCorners(), offset)) {
                    std::vector<ct::Vec3> moved = viewport.selectedCorners();
                    for (ct::Vec3& corner : moved) {
                        corner += offset;
                    }
                    viewport.setSelectedCorners(std::move(moved));
                }
            } else if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
                startPlaying = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                frameSelection = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
                settings.gridIndex = settings.gridIndex > 0 ? settings.gridIndex - 1 : 0;
            } else if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
                settings.gridIndex = std::min(settings.gridIndex + 1, ct::kGridSizes.size() - 1);
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                if (viewport.isDragging()) {
                    viewport.cancelDrag(document);
                } else if (!viewport.clearToolState()) {
                    if (viewport.tool() != ct::EditorTool::Select) {
                        viewport.setTool(ct::EditorTool::Select);
                    } else {
                        document.clearSelection();
                    }
                }
            }
        }
        if (menu.save) {
            save(document);
        }
        if (startPlaying && !playing) {
            startPlay();
        }
        if (menu.addPreset != nullptr) {
            ct::addPresetEntity(document, *menu.addPreset,
                                viewport.placementPoint(document, settings));
        }
        if (menu.addEntity != nullptr) {
            ct::LevelEntity entity = ct::makeEntity(*menu.addEntity);
            ct::setProperty(entity, "origin",
                            ct::formatVec3(viewport.placementPoint(document, settings)));
            ct::addEntity(document, std::move(entity));
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
        const float grid = settings.grid();
        renderer->setWorldGrid(grid, grid < 1.0f ? 1.0f : grid * 8.0f);
        if (playing) {
            const ImVec2 scale = io.DisplayFramebufferScale;
            const ct::PixelRect rect{view.x * scale.x, view.y * scale.y, view.width * scale.x,
                                     view.height * scale.y};
            renderer->drawFrame(play->camera(), ui, &rect);
        } else {
            std::vector<ct::WorldView> worldViews;
            for (const ct::LevelViewport::PaneDraw& pane : viewport.paneDraws(view)) {
                const ImVec2 scale = io.DisplayFramebufferScale;
                ct::WorldView worldView;
                worldView.viewProjection = pane.viewProjection;
                worldView.rect = {pane.rect.x * scale.x, pane.rect.y * scale.y,
                                  pane.rect.width * scale.x, pane.rect.height * scale.y};
                // Orthographic panes are wireframe plans (drawn by the viewport) on a dark
                // background: shaded, the walls around a room would hide everything in it.
                worldView.clearBackground = pane.orthographic;
                worldView.background = kOrthoBackground;
                worldView.drawWorld = !pane.orthographic;
                worldViews.push_back(worldView);
            }
            renderer->drawFrame(worldViews, ui);
        }

        ++frameCount;
        if (commandLine.frameLimit >= 0 && frameCount >= commandLine.frameLimit) {
            running = false;
        }
    }

    play.reset();
    renderer.reset(); // before the ImGui context it serves
    devUi.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
