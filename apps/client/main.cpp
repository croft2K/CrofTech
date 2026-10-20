// CrofTech client: a window onto a locally running simulation.
//
// The simulation runs on its own thread at 128 Hz. Every frame this thread sends the latest input,
// then draws the player's view interpolated between the two most recent ticks. The camera angles
// come straight from the mouse, so looking around never waits for a tick.
//
// Settings are console variables: config files set them (and reload when saved), and so does the
// developer console, opened with the key below Esc.

#include "WorldMesh.h"

#include "croftech/client/SimThread.h"
#include "croftech/core/Angle.h"
#include "croftech/core/Console.h"
#include "croftech/core/Content.h"
#include "croftech/core/Log.h"
#include "croftech/core/Profile.h"
#include "croftech/devui/ConsoleWindow.h"
#include "croftech/devui/DevUi.h"
#include "croftech/devui/StatsOverlay.h"
#include "croftech/level/TestLevels.h"
#include "croftech/render/Renderer.h"
#include "croftech/sim/CollisionWorld.h"
#include "croftech/sim/MovementConfig.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Development builds run straight from the source tree, so config edits hot-reload in place.
const std::filesystem::path kSourceDirectory = CROFTECH_SOURCE_DIR;
constexpr float kDegreesPerMouseCount = 0.022f; // Quake's m_yaw, so sensitivities carry over
constexpr auto kReloadInterval = std::chrono::milliseconds(250);

struct CommandLine {
    std::string game = "sandbox";
    int frameLimit = -1;              // quit after this many frames, for automated smoke runs
    std::vector<std::string> execute; // console commands run after the config files load
};

CommandLine parseCommandLine(int argc, char** argv) {
    CommandLine commandLine;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--game") {
            commandLine.game = argv[++i];
        } else if (option == "--frames") {
            commandLine.frameLimit = std::atoi(argv[++i]);
        } else if (option == "--exec") {
            commandLine.execute.emplace_back(argv[++i]);
        }
    }
    return commandLine;
}

struct ClientCVars {
    const ct::CVar& sensitivity;
    const ct::CVar& fov;
    const ct::CVar& showStats;
};

ClientCVars registerClientCVars(ct::Console& console) {
    return {
        console.add(ct::CVar::makeFloat("client.sensitivity", 3.0f, 0.01f, 100.0f,
                                        "degrees turned per mouse count = 0.022 x sensitivity")),
        console.add(ct::CVar::makeFloat(
            "client.fov", 100.0f, 60.0f, 140.0f,
            "horizontal field of view in degrees at 16:9; wider screens see more")),
        console.add(ct::CVar::makeBool("client.show_stats", true,
                                       "frame rate, tick cost and speed overlay")),
    };
}

// A config file whose settings are the console variables under one prefix.
struct ConfigFile {
    ct::LayeredConfig layers;
    std::string prefix;
    std::string name;
};

void reloadConfig(ct::Console& console, ConfigFile& file, bool announce) {
    const auto layers = file.layers.reloadIfChanged();
    if (!layers) {
        return;
    }
    for (const std::string& problem : console.applyConfig(file.prefix, *layers)) {
        ct::logWarning("{}", problem);
    }
    if (announce) {
        ct::logInfo("{} reloaded", file.name);
    }
}

ct::Vec3 lerp(ct::Vec3 a, ct::Vec3 b, float t) { return a + (b - a) * t; }

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
    ct::logInfo("Game: {}", game->title);

    ct::Console console;
    ct::registerMovementCVars(console);
    const ClientCVars client = registerClientCVars(console);
    std::vector<ConfigFile> configFiles;
    configFiles.push_back({ct::LayeredConfig(content, "config/movement.cfg"),
                           std::string(ct::kMovementCVarPrefix), "movement.cfg"});
    configFiles.push_back(
        {ct::LayeredConfig(content, "config/client.cfg"), "client.", "client.cfg"});
    for (ConfigFile& file : configFiles) {
        reloadConfig(console, file, false);
    }
    for (const std::string& commands : commandLine.execute) {
        console.execute(commands);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ct::logError("SDL: {}", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_Window* window =
        SDL_CreateWindow(game->title.c_str(), 1600, 900,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
    ct::ConsoleWindow consoleWindow(console);
    ct::StatsOverlay stats;

    const ct::Level level = ct::makeMovementTestLevel();
    const ct::CollisionWorld collision(level.brushes);
    const ct::WorldMesh mesh = ct::buildWorldMesh(level);
    renderer->setWorldMesh(mesh.vertices, mesh.indices);

    ct::MovementSettings movement = ct::movementSettingsFromCVars(console);
    std::uint64_t seenChanges = console.changeCount();
    const ct::SpawnPoint& spawn = level.spawnPoints.front();
    auto initial = std::make_unique<ct::State>();
    const ct::EntityId player = ct::spawnPlayer(initial->world, movement, spawn, 0);
    ct::SimThread sim(collision, movement, *initial);
    initial.reset();
    sim.start();

    bool running = true;
    console.addCommand("quit", "Exit the game",
                       [&running](std::span<const std::string>) { running = false; });

    float yaw = ct::angleToRadians(spawn.yaw);
    float pitch = 0.0f;
    const float maxPitch = ct::angleToRadians(ct::kMaxPitch);
    bool mouseCaptured = false;
    const auto captureMouse = [&](bool capture) {
        mouseCaptured = capture;
        SDL_SetWindowRelativeMouseMode(window, capture);
        devUi->setMouseCaptured(capture);
    };
    captureMouse(true);

    Clock::time_point nextReload = Clock::now() + kReloadInterval;
    Clock::time_point lastFrame = Clock::now();
    int frameCount = 0;
    float horizontalSpeed = 0.0f;

    while (running) {
        CT_PROFILE_FRAME();

        SDL_Event event;
        bool consoleKeyPressed = false;
        while (SDL_PollEvent(&event)) {
            // The console key only opens and closes the console; it never types into it.
            if (event.type == SDL_EVENT_TEXT_INPUT && consoleKeyPressed) {
                continue;
            }
            devUi->processEvent(event);
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (mouseCaptured) {
                    const float radiansPerCount =
                        kDegreesPerMouseCount * client.sensitivity.asFloat() * (ct::kPi / 180.0f);
                    yaw -= event.motion.xrel * radiansPerCount;
                    pitch -= event.motion.yrel * radiansPerCount;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.repeat) {
                    break;
                }
                if (event.key.scancode == SDL_SCANCODE_GRAVE) {
                    consoleKeyPressed = true;
                    consoleWindow.setOpen(!consoleWindow.isOpen());
                    captureMouse(!consoleWindow.isOpen());
                } else if (event.key.key == SDLK_ESCAPE) {
                    if (consoleWindow.isOpen()) {
                        consoleWindow.setOpen(false);
                        captureMouse(true);
                    } else {
                        captureMouse(!mouseCaptured);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!mouseCaptured && !consoleWindow.isOpen()) {
                    captureMouse(true);
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                captureMouse(false);
                break;
            default:
                break;
            }
        }
        yaw = std::remainder(yaw, 2.0f * ct::kPi);
        pitch = std::clamp(pitch, -maxPitch, maxPitch);

        ct::UserCmd cmd;
        cmd.yaw = static_cast<std::uint16_t>(ct::radiansToAngle(yaw));
        cmd.pitch = static_cast<std::int16_t>(ct::radiansToAngle(pitch));
        if (mouseCaptured) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const auto axis = [&](SDL_Scancode positive, SDL_Scancode negative) {
                return static_cast<std::int8_t>((keys[positive] ? 127 : 0) -
                                                (keys[negative] ? 127 : 0));
            };
            cmd.forward = axis(SDL_SCANCODE_W, SDL_SCANCODE_S);
            cmd.side = axis(SDL_SCANCODE_D, SDL_SCANCODE_A);
            cmd.buttons = keys[SDL_SCANCODE_SPACE] ? ct::kButtonJump : 0;
        }
        sim.submitInput(0, cmd);

        const Clock::time_point now = Clock::now();
        if (now >= nextReload) {
            nextReload = now + kReloadInterval;
            for (ConfigFile& file : configFiles) {
                reloadConfig(console, file, true);
            }
        }
        if (console.changeCount() != seenChanges) {
            seenChanges = console.changeCount();
            movement = ct::movementSettingsFromCVars(console);
            sim.setMovementSettings(movement);
        }

        ct::Vec3 eye;
        sim.readStates([&](const ct::State& previous, const ct::State& current,
                           Clock::time_point publishedAt) {
            const ct::Transform* from = previous.world.transforms.get(player);
            const ct::Transform* to = current.world.transforms.get(player);
            if (from == nullptr || to == nullptr) {
                return;
            }
            const float alpha = std::clamp(std::chrono::duration<float>(now - publishedAt).count() /
                                               ct::kTickSeconds,
                                           0.0f, 1.0f);
            eye = lerp(ct::eyePosition(movement, *from), ct::eyePosition(movement, *to), alpha);
            if (const ct::PlayerMotion* motion = current.world.playerMotions.get(player)) {
                horizontalSpeed = std::sqrt(motion->velocity.x * motion->velocity.x +
                                            motion->velocity.y * motion->velocity.y);
            }
        });

        stats.add({std::chrono::duration<float, std::milli>(now - lastFrame).count(),
                   sim.lastStepMilliseconds(), horizontalSpeed});
        lastFrame = now;

        devUi->beginFrame();
        if (client.showStats.asBool()) {
            stats.draw();
        }
        consoleWindow.draw();
        const ImDrawData* ui = devUi->endFrame();

        ct::Camera camera;
        camera.position = eye;
        camera.yawRadians = yaw;
        camera.pitchRadians = pitch;
        camera.horizontalFovRadians16x9 = client.fov.asFloat() * (ct::kPi / 180.0f);
        renderer->drawFrame(camera, ui);

        ++frameCount;
        if (commandLine.frameLimit >= 0 && frameCount >= commandLine.frameLimit) {
            running = false;
        }
    }

    console.removeCommand("quit");
    sim.stop();
    renderer.reset(); // before the ImGui context it serves
    devUi.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
