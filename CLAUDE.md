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

Run tests only: `ctest --preset windows-debug`. Build output goes to `build/<preset>/`.
Dependencies come from vcpkg in manifest mode (`vcpkg.json`, pinned by `builtin-baseline`).

Before finishing any change: `windows-debug` and `windows-asan` must build with zero warnings
(warnings are errors) and all tests must pass.

## Layout and dependency rules

    engine/core      math, angles, hashing, asserts, profiling       (depends on nothing)
    engine/level     level data: brushes, spawn points, brush geometry (core only)
    engine/sim       ECS world, components, collision, movement        (core, level)
    engine/net       bitstream, transport, channels, crypto, protocol
    engine/server    sessions, input buffers, lag compensation, relevancy
    engine/client    prediction, reconciliation, interpolation, presentation state
    engine/render    Vulkan, render graph
    engine/platform  SDL3 window/input, sockets, timers
    game/            weapons, damage, rules (simulation systems)
    apps/            client, dedicated_server, editor, levelc
    tests/           unit/, sim/ (headless), fuzz/

Each module is a CMake target `ct_<module>` (alias `ct::<module>`) with public headers under
`include/croftech/<module>/`. A module only sees headers of modules it links, so these rules hold:

- `sim` and `game` never depend on `render`, `platform`, audio or wall-clock time.
- `server` and the dedicated server never link `render`.
- `render` reads simulation state; it never writes it.

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
- Components must have no padding (they are hashed as raw bytes); each has a `sizeof` check.
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
