// CrofTech client: a window onto a game running in this process.
//
// A local server runs the simulation on its own thread at 128 Hz, and this thread is its client:
// every frame it sends the latest input as a packet and draws the player's view between the two
// newest snapshots the server sent back. Everything between them goes through the bytes a network
// would carry, over an in-memory link that the net.fake_* settings can slow down and make lossy.
// The camera angles come straight from the mouse, so looking around never waits for a tick.
//
// Settings are console variables: config files set them (and reload when saved), and so does the
// developer console, opened with the key below Esc.
//
// Levels are found by name through the content layers (levels/<name>.level, then a TrenchBroom
// levels/<name>.map), or by path. The game's start_map loads first; the map command switches.

#include "PlayerInput.h"
#include "WorldMesh.h"

#include "croftech/client/LocalGame.h"
#include "croftech/core/Angle.h"
#include "croftech/core/Console.h"
#include "croftech/core/Content.h"
#include "croftech/core/Log.h"
#include "croftech/core/Profile.h"
#include "croftech/devui/ConsoleWindow.h"
#include "croftech/devui/DevUi.h"
#include "croftech/devui/EntityInspector.h"
#include "croftech/devui/StatsOverlay.h"
#include "croftech/level/LevelLoad.h"
#include "croftech/render/Renderer.h"
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Development builds run straight from the source tree, so config edits hot-reload in place.
const std::filesystem::path kSourceDirectory = CROFTECH_SOURCE_DIR;
constexpr auto kReloadInterval = std::chrono::milliseconds(250);

struct CommandLine {
    std::string game = "sandbox";
    std::string map;                  // level name or path; empty means the game's start_map
    int frameLimit = -1;              // quit after this many frames, for automated smoke runs
    std::vector<std::string> execute; // console commands run after the config files load
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
    const ct::CVar& showEntities;
    const ct::CVar& fakeLatency;
    const ct::CVar& fakeJitter;
    const ct::CVar& fakeLoss;

    ct::LinkConditions linkConditions() const {
        return {fakeLatency.asFloat(), fakeJitter.asFloat(), fakeLoss.asFloat()};
    }
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
        console.add(ct::CVar::makeBool("client.show_entities", false,
                                       "window listing every entity and its components")),
        console.add(ct::CVar::makeFloat("net.fake_latency_ms", 0.0f, 0.0f, 1000.0f,
                                        "delay added to every packet, each way")),
        console.add(ct::CVar::makeFloat("net.fake_jitter_ms", 0.0f, 0.0f, 500.0f,
                                        "random extra delay of up to this, each way")),
        console.add(ct::CVar::makeFloat("net.fake_loss_percent", 0.0f, 0.0f, 100.0f,
                                        "share of packets lost, each way")),
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

std::vector<ct::EntityInspector::Entity> inspectEntities(const ct::World& world) {
    std::vector<ct::EntityInspector::Entity> entities;
    const auto generations = world.entities.generations();
    for (std::uint32_t index = 0; index < generations.size(); ++index) {
        const ct::EntityId id{index, generations[index]};
        if (!world.entities.isAlive(id)) {
            continue;
        }
        ct::EntityInspector::Entity& entity = entities.emplace_back();
        entity.index = id.index;
        entity.generation = id.generation;
        ct::forEachComponent(
            world, id, [&entity](const ct::TypeSchema& schema, std::span<const std::byte> bytes) {
                entity.components.push_back({&schema, bytes});
            });
    }
    return entities;
}

// A level read, checked and ready to play.
struct PreparedLevel {
    std::string name;
    ct::Level level;
    std::vector<ct::SpawnPoint> spawns;
};

std::optional<PreparedLevel> prepareLevel(const ct::ContentPaths& content, const std::string& name,
                                          std::string& error) {
    const std::optional<std::filesystem::path> path = ct::findLevelFile(content, name, error);
    if (!path) {
        return std::nullopt;
    }
    std::vector<std::string> warnings;
    std::optional<ct::Level> level = ct::loadAnyLevelFile(*path, warnings, error);
    for (const std::string& warning : warnings) {
        ct::logWarning("{}: {}", path->filename().string(), warning);
    }
    if (!level) {
        return std::nullopt;
    }
    std::optional<std::vector<ct::SpawnPoint>> spawns = ct::readSpawnPoints(*level, error);
    if (!spawns) {
        return std::nullopt;
    }
    if (spawns->empty()) {
        error = std::format("level '{}' has no {}", name, ct::kPlayerSpawnClass);
        return std::nullopt;
    }
    return PreparedLevel{name, std::move(*level), std::move(*spawns)};
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

    // Levels switch between frames, so the map command only prepares the next one.
    std::optional<PreparedLevel> pendingLevel;
    console.addCommand("map", "Load a level by name or path: map <name>",
                       [&](std::span<const std::string> args) {
                           if (args.size() != 1) {
                               ct::logWarning("usage: map <name>");
                               return;
                           }
                           std::string mapError;
                           pendingLevel = prepareLevel(content, args[0], mapError);
                           if (!pendingLevel) {
                               ct::logError("map {}: {}", args[0], mapError);
                           }
                       });
    for (const std::string& commands : commandLine.execute) {
        console.execute(commands);
    }
    if (!pendingLevel) {
        const std::string& name = commandLine.map.empty() ? game->startMap : commandLine.map;
        if (name.empty()) {
            ct::logError("no level to load: pass --map <name> or set start_map in game.cfg");
            return EXIT_FAILURE;
        }
        pendingLevel = prepareLevel(content, name, error);
        if (!pendingLevel) {
            ct::logError("map {}: {}", name, error);
            return EXIT_FAILURE;
        }
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
    ct::EntityInspector entityInspector;
    // The inspector reads a copy, so the simulation thread is never held up by drawing.
    const auto inspectedState = std::make_unique<ct::State>();

    ct::MovementSettings movement = ct::movementSettingsFromCVars(console);
    std::uint64_t seenChanges = console.changeCount();

    std::unique_ptr<ct::LocalGame> localGame;
    ct::PlayerLook look;
    std::vector<std::uint32_t> moverParts; // the level entity of each moving part of the mesh
    std::vector<ct::Vec3> moverOffsets;
    const auto enterLevel = [&](const PreparedLevel& prepared) {
        localGame.reset();
        const ct::WorldMesh mesh = ct::buildWorldMesh(prepared.level, ct::MeshPurpose::Playing);
        renderer->setWorldMesh(mesh.vertices, mesh.indices, mesh.parts);
        moverParts = mesh.partEntities;
        moverOffsets.assign(moverParts.size(), {});
        localGame =
            std::make_unique<ct::LocalGame>(prepared.level, movement, client.linkConditions());
        for (const std::string& warning : localGame->level().warnings()) {
            ct::logWarning("{}: {}", prepared.name, warning);
        }
        look = {ct::angleToRadians(prepared.spawns.front().yaw), 0.0f};
        ct::logInfo("Level: {} ({} brushes, {} entities)", prepared.name,
                    prepared.level.brushes.size(), prepared.level.entities.size());
    };

    bool running = true;
    console.addCommand("quit", "Exit the game",
                       [&running](std::span<const std::string>) { running = false; });

    bool mouseCaptured = false;
    const auto captureMouse = [&](bool capture) {
        mouseCaptured = capture;
        SDL_SetWindowRelativeMouseMode(window, capture);
        devUi->setMouseCaptured(capture);
    };
    captureMouse(true);

    ct::Vec3 eye; // kept until the server has let the player in
    Clock::time_point nextReload = Clock::now() + kReloadInterval;
    Clock::time_point lastFrame = Clock::now();
    int frameCount = 0;
    float horizontalSpeed = 0.0f;

    while (running) {
        CT_PROFILE_FRAME();

        if (pendingLevel) {
            enterLevel(*pendingLevel);
            pendingLevel.reset();
        }

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
                    look.turn(event.motion.xrel, event.motion.yrel, client.sensitivity.asFloat());
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
        const ct::UserCmd cmd =
            ct::makeUserCmd(look, mouseCaptured ? SDL_GetKeyboardState(nullptr) : nullptr);
        const Clock::time_point now = Clock::now();
        localGame->update(cmd, now);

        if (now >= nextReload) {
            nextReload = now + kReloadInterval;
            for (ConfigFile& file : configFiles) {
                reloadConfig(console, file, true);
            }
        }
        if (console.changeCount() != seenChanges) {
            seenChanges = console.changeCount();
            movement = ct::movementSettingsFromCVars(console);
            localGame->setMovementSettings(movement);
            localGame->setConditions(client.linkConditions());
        }

        if (const std::optional<ct::Vec3> playerEye = localGame->eyePosition(now)) {
            eye = *playerEye;
        }
        for (std::size_t i = 0; i < moverParts.size(); ++i) {
            moverOffsets[i] = localGame->moverOffset(moverParts[i], now);
        }
        renderer->setWorldPartOffsets(moverOffsets);
        if (const ct::State* latest = localGame->latestState()) {
            if (const ct::PlayerMotion* motion =
                    latest->world.playerMotions.get(localGame->player())) {
                horizontalSpeed = std::sqrt(motion->velocity.x * motion->velocity.x +
                                            motion->velocity.y * motion->velocity.y);
            }
            if (client.showEntities.asBool()) {
                *inspectedState = *latest;
            }
        }

        stats.add({std::chrono::duration<float, std::milli>(now - lastFrame).count(),
                   localGame->serverTickMilliseconds(), horizontalSpeed});
        lastFrame = now;

        devUi->beginFrame();
        if (client.showStats.asBool()) {
            stats.draw();
        }
        if (client.showEntities.asBool()) {
            bool open = true;
            entityInspector.draw(inspectEntities(inspectedState->world), &open);
            if (!open) {
                console.execute("client.show_entities 0");
            }
        }
        consoleWindow.draw();
        const ImDrawData* ui = devUi->endFrame();

        ct::Camera camera;
        camera.position = eye;
        camera.yawRadians = look.yaw;
        camera.pitchRadians = look.pitch;
        camera.horizontalFovRadians16x9 = client.fov.asFloat() * (ct::kPi / 180.0f);
        renderer->drawFrame(camera, ui);

        ++frameCount;
        if (commandLine.frameLimit >= 0 && frameCount >= commandLine.frameLimit) {
            running = false;
        }
    }

    console.removeCommand("quit");
    console.removeCommand("map");
    localGame.reset();
    renderer.reset(); // before the ImGui context it serves
    devUi.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
