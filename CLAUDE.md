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
`client.show_entities 1` opens a live, read-only view of every entity's components.

Run the editor: `build\windows-debug\bin\croftech_editor.exe [--game sandbox] [--map name]`. Every
edit is a command (`engine/editor`: `CommandStack`, `LevelCommands`), so undo covers everything;
drags preview live and record one command when released. Editing logic lives in `engine/editor`
and is unit-tested; `apps/editor` holds only the ImGui panels, the 3D view and input.

Levels are `.level` text files (`ct::parseLevel`/`formatLevel`, format described in
`level/LevelFile.h`): the editable source, diffable in git. A level name is looked up as
`levels/<name>.level`, then `levels/<name>.map` (TrenchBroom, imported on load), through the
content layers; a name with an extension is a path. Shipped games will later cook levels and
assets into packed binary files; the text file stays the source.

CrofTech is an engine; games are built on it. Each game lives in `games/<name>/` with a `game.cfg`
(title, start_map) and a `content/` folder. Content is layered: engine `content/` first, then the game's.
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
    engine/level     level data: brushes, entities, brush geometry, level files, .map import
                     (core)
    engine/sim       ECS world, components, collision, movement        (core, level)
    engine/platform  precise timers; later sockets                     (core)
    engine/client    simulation thread; later prediction, interpolation (sim, platform)
    engine/render    Vulkan renderer, incl. Dear ImGui drawing (Windows client only)  (core)
    engine/devui     Dear ImGui context, developer console, stats overlay, entity inspector
                     (core)
    engine/editor    editor document, undo/redo commands, brush editing and picking maths
                     (core, level)
    engine/net       bitstream, transport, channels, crypto, protocol  (M6)
    engine/server    sessions, input buffers, lag compensation          (M6-M7)
    game/            weapons, damage, rules (simulation systems)
    apps/            client (croftech.exe), editor (croftech_editor.exe), common (code both
                     share); later dedicated_server, levelc
    content/         engine default content (config, levels; later assets)
    games/<name>/    a game: game.cfg, content/ overrides, later its gameplay code
    tests/           unit/, sim/ (headless), fuzz/

Each module is a CMake target `ct_<module>` (alias `ct::<module>`) with public headers under
`include/croftech/<module>/`. A module only sees headers of modules it links, so these rules hold:

- `sim` and `game` never depend on `render`, `platform`, audio or wall-clock time.
- `server` and the dedicated server never link `render`.
- `render` reads simulation state; it never writes it.
- Dear ImGui is drawn by our own Vulkan code in `render` (`UiRenderer`), not ImGui's Vulkan
  backend, which links the system Vulkan loader and would clash with volk.

Every target calls `ct_apply_defaults(<target>)`; test executables also call `ct_add_test`.

## Simulation rules (determinism)

- Fixed 128 Hz tick. `State next = step(const State& prev, const InputSet& inputs, TickIndex tick)`.
  No wall-clock time, globals, I/O, rendering or audio inside a tick.
- Same build + same platform + same inputs = bit-identical state. Fixed iteration order everywhere;
  never iterate an unordered container in simulation code. RNG is seeded and stored in the state.
- Floats: no fast-math; FMA contraction is disabled build-wide (`-ffp-contract=off`). Do not
  re-enable it or add `/fp:fast`.
- No `std::sin`/`std::cos`/`std::atan2` etc. in simulation code: their results differ between
  platforms, and the Linux server must match Windows clients. Use `ct::sinCos` (core/Angle.h) and
  `+ - * / sqrt` only. Angles in the simulation are 16-bit turn fractions, as in `UserCmd`.
- Every component has a schema next to it (`Schema<T>`, one `CT_FIELD` per member, core/Schema.h)
  and a `static_assert(describesEveryByte(...))`, which fails if a field is missing from the schema
  or the struct has padding (states are hashed as raw bytes). The schema drives saves
  (`sim/StateSave.h`), the inspector and, later, replication; generic code never names fields.
  Type and field names are stable identifiers in save files: renaming one drops it from old saves.
  Register every new component pool in `forEachPool` (sim/World.h).
- Units are metres and seconds, Z up. Movement tuning lives in `MovementSettings`.
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
