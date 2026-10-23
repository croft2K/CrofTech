# CrofTech

A netcode-first, idTech-style engine for fast competitive FPS games. The simulation is
deterministic, rewindable and serializable; every other system serves it. Design reference:
"Engine Architecture: Netcode-First idTech-Style Engine" (Oct 2026). Milestone order, decisions and
open questions: `docs/ROADMAP.md` (engine, editor and gameplay come before real networking, but the
netcode seams listed there are kept from day one).

## Build and test

Windows (any shell; the script loads the Visual Studio environment and keeps your `VCPKG_ROOT`):

    .\scripts\build.ps1                          # windows-debug: configure, build, run tests
    .\scripts\build.ps1 -Preset windows-asan     # AddressSanitizer
    .\scripts\build.ps1 -Preset windows-release
    .\scripts\build.ps1 -Preset windows-profile  # Release + Tracy zones

Linux: `cmake --workflow --preset linux-debug` (also `linux-release`, `linux-asan` with UBSan).
`linux-fuzz` builds only the fuzz targets as libFuzzer executables (CI runs each for a minute);
in every other build they replay their seed corpus in `tests/fuzz/corpus/` as ordinary tests.

Run tests only: `ctest --preset windows-debug`. Build output goes to `build/<preset>/`; every
executable and the compiled shaders land in `build/<preset>/bin/`.

Run the client: `build\windows-debug\bin\croftech.exe [--game sandbox] [--map name] [--frames N]
[--exec "cmds"]` (`--map` defaults to the game's `start_map`; `--frames` quits after N frames, for
automated smoke runs; `--exec` runs console commands once the config files have loaded). In the
client, the key below Esc opens the developer console; `map <name>` there switches level and
`client.show_entities 1` opens a live, read-only view of every entity's components,
`client.invert_mouse 1` inverts mouse look (the editor reads it too, and has it in its View
menu), and
`client.step_smoothing` sets how long the view takes to follow a step up or down stairs
(`client/StepSmoothing.h`; play-in-editor uses the default). Players are drawn as simple boxes
(`apps/common/EntityShapes`, through `Renderer::setDynamicGeometry`, which also takes see-through
effects) until M5 brings models; `client.third_person 1` follows your own from behind. Sounds
and effects come from `GameEvents` (`client/GameEvents.h`), which the client works out from what
changed between snapshots (a jump, a hit, a door setting off), so the server sends no events.
`apps/common/GameSounds` plays `sounds/<name>.wav` through the content layers (the engine's are
placeholders made by `scripts/make_placeholder_sounds.py`) on SDL audio (`AudioOutput`), at
`client.volume`. The client
and its game's server run in one process and talk only through packets over an in-memory link
(`ct::LocalGame`); `net.fake_latency_ms`, `net.fake_jitter_ms` and `net.fake_loss_percent` make
that link behave like a poor network. F5 saves and F9 loads the newest save (`save [name]` and
`load [name]` at the console), and reaching a checkpoint saves as `checkpoint`. A save
(`sim/SaveGame.h`, fuzzed) is the level's name and content hash plus the state; saves go to SDL's
pref path (`%APPDATA%/CrofTech/<game>/saves`), never into content. Loading replaces the server's
state (`Server::restore`), and snapshots carry a timeline number so the client follows it back in
time. Players have health and armour (the HUD in `apps/common/GameHud` is shared with
play-in-editor); a dead player respawns at the spawn point furthest from other players when fire
is pressed after `rules.respawn_delay`. Players carry weapons (`sim/Weapons.h`, fired by
`runWeapons` in `sim/Firing.h`): each is a file, `weapons/<name>.cfg` (variables
`weapon.<name>.<setting>`), and a game lists its weapons in `game.cfg` in number-key order. Number
keys and the mouse wheel choose one (`WeaponSelect` in `apps/common/PlayerInput`). A weapon with a
projectile speed fires rockets or grenades (`sim/Projectile.h`): simulated entities that explode
on contact or when their fuse runs out, hurting and throwing what is near (rocket jumps). The HUD
shows a crosshair (marked when your shot hurts someone), the weapons carried and the ammo in hand;
a placeholder weapon is held in view (`addViewWeapon`). Shots make Fire and Impact game events:
the client works out where hitscan pellets went from the shooter's random counter, as the server
did, for tracers and sparks, and each weapon fires with `sounds/<weapon>_fire.wav`. Pickups
(the `pickup` class: health, armour, ammo or a weapon named from the game's list) are taken by
walking into them when needed, come back after their respawn time (or never, for single
player) and fire on_pickup; health and armour above `rules.max_health`/`max_armour` drain away.
`--map weapons_test` is a small arena with all of them. Enemies (the `enemy` class, of a kind
from the game's list: `enemies` in `game.cfg`, each kind a file `enemies/<name>.cfg` with
variables `enemy.<name>.<setting>`) are bodies moved by the player movement code at their kind's
size and speed (`movementOf` in `sim/Enemies.h`; every system that touches bodies uses each one's
own box), driven by input commands from their AI (`runEnemies`, `sim/src/Enemies.cpp`, state in
`sim/Enemy.h`). Each stands at its post, or walks its patrol (`patrol` names a `patrol_point`,
each point names its `next` and has a wait and on_arrive), until it sees a player in its view
cone with nothing in between, hears one shoot or run nearby (unless it lies in ambush), is hurt,
or gets the alert input (on_alert); then it reacts, chases along the navigation mesh (opening
doors, waiting for and riding lifts, going round doors it can't open), strikes in reach and, with
a weapon (the gunner), shoots in bursts through the weapon code from its distance, sidestepping,
and by its kind's cover chance runs after a burst or a hit to a nearby spot the target can't see,
for a while. A target lost from sight is followed for a moment, then searched for about where
it was lost; then the enemy walks back to its post or patrol. on_death goes to the wiring, and
the body goes after its corpse time. Players and enemies are solid to each
other (`CollisionScene::withBodies`). Players hold lifts at their end; enemies don't, so a lift
carries a waiting enemy back. `--map enemies_test` has chargers in the open, on a platform with
a lift and behind a door, one on patrol, and gunners on the platform (on patrol) and beyond lava
(with pillars to hide behind). In the editor, the patrol routes of selected entities are drawn in
green (`entityReferences` in `editor/EntityLinks.h`, for any property of the Reference type), and
the inspector's "Add patrol point" puts the next point of a route a few metres on, linked in;
enemy markers are their kind's size (`BodySizes` in `editor/LevelItem.h`). An enemy sounds like
its kind (`sounds/<kind>_alert.wav`, then `alert.wav`; the same for pain, death, footstep and
swing), and its noticing you (`EnemyAlert`) and swinging (`EnemySwing`) are game events.
`client.show_ai 1` (Play > Show what enemies think in the editor) labels each enemy with its
state and health and draws its view cone, hearing ring, path, goal, target and post
(`apps/common/EnemyThoughts`). Encounters (`sim/src/Encounters.cpp`, run after enemies) fight in
waves: each `spawner` names its `encounter` and wave, and sends its count of enemies one at a time
as its spot clears (hunting the nearest player unless `hunt` is off), or one for each spawn
input; the encounter starts on its start input (or with the level), keeps to `max_alive`,
starts the next wave once `next_wave_at` are left and `wave_delay` has passed, and fires
on_start, on_wave and on_cleared, which lock and unlock the arena through the wiring. A sent
enemy's `Enemy::spawner` names its spawner, whose wiring gets its events. `--map
encounter_test` is a hall of weapons leading into a three-wave arena. Script entities (the
`script` class) run Luau scripts, `scripts/<name>.luau` through the content layers (written for
level designers in `docs/Scripting.md`): a script returns a table declaring its state (numbers and
booleans), events, inputs and an optional start function, and the wiring reaches its inputs and
its events like any entity's (`scriptInput`/`scriptEvent` in `sim/GameLevel.h` number them from
128). `engine/script` compiles (`ScriptProgram`) and runs them (`ScriptRunner`): every call is a
fresh Luau state in a 4 GB-aligned arena (`ScriptArena`; Luau hashes pointers by their low 32
bits, so table order is the same everywhere), held to a step and a memory budget, with no I/O and a
library cut to what is exact on every platform (no `^`, only exact `math` functions, no
`--!optimize`); `tostring` shows references by their place in the arena. The declared state lives
in the `Script` component as doubles, so scripts save, rewind and replicate; a call's effects
(`game:fire`, `game:after`, `game:send`) are applied only if it finishes. The client and editor
read a level's scripts with `loadLevelScripts` (the editor each time play starts, and its
inspector through `ScriptLibrary`, which offers a script entity's events and inputs for wiring);
saves record the level's hash continued with its scripts'. The editor edits scripts in its Scripts
window (`apps/editor/ScriptPanel`, `CodeEditor`; the text, cursor, undo and Luau colouring are
`TextBuffer` and `LuauHighlight` in `engine/editor`): text being edited is compiled as it is typed
(`ScriptLibrary::setText`) and plays unsaved, and beside it are the entities running the script
(outlined in the view), the wiring in and out of them and the problems with it (`ScriptLinks`,
which also checks `game:send` calls written with names in quotes; `ScriptProgram::callSites`). What scripts print and their errors
reach the log through `Simulation::takeScriptMessages`. `--map script_test` is a combination lock.

Run the editor: `build\windows-debug\bin\croftech_editor.exe [--game sandbox] [--map name]`. Every
edit is a command (`engine/editor`: `CommandStack`, `LevelCommands`), so undo covers everything;
drags preview live and record one command when released, and edits of several selected items are
one compound command. Moves, turns, flips and scales of any brush shape go through
`AffineTransform` (double precision, exact for quarter turns, flips and grid-aligned scales) and
`SelectionDrag`. New shapes (`Shapes.h`) are corner sets fitted to the drawn box and built as
their convex hull. The Vertex tool moves corners with `withCornersMoved` (the hull of the moved
corners, so corners are never recomputed); clip, subtract, hollow, merge and extrude are in
`BrushOperations.h`. Play (F5, or Shift+F5 to start where the 3D
camera is) runs the level being edited as a game in the
view without saving it: the same `LocalGame` as the client, with network conditions from the Play
menu; Esc stops, and F6 and F9 save and load in memory while it lasts. The view has one or four panes (3D, or orthographic top, front and side as wireframe plans; View
menu), and the tools work in whichever pane the mouse is in. Editing logic lives in `engine/editor` and is unit-tested; `apps/editor` holds
only the ImGui panels, the 3D view, gizmos and input.

Levels are `.level` text files (`ct::parseLevel`/`formatLevel`, format described in
`level/LevelFile.h`): the editable source, diffable in git. Brushes are stored as corners and
faces, with planes derived from them (`level/BrushGeometry.h`), because float planes let corners
drift off the grid by millimetres far from the origin. Brush geometry is built in double precision
(`brushFromPlanes`, `brushFromPoints`, `clipBrush`) and rounded to float once, so corners on the
grid stay exact through any number of edits. Entities are point entities (an "origin") or brush
entities (doors, movers, triggers: the brushes whose `Brush::entity` is theirs; the file nests them
in the entity's block). Groups (`Level::groups`) are named sets of world brushes and entities that
the editor selects and moves as one; a brush of an entity is in its entity's group. Brushes can
have names. Both only organise the level; play ignores them. TrenchBroom groups import as groups. Outputs wire entities together: when an event happens, an input is sent
to every entity with the target name, after a delay. `level/EntityClasses.h` lists each class's
properties (types, defaults), events and inputs, and the ready-made presets (Door, Lift, Platform, Trigger, Checkpoint, Hurt, Kill, and the pickups Health, Mega health, Armour, Heavy armour, Ammo, Weapon);
the editor's inspector and the game both read it. To play a level, the server builds a
`GameLevel` (`sim/GameLevel.h`) from it once: world collision (volumes aren't solid), each logic
entity's settings with its outputs resolved to target entities and inputs, and the state play
starts from. Logic entities are simulated entities with components (`sim/LogicComponents.h`), and
a delayed output waits as a `PendingInput` entity, so logic saves and rewinds with everything
else. Movers have their own collision models, placed at each mover's offset by `CollisionScene`,
which player movement traces against; each tick movers move before players, pushing players out
of the way and carrying riders, and a move that would leave a player inside something is undone
(a closing door goes back up). The client draws each mover's brushes as a separate part of the
world mesh, moved by its offset between snapshots. Problems in the wiring are warnings, logged when play starts.
The navigation mesh (`engine/nav`: Recast builds it, Detour finds paths in `NavQuery`) is built
from the same level (`navInput` in `sim/GameLevel.h`) for an agent the player's size, the first
time `GameLevel::navMesh` is asked for it: world brushes are solid, hurt volumes are hazard areas
paths avoid, and each mover is a door (built open; where it stands shut is a door area), a lift
(walkable at both stops, with a link for the ride) or an obstacle, by its `nav` setting. Paths
say at each corner which door or lift comes next, and a filter blocks the movers that won't open.
`client.show_navmesh 1` draws it in the game, and View > Show navigation mesh in the editor,
which rebuilds it on a worker thread as the level changes (`apps/common/NavMeshDrawing`). Recast
comes from an overlay port (`vcpkg-overlays/`) built with virtual query filters and 64-bit
polygon references. In the editor a click in the view
picks a whole group or entity, Alt-click one brush; Ctrl+G groups the selection, Ctrl+Shift+G
ungroups. The hierarchy shows groups and entities as trees and selects exactly the row clicked
(Ctrl toggles, Shift selects a range); F2 or a double-click renames a brush, entity or group; a selected mover shows a see-through copy at its end with a drag handle, and
selected entities show their wires (`editor/EntityLinks.h`). A level name is looked up as
`levels/<name>.level`, then `levels/<name>.map` (TrenchBroom, imported on load), through the
content layers; a name with an extension is a path. Shipped games will later cook levels and
assets into packed binary files; the text file stays the source.

CrofTech is an engine; games are built on it. Each game lives in `games/<name>/` with a `game.cfg`
(title, start_map, weapons, enemies) and a `content/` folder. Content is layered: engine `content/` first, then the game's.
Config files merge setting by setting (a game lists only what it changes); other files are taken
whole from the highest layer that has them (`ct::ContentPaths`, `ct::LayeredConfig`). Development
builds read content from the source tree and reload config whenever it is saved. Debug builds
enable the Vulkan validation layer; treat any validation error it prints as a bug.

Settings are console variables (`ct::CVar`, owned by a `ct::Console`). Each config file maps to a
prefix: the `gravity` line of `config/movement.cfg` is the variable `movement.gravity`. Loading a
file resets its prefix to defaults and applies the layers (`Console::applyConfig`), so the file,
the console and `--exec` all change the same values. Code reads values through the `const CVar&`
that `Console::add` returns, and polls `Console::changeCount()` to react to changes.

Windows presets include the client (vcpkg feature `client`: SDL3, volk, VMA, vk-bootstrap, Dear
ImGui with docking and its SDL3 backend) and need
the Vulkan SDK for `glslc`. Linux presets build only the engine, server-side code and tests.
Dependencies come from vcpkg in manifest mode (`vcpkg.json`, pinned by `builtin-baseline`).

Before finishing any change: `windows-debug` and `windows-asan` must build with zero warnings
(warnings are errors) and all tests must pass.

## Layout and dependency rules

    engine/core      math, angles, hashing, asserts, logging, config files, console variables,
                     profiling
    engine/audio     WAV reading (fuzzed) and the software mixer: voices placed in the world,
                     panned and faded with distance; no device, so it is portable and tested
                     (core)
    engine/level     level data: brushes, entities, brush geometry, level files, .map import
                     (core)
    engine/nav       navigation meshes built from brushes (Recast) and path queries (Detour);
                     knows nothing of the simulation, so the editor can build one too
                     (core, level)
    engine/script    Luau scripts: compiling and checking them, the sandbox that runs their
                     calls, reading a level's scripts from content; knows nothing of the
                     simulation (core, level)
    engine/sim       ECS world, components, collision, movement, level logic and gameplay
                     (health, damage, weapons, pickups, enemies, encounters, script entities)
                     (core, level, nav, script)
    engine/platform  precise timers; later sockets                     (core)
    engine/client    client connection, LocalGame (server thread + client over an in-memory
                     link), GameEvents worked out from snapshots, view smoothing; later
                     prediction                                       (net, server, sim)
    engine/render    Vulkan renderer, incl. Dear ImGui drawing (Windows client only)  (core)
    engine/devui     Dear ImGui context, developer console, stats overlay, entity inspector
                     (core)
    engine/editor    editor document, undo/redo commands, brush editing and picking maths
                     (core, level)
    engine/net       protocol messages, Transport, LoopbackLink with the network
                     conditioner; later bitstream, sockets, channels, crypto (M6) (core, sim)
    engine/server    Server (players join over a Transport, inputs, ticks, snapshots),
                     ServerThread; later input buffers, lag compensation (M6-M7)
                     (net, sim, platform)
    apps/            client (croftech.exe), editor (croftech_editor.exe), common (code both
                     share); later dedicated_server, levelc
    content/         engine default content (config, levels; later assets)
    games/<name>/    a game: game.cfg, content/ overrides, later its gameplay code
    tests/           unit/, sim/ (headless), fuzz/

Each module is a CMake target `ct_<module>` (alias `ct::<module>`) with public headers under
`include/croftech/<module>/`. A module only sees headers of modules it links, so these rules hold:

- `sim` never depends on `render`, `platform`, audio or wall-clock time.
- `server` and the dedicated server never link `render`.
- `render` reads simulation state; it never writes it.
- Dear ImGui is drawn by our own Vulkan code in `render` (`UiRenderer`), not ImGui's Vulkan
  backend, which links the system Vulkan loader and would clash with volk.

Every target calls `ct_apply_defaults(<target>)`; test executables also call `ct_add_test`.

## Simulation rules (determinism)

- Fixed 128 Hz tick. `State next = step(const State& prev, const InputSet& inputs, TickIndex tick)`.
  No wall-clock time, globals, I/O, rendering or audio inside a tick.
- Same build + same platform + same inputs = bit-identical state. Fixed iteration order everywhere;
  never iterate an unordered container in simulation code. Random numbers come from counters
  kept in the state (`core/Random.h`), such as each player's `Weapons::random`.
- Floats: no fast-math; FMA contraction is disabled build-wide (`-ffp-contract=off`). Do not
  re-enable it or add `/fp:fast`.
- No `std::sin`/`std::cos`/`std::atan2` etc. in simulation code: their results differ between
  platforms, and the Linux server must match Windows clients. Use `ct::sinCos`,
  `ct::directionToAngle` and `ct::pitchToward` (core/Angle.h) and `+ - * / sqrt` only. Angles in the simulation are 16-bit turn fractions, as in `UserCmd`.
- Every component has a schema next to it (`Schema<T>`, one `CT_FIELD` per member, or one
  `CT_FIELD_ELEMENT` per element of a `std::array` member, core/Schema.h)
  and a `static_assert(describesEveryByte(...))`, which fails if a field is missing from the schema
  or the struct has padding (states are hashed as raw bytes). The schema drives saves
  (`sim/StateSave.h`), the inspector and, later, replication; generic code never names fields.
  Type and field names are stable identifiers in save files: renaming one drops it from old saves.
  Register every new component pool in `forEachPool` (sim/World.h).
- Units are metres and seconds, Z up. Everything a game tunes reaches the simulation as a
  `GameSettings` (sim/GameSettings.h): `MovementSettings` from `config/movement.cfg`,
  `RulesSettings` (health, armour, respawning) from `config/rules.cfg`, a `WeaponSettings` for
  each weapon the game lists and an `EnemySettings` for each kind of enemy. `gameConfigFiles`
  names the files.
- All damage goes through `applyDamage` (sim/Damage.h), so armour, knockback and death work the
  same for every source.
- Components are trivially copyable plain structs with no pointers. Refer to other entities by
  generation-checked `EntityId`, never by pointer.
- Presentation state (particles, sounds, ragdolls, view-model animation) lives outside the
  simulated world and is never rewound.

## Network rules (no exceptions)

- Every read from a packet or any other untrusted bytes (files, maps, assets) is bounds-checked.
  Malformed input is rejected with an error and counted; it never crashes, asserts or is trusted.
- `CT_ASSERT` is for programmer errors only and is compiled out of release builds, so it must
  never be the check that guards untrusted data.
- Clients send inputs only; the server decides every outcome.
- Parsers of untrusted data get a libFuzzer target.

## Code style

- C++20. Format with `.clang-format` (4-space indent, 100 columns).
- Namespace `ct`. Types, enums and enumerators `PascalCase`; functions, methods and variables
  `camelCase`; private members `m_camelCase` (plain-data struct fields have no prefix);
  constants `kPascalCase`; macros `CT_UPPER_CASE`.
- One main type per file, file named after it (`Hash.h`, `Assert.cpp`).
- Include order: own header, `croftech/...`, third-party, standard library.
- Comments explain why, not what. No commented-out code.

## Ownership

- RAII for every resource. No raw `new`/`delete` outside allocators.
- `std::unique_ptr` for single ownership. Avoid `std::shared_ptr` in sim, net and render code;
  use handles instead.
- Raw pointers and references are non-owning. Pass buffers as `std::span`.
- No per-tick heap allocation in the simulation or snapshot paths; use fixed-capacity storage.

## Licence

All engine code is written from scratch. Never copy or port code from id Software's GPL releases
(Quake, Doom, idTech); reimplement ideas from understanding only.
