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
| M1 | Foundations | SDL3 window and raw input, Vulkan static mesh, fixed 128 Hz loop with interpolated uncapped rendering, ECS with `EntityId` handles, state hashing, player movement and collision on a test level, headless sim tests | **Done** (playtested 2026-10-20). Walk the test level; movement tuned from a hot-reloading data file; headless movement and determinism tests pass; tick cost shown in the title bar and Tracy |
| M2 | Editor and level logic | Docked ImGui editor, text level format, TrenchBroom `.map` import, brush tools (multi-selection, move/rotate/scale gizmos, primitives such as wedges, cylinders, arches and stairs, vertex, edge and face editing, clip, extrude, hollow, subtract, merge), undo/redo command stack, schema-generated inspector, play-in-editor over the in-memory transport, logic entities (triggers, relays, counters, timers, doors, movers) with event wiring, saves and checkpoints, play from here | Build a level with a working door puzzle in the editor, play it, save, load |
| M3 | Gameplay and AI | Hitscan and projectile weapons, damage, pickups, respawn, navmesh from the level, enemy perception and data-driven behaviours, encounter system (waves, arena locks, music cues), script entity (Luau) | First playable single-player arena; an encounter built with wiring plus one script |
| M4 | Editor tools | Texture and material assignment with face alignment, editable level-geometry meshes, visual logic graph, asset browser, hot reload of textures, materials, shaders and levels | Block out and wire a new arena without leaving the editor |
| M5 | Rendering and animation | Render graph, clustered forward lighting, PBR, cascaded and local shadows, GPU-driven culling, skeletal animation, sequencer (camera paths, animation, audio, events), TAA plus a sharp competitive mode, level compiler and content cooking into pack files | A scripted intro sequence in a lit, shadowed level at a stable frame rate |
| M6 | Real networking | Pack checksums so clients and server run identical content, UDP transport, connect tokens, encryption, redundant inputs, delta-compressed snapshots, clock sync, prediction and reconciliation, interpolation, netgraph, packet fuzzers | Two machines play smoothly at 150 ms RTT, 20 ms jitter, 5% loss |
| M7 | Combat netcode and scale | Lag compensation with rewind cap, sub-tick inputs, server-side projectiles, demo recording, shot debugger, relevancy and visibility culling, bandwidth budgets, Linux dedicated server, bot clients | 24-hour bot soak with no leaks; server tick p99 under 4 ms |
| M8 | Campaign polish | Level streaming and whatever the first campaign needs | A multi-section campaign level streams in and plays start to finish |

## M2 plan

In order; each step builds, tests and commits on its own.

1. **Engine/game split.** *Done: `games/sandbox/`, layered content, `--game`.* CrofTech becomes an engine that games are built on. Each game lives in
   `games/<name>/` with its own content (config, levels, assets) and, later, its own gameplay code.
   The engine ships defaults; a game overrides them (movement tuning, rules, weapons), and a level or
   game mode can override the game. The client and editor take `--game <name>`.
2. **Dear ImGui with docking**, plus a developer console (cvars) and a basic netgraph-style
   overlay (frame time, tick time, speed). *Done: config files and the console share one set of
   console variables; ImGui is drawn by our own Vulkan pass.*
3. **Level format.** A text, diff-friendly level file (brushes, entities, properties) with a loader
   and saver, and a TrenchBroom `.map` importer for blockout until our brush tools exist.
   *Done: `.level` files with fuzzed loader and importer, `--map` and the `map` command; the
   movement test level is now `content/levels/movement_test.level`.*
4. **Component schema.** Each component declares its fields once; the declaration drives the editor
   inspector, saves and, later, network replication. *Done: `CT_FIELD` schemas checked to cover
   every byte, a versioned state save that tolerates added and removed fields (fuzzed), and a
   read-only entity inspector.*
5. **Editor shell.** Docked viewport, scene hierarchy, inspector, console; selection; undo/redo
   command stack from the first edit; basic brush editing (create, move, resize and delete box
   brushes on a snapping grid), enough to block out the M2 door puzzle without another editor.
   *Done: `croftech_editor.exe` with docked hierarchy, inspector and console around the 3D view;
   fly camera, picking, move, raise, face-drag resize, box tool, duplicate, delete, entity
   property editing, grid sizes, save with an unsaved-changes prompt.*
6. **Brush tools.** Brought forward from M4 (2026-10-21) because every later step is tested in
   levels built in the editor, and boxes alone make that slow. Three parts, each committed and
   tried on its own:
   1. *Selection and transforms:* tool toolbar, multi-selection (Ctrl-click, drag a box), move,
      rotate and scale gizmos, 90° rotations and flips, all working on any brush shape. *Done.*
   2. *Shapes:* wedge, cylinder, cone, pyramid and rough sphere tools; arches and stairs built from
      several brushes; a settings panel (sides, steps, thickness) with a live preview. *Done:
      drawn as a rectangle then a height; the settings reshape the shape just placed.*
   3. *Vertex, edge and face editing, and cutting:* drag corners, edges and faces of any brush with
      snapping (edits that would make a brush concave or flat are refused); clip, extrude, hollow,
      subtract and merge. *Done (2026-10-21). A test showed float planes letting corners drift
      off the grid (up to 1 mm at 512 m and 4 mm at 2 km after 1000 corner edits), so brushes are
      now stored as corners and faces with planes derived, and built in double precision; corners
      on the grid stay exact. Faces are built by clipping plane by plane, and the limit rose from
      64 to 512 faces per brush.*
7. **Play-in-editor.** A local server and client exchanging serialized bytes over an in-memory
   transport, with the network conditioner for latency and loss.
8. **Logic entities and event wiring.** Triggers, relays, counters, timers, doors and movers (which
   need player collision against moving brushes), wired in the inspector.
9. **Saves, checkpoints and play from here.**

## Decisions

| Decision | Outcome |
|---|---|
| Licence | All code written from scratch; no GPL id code reused |
| Platforms | Windows client and tools; Linux dedicated server binary (Linux builds kept in CI) |
| Movement style | Classic acceleration and friction model, with every value in a tuning file; feel chosen by playtesting in M1. Abilities (dash, double jump) added later as predicted systems |
| Float strategy | Strict float settings with same-build determinism; FMA contraction disabled; state quantized to network precision at the end of each tick |
| Per-game tuning | Movement, rules and content are game data layered over engine defaults (engine → game → mode/level). Games change numbers in data and behaviour in their own code; the engine's movement model stays shared so prediction and netcode work for every game |
| Threading | Simulation on its own fixed-step thread; rendering and input on the main thread, reading the two latest published states (M1) |
| Content packaging | `.level` text files are the editable source. Shipped builds cook levels and assets into binary pack files: shared packs plus a small pack per level, checksummed so clients and server can prove they match. Packs load as one more content layer and are untrusted input (bounds-checked, fuzzed). Built with the level compiler in M5, needed by M6 |
| Scripting | Event wiring between entities first (M2). A script entity that plugs into the same wiring follows in M3. Script state lives in components so saves, rewinds and prediction keep working |
| Level editor | Our own editor, brush tools and later modelling tools; TrenchBroom is not part of the workflow. The `.map` importer stays for bringing in existing maps |
| Modelling | Brushes first; later, editable meshes for level geometry (bevels, curves, detail) in our editor. Characters, weapons and animation are made in Blender and imported as glTF |
| Quality bar | The editor and engine must eventually compare with the latest idTech and id Studio (2026-10-21). As there, brushes are for blockout and collision; visual detail comes from editable level meshes, imported models, materials, decals and lighting. Every limit and simplification is temporary, and detail features come forward wherever the order allows |
| Script language | Luau: built to sandbox untrusted scripts (maps downloaded from servers), small enough to compile into the Linux server, and simple to keep deterministic. Gameplay code (weapons, rules, modes) stays C++; a C# gameplay layer may be revisited once networking works (after M6/M7) |

## Still open

- **Game mode for multiplayer**: duel or team deathmatch. Needed by M6; sets the bandwidth budget.
- **Lag-compensation cap**: starts at 200 ms; tune in M7.
- **Hosting**: self-hosted, community servers, or both. Needed by M7.

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
