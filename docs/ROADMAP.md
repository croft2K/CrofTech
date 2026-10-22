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
| M2 | Editor and level logic | Docked ImGui editor, text level format, TrenchBroom `.map` import, brush tools (multi-selection, move/rotate/scale gizmos, primitives such as wedges, cylinders, arches and stairs, vertex, edge and face editing, clip, extrude, hollow, subtract, merge), undo/redo command stack, schema-generated inspector, play-in-editor over the in-memory transport, logic entities (triggers, relays, counters, timers, doors, movers) with event wiring, saves and checkpoints, play from here | **Done** (playtested 2026-10-21). Build a level with a working door puzzle in the editor, play it, save, load |
| M3 | Gameplay and AI | Hitscan and projectile weapons, damage, pickups, respawn, navmesh from the level, enemy perception and data-driven behaviours, encounter system (waves, arena locks, music cues), script entity (Luau) | First playable single-player arena; an encounter built with wiring plus one script |
| M4 | Editor tools | Texture and material assignment with face alignment, editable level-geometry meshes, visual logic graph (wires as nodes, with the outputs table as the text form), movers that follow a path through several stops and movers that turn (rotating doors), asset browser, hot reload of textures, materials, shaders and levels | Block out and wire a new arena without leaving the editor |
| M5 | Rendering and animation | Render graph, clustered forward lighting, PBR, cascaded and local shadows, GPU-driven culling, skeletal animation, sequencer (camera paths, animation, audio, events, keyframed movers with easing), TAA plus a sharp competitive mode, level compiler and content cooking into pack files | A scripted intro sequence in a lit, shadowed level at a stable frame rate |
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
   transport, with the network conditioner for latency and loss. *Done (2026-10-21):
   `engine/net` (fuzzed protocol, LoopbackLink with latency, jitter and loss), `engine/server`,
   `LocalGame`; the client plays through it too (`net.fake_*` settings); F5 in the editor. Full
   snapshots and no client prediction until M6, so play under latency feels delayed.*
8. **Logic entities and event wiring.** Triggers, relays, counters, timers, doors and movers (which
   need player collision against moving brushes), wired in the inspector. In three parts:
   - 8a. *Done.* Brush entities and outputs in the level (file version 3, `.map` doors and triggers
     import), the entity class table, and the editor: a click picks the whole entity, and the
     inspector edits class properties and outputs. After Tony's review, door and mover became one
     `mover` class (what activates it: inputs, touch or standing on it; wait, loop), with
     ready-made Door, Lift, Platform and Trigger presets that need no wiring, a see-through copy
     of a selected mover at its end with a handle to drag it, and wires drawn in the view between
     connected entities, labelled with event and input. The fast path for quick iteration; every
     property and output stays editable underneath.
   - 8b. *Done.* Logic in the simulation: the server builds a `GameLevel` from the whole level
     (collision without triggers, wiring resolved to entities and inputs, with warnings for wires
     that can't work); triggers, relays, counters and timers are components, and delayed outputs
     wait as entities, so logic is saved, rewound and sent like the rest of the state. Triggers
     are hidden in play.
   - 8c. *Done.* Doors and movers: player collision against moving brushes (pushed, carried,
     blocked, and a closing door goes back up rather than crushing), doors that open when walked
     into, lifts that rise when stood on, platforms that loop, and moved brushes drawn from the
     snapshots. Movers slide in straight lines; paths and turning movers are in M4.
9. **Saves, checkpoints and play from here.** *Done (2026-10-21).* Save games record the level
   (name and content hash, so a save of an edited level is refused) and the state; F5/F9 and the
   save and load commands in the game, F6/F9 in play-in-editor. Checkpoint volumes save the first
   time a player walks in. Loading replaces the server's state and starts a new snapshot timeline,
   which clients follow. Shift+F5 in the editor plays from the 3D camera.

## M3 plan

Agreed 2026-10-21. In order; each step builds, tests and commits on its own and ends in something
to try. Gameplay is simulation code, so all of it is deterministic, saved, rewound and sent in
snapshots like the logic of M2, and every tuning value is game data layered over engine defaults.

1. **Health, damage and respawn.** Players get health and armour. All damage goes through one
   function (armour takes a share, knockback pushes, the attacker is remembered for later credit
   and AI). A dead player drops, then respawns at the spawn point furthest from other players when
   fire is pressed. Hurt volumes (lava, kill pits; Hurt and Kill presets) are switched by the
   wiring, and falling out of the world kills. Game rules (starting and maximum health, armour
   share, respawn delay) are a layered config file like movement. A first HUD, shared by the game
   and play-in-editor, shows health, armour, a damage flash and a death screen.
2. **Seeing things: entity drawing, effects and sound.** *Done (2026-10-22): players drawn as
   boxes (third person with `client.third_person`), an additive effect pipeline (respawn glow),
   `engine/audio` (fuzzed WAV reader, mixer), and sounds for footsteps, jumps, landings, pain,
   death, respawn, movers and checkpoints, from events the client works out from snapshots.* The
   renderer draws simple shapes for
   simulated things (other players, enemies, items, projectiles) until M5 brings models. Effects
   (tracers, impacts, pain) are presentation: the client works them out from what changed between
   snapshots, and they are never rewound. A new audio module (SDL3 audio, a small mixer with 3D
   panning and distance falloff) plays sounds. WAV files are read as untrusted input and fuzzed;
   placeholder sounds are generated by a script, so no third-party assets are needed.
3. **Weapons.** In three parts. *3a done (2026-10-22): weapon files (`weapons/<name>.cfg`, listed in
   `game.cfg`), a `Weapons` component (ammo, the weapon in hand, raise and refire times, a random
   counter), hitscan shots with spread against the world, movers and player boxes, switching by
   number keys and the wheel through the input command's weapon field (plus reserved sub-tick
   fields, protocol version 3), and changing to the best loaded weapon when one runs dry. Next:
   3b rockets and grenades, 3c crosshair, ammo on the HUD, view weapon, tracers, impacts and firing
   sounds.* Each weapon is a data file in the game's content. Hitscan weapons spread their
   shots from a seeded RNG kept in the state. Projectiles are simulated entities: rockets with
   splash damage and knockback (rocket jumps), and grenades that bounce. Weapons have ammo and are
   switched with a new weapon field in the input command, which also reserves the sub-tick fields
   for M7. Shots trace against the world, movers and the hit boxes of players and enemies. A
   crosshair, ammo on the HUD and a placeholder view weapon.
4. **Pickups.** Health, armour, ammo and weapon items placed in the editor. They respawn after a
   delay in arenas and stay taken in single player, with an on_pickup event for the wiring.
5. **Navigation mesh.** Recast (zlib licence) builds it from the level's collision when the
   GameLevel is built; Detour finds paths. Doors and lifts are links that open and close with
   their movers. The mesh is drawn on request in the game and in the editor, and building a test
   level's mesh is checked by tests.
6. **Enemies.** Enemy types are data: health, size, speed, senses, attacks and behaviour tuning.
   Enemies move with the player movement code, driven by commands from their AI, so they step,
   slide and ride lifts the way players do. They see players inside a view cone with line of
   sight, and hear shots and footsteps. Behaviour is a state machine (idle, patrol, alerted, chase,
   attack, strafe or take cover, search) whose tuning is data. on_alert and on_death events go to
   the wiring. Two kinds to start: a melee charger and a ranged gunner.
7. **Encounters.** An encounter entity runs waves from spawner entities: how many at once, and
   when the next wave starts. It locks the arena through the wiring (on_start closes the doors,
   on_cleared opens them). A music entity with play and stop inputs crossfades between tracks.
8. **Script entity (Luau).** Takes inputs and fires outputs through the same wiring, and keeps its
   state in components, so it is saved and rewound. It is sandboxed for maps downloaded from
   servers: no I/O, an instruction and memory budget per call, and deterministic maths only.
9. **First playable arena.** Built in the editor, with weapons and pickups, waves of both enemy
   kinds, arena locks, music and one scripted moment. M3 is done when it has been played through.

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
| Gameplay code | Gameplay systems (health, weapons, pickups, AI, encounters) are part of `engine/sim`, because their components live in the `World` that is saved, rewound and sent. Games change them through data files; game-specific code waits until there is a game that needs it. Navigation (Recast/Detour) and scripting (Luau) get modules of their own that the simulation links |
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
