# CrofTech Roadmap

Revised 2026-10-20 from the architecture doc ("Engine Architecture: Netcode-First idTech-Style
Engine"). The order puts engine, editor, gameplay and campaign tools before real networking.

## Netcode seams kept from day one

Real networking arrives at M6, but these are cheap now and very expensive to retrofit:

- **Pure simulation tick.** `step(prev, inputs, tick)` has no rendering, audio, I/O or wall-clock
  time.
- **Client and server split in one process.** Single-player and play-in-editor run a local server
  and client that exchange serialized bytes over an in-memory transport. M6 swaps the transport.
- **Plain-data components with a schema.** The same schema drives the editor inspector, saves and,
  later, network replication.
- **Inputs-only clients.** The client never decides an outcome.
- **In-memory network conditioner.** Adds latency and loss to the in-memory transport so gameplay
  can be tried at 100 ms+ long before UDP exists.

## Milestones

Each milestone ends in something playable.

| | Milestone | Scope | Done when |
|---|---|---|---|
| M0 | Bootstrap | CMake presets, vcpkg, CI, `CLAUDE.md`, doctest, Tracy hookup | **Done.** One command builds and tests Windows debug, asan and profile builds |
| M1 | Foundations | SDL3 window and raw input, Vulkan static mesh, fixed 128 Hz loop with interpolated uncapped rendering, ECS with `EntityId` handles, state hashing, player movement and collision on a test level, headless sim tests | Walk the test level; movement tuned from a data file; headless movement tests pass; Tracy shows tick cost |
| M2 | Editor and level logic | Docked ImGui editor, text level format, TrenchBroom `.map` import, undo/redo command stack, schema-generated inspector, play-in-editor over the in-memory transport, logic entities (triggers, relays, counters, timers, doors, movers) with event wiring, saves and checkpoints, play from here | Build a level with a working door puzzle in the editor, play it, save, load |
| M3 | Gameplay and AI | Hitscan and projectile weapons, damage, pickups, respawn, navmesh from the level, enemy perception and data-driven behaviours, encounter system (waves, arena locks, music cues), script entity (Lua or Luau) | First playable single-player arena; an encounter built with wiring plus one script |
| M4 | Editor tools | Brush tools (create, clip, extrude), transform gizmos, snapping, visual logic graph, asset browser, hot reload of textures, materials, shaders and levels | Block out and wire a new arena without leaving the editor |
| M5 | Rendering and animation | Render graph, clustered forward lighting, PBR, cascaded and local shadows, GPU-driven culling, skeletal animation, sequencer (camera paths, animation, audio, events), TAA plus a sharp competitive mode | A scripted intro sequence in a lit, shadowed level at a stable frame rate |
| M6 | Real networking | UDP transport, connect tokens, encryption, redundant inputs, delta-compressed snapshots, clock sync, prediction and reconciliation, interpolation, netgraph, packet fuzzers | Two machines play smoothly at 150 ms RTT, 20 ms jitter, 5% loss |
| M7 | Combat netcode and scale | Lag compensation with rewind cap, sub-tick inputs, server-side projectiles, demo recording, shot debugger, relevancy and visibility culling, bandwidth budgets, Linux dedicated server, bot clients | 24-hour bot soak with no leaks; server tick p99 under 4 ms |
| M8 | Campaign polish | Level streaming and whatever the first campaign needs | A multi-section campaign level streams in and plays start to finish |

## Decisions

| Decision | Outcome |
|---|---|
| Licence | All code written from scratch; no GPL id code reused |
| Platforms | Windows client and tools; Linux dedicated server binary (Linux builds kept in CI) |
| Movement style | Classic acceleration and friction model, with every value in a tuning file; feel chosen by playtesting in M1. Abilities (dash, double jump) added later as predicted systems |
| Float strategy | Strict float settings with same-build determinism; FMA contraction disabled; state quantized to network precision at the end of each tick |
| Scripting | Event wiring between entities first (M2). A script entity that plugs into the same wiring follows in M3. Script state lives in components so saves, rewinds and prediction keep working |

## Still open

- **Game mode for multiplayer**: duel or team deathmatch. Needed by M6; sets the bandwidth budget.
- **Script language**: Lua or Luau. Needed by M3.
- **Lag-compensation cap**: starts at 200 ms; tune in M7.
- **Hosting**: self-hosted, community servers, or both. Needed by M7.
- **Threading model**: proposal is simulation on a fixed-step thread, rendering and input on the
  main thread, handing off through two read-only state buffers. Needed in M1.

## Design notes carried from the architecture review

- Visibility culling needs visibility data from the level compiler; the compiler must exist before
  M7's culling work.
- Jolt rigid bodies live outside the plain-data ECS: keep them server-authoritative, not predicted
  or rewound, and interpolated on clients.
- The server uses one UDP socket for all clients, not one per client.
- Snapshot send rate should be configurable separately from the 128 Hz tick, and large snapshots
  need fragmentation.
- Reserve sub-tick fields in the input command from the start, so the protocol doesn't change in M7.
- Derive the lag-compensation history length from the rewind cap plus the maximum interpolation
  delay, rather than fixing it at 32 ticks.
