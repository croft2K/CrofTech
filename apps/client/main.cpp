// CrofTech client: a window onto a locally running simulation.
//
// The simulation runs on its own thread at 128 Hz. Every frame this thread sends the latest input,
// then draws the player's view interpolated between the two most recent ticks. The camera angles
// come straight from the mouse, so looking around never waits for a tick.

#include "WorldMesh.h"

#include "croftech/client/SimThread.h"
#include "croftech/core/Angle.h"
#include "croftech/core/ConfigFile.h"
#include "croftech/core/Content.h"
#include "croftech/core/Log.h"
#include "croftech/core/Profile.h"
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
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

using Clock = std::chrono::steady_clock;

// Development builds run straight from the source tree, so config edits hot-reload in place.
const std::filesystem::path kSourceDirectory = CROFTECH_SOURCE_DIR;
constexpr float kDegreesPerMouseCount = 0.022f; // Quake's m_yaw, so sensitivities carry over
constexpr auto kReloadInterval = std::chrono::milliseconds(250);
constexpr auto kTitleInterval = std::chrono::milliseconds(250);

struct ClientSettings {
    float sensitivity = 3.0f;
    float fovDegrees = 100.0f;
};

struct CommandLine {
    std::string game = "sandbox";
    int frameLimit = -1; // quit after this many frames, for automated smoke runs
};

CommandLine parseCommandLine(int argc, char** argv) {
    CommandLine commandLine;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--game") {
            commandLine.game = argv[++i];
        } else if (option == "--frames") {
            commandLine.frameLimit = std::atoi(argv[++i]);
        }
    }
    return commandLine;
}

// Every layer is applied over the built-in defaults, so deleting a line undoes it.
std::optional<ct::MovementSettings> reloadMovement(ct::LayeredConfig& config) {
    const auto layers = config.reloadIfChanged();
    if (!layers) {
        return std::nullopt;
    }
    ct::MovementSettings settings;
    for (const ct::LayeredConfig::Layer& layer : *layers) {
        for (const std::string& problem : ct::applyMovementConfig(layer.entries, settings)) {
            ct::logWarning("{}: {}", layer.path.string(), problem);
        }
    }
    return settings;
}

std::optional<ClientSettings> reloadClient(ct::LayeredConfig& config) {
    const auto layers = config.reloadIfChanged();
    if (!layers) {
        return std::nullopt;
    }
    ClientSettings settings;
    for (const ct::LayeredConfig::Layer& layer : *layers) {
        for (const ct::ConfigEntry& entry : layer.entries) {
            float value = 0.0f;
            const bool parsed = ct::parseFloat(entry.value, value);
            if (entry.key == "sensitivity" && parsed && value > 0.0f) {
                settings.sensitivity = value;
            } else if (entry.key == "fov" && parsed && value >= 60.0f && value <= 140.0f) {
                settings.fovDegrees = value;
            } else {
                ct::logWarning("{}: line {}: unknown setting or invalid value '{} {}'",
                               layer.path.string(), entry.line, entry.key, entry.value);
            }
        }
    }
    return settings;
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

    std::unique_ptr<ct::Renderer> renderer =
        ct::Renderer::create(window, std::filesystem::path(SDL_GetBasePath()) / "shaders", error);
    if (!renderer) {
        ct::logError("renderer: {}", error);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    const ct::Level level = ct::makeMovementTestLevel();
    const ct::CollisionWorld collision(level.brushes);
    const ct::WorldMesh mesh = ct::buildWorldMesh(level);
    renderer->setWorldMesh(mesh.vertices, mesh.indices);

    ct::LayeredConfig movementFile(content, "config/movement.cfg");
    ct::LayeredConfig clientFile(content, "config/client.cfg");
    ct::MovementSettings movement = reloadMovement(movementFile).value_or(ct::MovementSettings{});
    ClientSettings client = reloadClient(clientFile).value_or(ClientSettings{});

    const ct::SpawnPoint& spawn = level.spawnPoints.front();
    auto initial = std::make_unique<ct::State>();
    const ct::EntityId player = ct::spawnPlayer(initial->world, movement, spawn, 0);
    ct::SimThread sim(collision, movement, *initial);
    initial.reset();
    sim.start();

    float yaw = ct::angleToRadians(spawn.yaw);
    float pitch = 0.0f;
    const float maxPitch = ct::angleToRadians(ct::kMaxPitch);
    bool mouseCaptured = true;
    SDL_SetWindowRelativeMouseMode(window, true);

    Clock::time_point nextReload = Clock::now() + kReloadInterval;
    Clock::time_point nextTitle = Clock::now() + kTitleInterval;
    int framesSinceTitle = 0;
    int frameCount = 0;
    float horizontalSpeed = 0.0f;

    bool running = true;
    while (running) {
        CT_PROFILE_FRAME();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (mouseCaptured) {
                    const float radiansPerCount =
                        kDegreesPerMouseCount * client.sensitivity * (ct::kPi / 180.0f);
                    yaw -= event.motion.xrel * radiansPerCount;
                    pitch -= event.motion.yrel * radiansPerCount;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE && !event.key.repeat) {
                    mouseCaptured = !mouseCaptured;
                    SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!mouseCaptured) {
                    mouseCaptured = true;
                    SDL_SetWindowRelativeMouseMode(window, true);
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                mouseCaptured = false;
                SDL_SetWindowRelativeMouseMode(window, false);
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
            if (std::optional<ct::MovementSettings> reloaded = reloadMovement(movementFile)) {
                movement = *reloaded;
                sim.setMovementSettings(movement);
                ct::logInfo("movement.cfg reloaded");
            }
            if (std::optional<ClientSettings> reloaded = reloadClient(clientFile)) {
                client = *reloaded;
                ct::logInfo("client.cfg reloaded");
            }
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

        ct::Camera camera;
        camera.position = eye;
        camera.yawRadians = yaw;
        camera.pitchRadians = pitch;
        camera.horizontalFovRadians16x9 = client.fovDegrees * (ct::kPi / 180.0f);
        renderer->drawFrame(camera);

        ++frameCount;
        ++framesSinceTitle;
        if (now >= nextTitle) {
            const float seconds =
                std::chrono::duration<float>(now - (nextTitle - kTitleInterval)).count();
            const std::string title =
                std::format("{}  |  {:.0f} fps  |  {:.2f} m/s  |  tick {:.3f} ms", game->title,
                            static_cast<float>(framesSinceTitle) / seconds, horizontalSpeed,
                            sim.lastStepMilliseconds());
            SDL_SetWindowTitle(window, title.c_str());
            nextTitle = now + kTitleInterval;
            framesSinceTitle = 0;
        }
        if (commandLine.frameLimit >= 0 && frameCount >= commandLine.frameLimit) {
            running = false;
        }
    }

    sim.stop();
    renderer.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
