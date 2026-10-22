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
//
// F5 saves and F9 loads the newest save (the save and load commands take names); reaching a
// checkpoint saves too. Saves go in the user's folder for the game, never in its content. After
// dying, fire respawns.

#include "AudioOutput.h"
#include "EntityShapes.h"
#include "GameEffects.h"
#include "GameHud.h"
#include "GameSounds.h"
#include "PlayerInput.h"
#include "SaveFiles.h"
#include "WorldMesh.h"

#include "croftech/client/GameEvents.h"
#include "croftech/client/LocalGame.h"
#include "croftech/client/StepSmoothing.h"
#include "croftech/core/Angle.h"
#include "croftech/core/Console.h"
#include "croftech/core/Content.h"
#include "croftech/core/Log.h"
#include "croftech/core/Profile.h"
#include "croftech/devui/ConsoleWindow.h"
#include "croftech/devui/DevUi.h"
#include "croftech/devui/EntityInspector.h"
#include "croftech/devui/StatsOverlay.h"
#include "croftech/level/LevelFile.h"
#include "croftech/level/LevelLoad.h"
#include "croftech/render/Renderer.h"
#include "croftech/sim/GameSettings.h"
#include "croftech/sim/MovementConfig.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
constexpr auto kNoticeTime = std::chrono::seconds(2);

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
    const ct::CVar& stepSmoothing;
    const ct::CVar& thirdPerson;
    const ct::CVar& volume;
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
        console.add(ct::CVar::makeFloat(
            "client.step_smoothing", 0.1f, 0.0f, 1.0f,
            "seconds the view takes to follow a step up or down stairs; 0 follows at once")),
        console.add(ct::CVar::makeBool("client.third_person", false,
                                       "1 = follow the player from behind, to see it move")),
        console.add(ct::CVar::makeFloat("client.volume", 0.8f, 0.0f, 1.0f,
                                        "loudness of every sound, 0 to 1")),
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

// A line of text in the middle of the screen near the top, such as "Saved".
void drawNotice(const std::string& text) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        {viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.15f},
        ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowBgAlpha(0.6f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("Notice", nullptr, kFlags)) {
        ImGui::TextUnformatted(text.c_str());
    }
    ImGui::End();
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
    ct::registerGameCVars(console);
    const ClientCVars client = registerClientCVars(console);
    std::vector<ConfigFile> configFiles;
    configFiles.push_back({ct::LayeredConfig(content, "config/movement.cfg"),
                           std::string(ct::kMovementCVarPrefix), "movement.cfg"});
    configFiles.push_back({ct::LayeredConfig(content, "config/rules.cfg"),
                           std::string(ct::kRulesCVarPrefix), "rules.cfg"});
    configFiles.push_back(
        {ct::LayeredConfig(content, "config/client.cfg"), "client.", "client.cfg"});
    for (ConfigFile& file : configFiles) {
        reloadConfig(console, file, false);
    }

    // Levels switch between frames, so the map command only prepares the next one, and a load
    // only prepares its level and keeps the state to restore once it has been entered.
    std::optional<PreparedLevel> pendingLevel;
    std::optional<std::vector<std::byte>> pendingState;
    const ct::SaveFiles saves(game->name);
    PreparedLevel* currentLevel = nullptr; // set once a level has been entered
    std::uint64_t currentLevelHash = 0;
    std::string notice;
    Clock::time_point noticeUntil;
    const auto tell = [&](std::string text) {
        ct::logInfo("{}", text);
        notice = std::move(text);
        noticeUntil = Clock::now() + kNoticeTime;
    };
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
    std::unique_ptr<ct::LocalGame> localGame;
    const auto writeSave = [&](const std::string& name, std::vector<std::byte> state) {
        std::string saveError;
        if (currentLevel == nullptr ||
            !saves.write(name, {currentLevel->name, currentLevelHash, std::move(state)},
                         saveError)) {
            ct::logError("save {}: {}", name, saveError);
            return false;
        }
        return true;
    };
    console.addCommand("save", "Save the game: save [name], quick by default",
                       [&](std::span<const std::string> args) {
                           const std::string name = args.empty() ? "quick" : args[0];
                           if (localGame && writeSave(name, localGame->save())) {
                               tell(name == "quick" ? "Saved" : "Saved " + name);
                           }
                       });
    console.addCommand("load", "Load a save: load [name], the newest by default",
                       [&](std::span<const std::string> args) {
                           std::string loadError;
                           const std::optional<std::string> name =
                               args.empty() ? saves.newest() : std::optional<std::string>(args[0]);
                           if (!name) {
                               tell("Nothing saved yet (F5 saves)");
                               return;
                           }
                           std::optional<ct::SaveGame> save = saves.read(*name, loadError);
                           if (!save) {
                               ct::logError("load {}: {}", *name, loadError);
                               return;
                           }
                           std::optional<PreparedLevel> level =
                               prepareLevel(content, save->level, loadError);
                           if (!level) {
                               ct::logError("load {}: {}", *name, loadError);
                               return;
                           }
                           if (ct::levelContentHash(level->level) != save->levelHash) {
                               ct::logError("load {}: level '{}' has changed since it was saved",
                                            *name, save->level);
                               return;
                           }
                           pendingLevel = std::move(level);
                           pendingState = std::move(save->state);
                           tell("Loaded " + *name);
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
    // Destroyed before SDL shuts down, sounds first.
    auto audio = std::make_unique<ct::AudioOutput>();
    auto sounds = std::make_unique<ct::GameSounds>(*audio, content);
    ct::GameEvents gameEvents;
    ct::GameEffects gameEffects;
    std::vector<ct::GameEvent> events; // this frame's
    ct::EntityInspector entityInspector;
    // The inspector reads a copy, so the simulation thread is never held up by drawing.
    const auto inspectedState = std::make_unique<ct::State>();

    ct::GameSettings settings = ct::gameSettingsFromCVars(console);
    std::uint64_t seenChanges = console.changeCount();

    ct::PlayerLook look;
    ct::GameHud hud;
    ct::StepSmoothing stepSmoothing;
    std::uint32_t seenTimeline = 0;
    std::optional<PreparedLevel> enteredLevel;
    std::vector<std::uint32_t> moverParts; // the level entity of each moving part of the mesh
    std::vector<ct::Vec3> moverOffsets;
    std::vector<ct::WorldVertex> shapes; // rebuilt every frame
    std::vector<ct::EffectVertex> effects;
    const auto enterLevel = [&](const PreparedLevel& prepared) {
        localGame.reset();
        sounds->stopAll();
        gameEvents.reset();
        gameEffects.clear();
        const ct::WorldMesh mesh = ct::buildWorldMesh(prepared.level, ct::MeshPurpose::Playing);
        renderer->setWorldMesh(mesh.vertices, mesh.indices, mesh.parts);
        moverParts = mesh.partEntities;
        moverOffsets.assign(moverParts.size(), {});
        localGame =
            std::make_unique<ct::LocalGame>(prepared.level, settings, client.linkConditions());
        hud = {};
        seenTimeline = 0;
        currentLevelHash = ct::levelContentHash(prepared.level);
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
            enteredLevel = std::move(pendingLevel);
            pendingLevel.reset();
            currentLevel = &*enteredLevel;
            enterLevel(*enteredLevel);
        }
        if (pendingState) {
            std::string loadError;
            if (!localGame->load(*pendingState, loadError)) {
                ct::logError("load: {}", loadError);
            }
            pendingState.reset();
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
                } else if (event.key.key == SDLK_F5) {
                    console.execute("save");
                } else if (event.key.key == SDLK_F9) {
                    console.execute("load");
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
            settings = ct::gameSettingsFromCVars(console);
            localGame->setSettings(settings);
            localGame->setConditions(client.linkConditions());
        }

        if (std::optional<std::vector<std::byte>> checkpoint = localGame->takeCheckpoint()) {
            if (writeSave("checkpoint", std::move(*checkpoint))) {
                tell("Checkpoint");
            }
        }
        // After a load or a respawn, look the way the player faces.
        const bool respawned =
            hud.update(localGame->latestState(), localGame->player(), settings.rules, now);
        if ((localGame->timeline() != seenTimeline || respawned) &&
            localGame->latestState() != nullptr) {
            seenTimeline = localGame->timeline();
            if (const ct::Transform* transform =
                    localGame->latestState()->world.transforms.get(localGame->player())) {
                look = ct::PlayerLook::facing(*transform);
            }
        }
        if (const std::optional<ct::Vec3> playerEye = localGame->eyePosition(now)) {
            eye = stepSmoothing.apply(*localGame, *playerEye, client.stepSmoothing.asFloat(), now);
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
        const ImGuiViewport* screen = ImGui::GetMainViewport();
        hud.draw(screen->Pos.x, screen->Pos.y, screen->Size.x, screen->Size.y, now);
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
        if (now < noticeUntil) {
            drawNotice(notice);
        }
        consoleWindow.draw();
        const ImDrawData* ui = devUi->endFrame();

        const bool thirdPerson = client.thirdPerson.asBool();
        ct::Camera camera;
        camera.position = thirdPerson ? ct::thirdPersonPosition(localGame->level().collision(), eye,
                                                                look.yaw, look.pitch)
                                      : eye;

        events.clear();
        gameEvents.update(localGame->latestState(), localGame->timeline(), localGame->level(),
                          settings.movement, events);
        gameEffects.add(events, now);
        if (const ct::State* latest = localGame->latestState()) {
            sounds->update(events, localGame->player(), localGame->level(), latest->world,
                           camera.position, look.yaw, client.volume.asFloat());
        }
        shapes.clear();
        effects.clear();
        ct::addPlayers(shapes, *localGame, now, thirdPerson);
        gameEffects.build(now, effects);
        renderer->setDynamicGeometry(shapes, effects);

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
    sounds.reset();
    audio.reset();
    renderer.reset(); // before the ImGui context it serves
    devUi.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
