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
| M3 | Gameplay and AI | Hitscan and projectile weapons, damage, pickups, respawn, navmesh from the level, enemy perception and data-driven behaviours, encounter system (waves, arena locks), script entity (Luau) with in-editor script editing | **Done** (2026-10-23). An encounter built with wiring (`encounter_test`) and a level run by a script (`script_test`), both played. The planned arena was dropped for entity authoring; music waits for the audio middleware |
| M4 | Entity authoring and editor tools | Entity types made in the editor from templates (enemies, pickups, props), per-entity property overrides, enemy behaviour as graphs on our own deterministic runtime, a node-graph editor shared by behaviours and the visual logic graph (wires as nodes, with the outputs table as the text form), a game data panel (entity types, weapons, movement, rules; saved to the game's files with undo, applied live in play-in-editor); then texture and material assignment with face alignment, editable level-geometry meshes, movers along paths and turning movers, asset browser, hot reload of textures, materials, shaders and levels | Make a new enemy type with a behaviour of its own, place it with per-entity tweaks, and block out and wire an arena with it, without leaving the editor |
| M5 | Rendering and animation | Render graph, clustered forward lighting, PBR, cascaded and local shadows, GPU-driven culling, skeletal animation, Jolt physics (client-only ragdolls and debris first; server-authoritative props later, outside the rewound state), sequencer (camera paths, animation, audio, events, keyframed movers with easing), TAA plus a sharp competitive mode, level compiler and content cooking into pack files | A scripted intro sequence in a lit, shadowed level at a stable frame rate |
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
3. **Weapons.** In three parts, all done. *3a done (2026-10-22): weapon files (`weapons/<name>.cfg`, listed in
   `game.cfg`), a `Weapons` component (ammo, the weapon in hand, raise and refire times, a random
   counter), hitscan shots with spread against the world, movers and player boxes, switching by
   number keys and the wheel through the input command's weapon field (plus reserved sub-tick
   fields, protocol version 3), and changing to the best loaded weapon when one runs dry. 3b done
   (2026-10-22): projectiles as simulated entities (rocket and grenade launcher files: speed,
   gravity, bounce, fuse, splash damage, radius and knockback, self damage), explosions that hurt
   and throw what they can see, rocket jumps, and on the client small boxes for projectiles, an
   explosion flash and sound. 3c done (2026-10-22): crosshair with a hit mark, the weapons carried
   and ammo on the HUD, a placeholder view weapon that kicks and is raised, tracers and impact
   sparks from Fire and Impact events (pellets worked out again from the random counter), and
   firing, impact, hit and switch sounds.* Each weapon is a data file in the game's content. Hitscan weapons spread their
   shots from a seeded RNG kept in the state. Projectiles are simulated entities: rockets with
   splash damage and knockback (rocket jumps), and grenades that bounce. Weapons have ammo and are
   switched with a new weapon field in the input command, which also reserves the sub-tick fields
   for M7. Shots trace against the world, movers and the hit boxes of players and enemies. A
   crosshair, ammo on the HUD and a placeholder view weapon.
4. **Pickups.** *Done (2026-10-22): one `pickup` point class (item health, armour, ammo or
   weapon; amount, limit, weapon chosen from the game's list, respawn delay or -1 for never,
   on_pickup, enable/disable/toggle), presets (Health, Mega health, Armour, Heavy armour, Ammo,
   Weapon), rules for maximum health and armour with the extra draining away, pickups drawn,
   heard and glowing back in the game, and `levels/weapons_test.level` to try it all.* Health,
   armour, ammo and weapon items placed in the editor. They respawn after a
   delay in arenas and stay taken in single player, with an on_pickup event for the wiring.
5. **Navigation mesh.** *Done (2026-10-22): a new `engine/nav` module (Recast and Detour 1.6
   from an overlay port with virtual query filters and 64-bit polygon references), a tiled mesh
   for an agent the player's size built the first time the GameLevel is asked for it, hurt
   volumes as hazard areas, movers as doors, lifts or obstacles (the mover's new `nav` setting,
   worked out from how it is set going when left on auto), paths that name the door or lift at
   each corner with a filter for movers that won't open, `client.show_navmesh`, and View > Show
   navigation mesh in the editor, rebuilt on a worker thread as the level changes. Left for later:
   meshes for agents of other sizes (one per enemy size class, step 6 on), links for dropping off
   ledges and for jumps, and drawing the mesh in the orthographic panes.* Recast (zlib licence)
   builds it from the level's collision when the GameLevel is built; Detour finds paths. Doors and lifts are links that open and close with
   their movers. The mesh is drawn on request in the game and in the editor, and building a test
   level's mesh is checked by tests.
6. **Enemies.** In three parts. *6a done (2026-10-22): enemy kinds as data (`enemies/<name>.cfg`,
   listed in `game.cfg`: health, size, eye height, speed, turning, sight, reaction time, melee
   damage, reach, wind-up, interval and knockback, corpse time), the `enemy` class (kind chosen
   from the game's list, on_alert, on_death, an alert input), every body (players and enemies)
   pushed, carried, hurt and hit by its own box, an AI that sees players in a view cone with line
   of sight, turns on whoever hurts it, chases along the navigation mesh through doors and on
   lifts, and strikes in reach (the charger), enemies drawn as boxes, and
   `levels/enemies_test.level`. 6b done (2026-10-22): players and enemies solid to each other,
   hearing shots and running (with an ambush setting for the deaf), and the gunner: a weapon from
   the game's list fired through the weapon code in bursts, with aim error, from a distance it
   keeps, sidestepping where there is floor. 6c done (2026-10-22): patrols (a `patrol_point` class
   naming the next point, with a wait and on_arrive; an enemy's `patrol` names the first; routes
   drawn in the editor, which adds the next point with one button), searching (it follows a lost
   target round the corner, searches spots about where it lost it, then walks back to its post or
   patrol), and cover for the gunner (after a burst or a hit, by chance, it runs to a nearby spot
   the target can't see, waits, and comes out again). 6d done (2026-10-22): enemy sounds of their
   own kind (`<kind>_alert`, `_pain`, `_death`, `_footstep` and `_swing`, else the plain sound), a
   spark off an enemy's head as it notices you, bodies flashing when hurt, editor markers at each
   kind's size, and a view of what enemies think (`client.show_ai`, and Play > Show what enemies
   think in the editor): state, health, view cone, hearing ring, path, goal, target and post.* Enemy types are data: health, size, speed, senses, attacks and behaviour tuning.
   Enemies move with the player movement code, driven by commands from their AI, so they step,
   slide and ride lifts the way players do. They see players inside a view cone with line of
   sight, and hear shots and footsteps. Behaviour is a state machine (idle, patrol, alerted, chase,
   attack, strafe or take cover, search) whose tuning is data. on_alert and on_death events go to
   the wiring. Two kinds to start: a melee charger and a ranged gunner.
7. **Encounters.** An encounter entity runs waves from spawner entities: how many at once, and
   when the next wave starts. It locks the arena through the wiring (on_start closes the doors,
   on_cleared opens them). *Done (2026-10-22): `spawner` (kind, encounter, wave, count, hunt; a
   spawn input; on_spawn, on_death) and `encounter` (max_alive, next_wave_at, wave_delay,
   start_on; start and stop; on_start, on_wave, on_cleared), and
   `levels/encounter_test.level`. The music entity is put off: Tony means to bring in FMOD or an
   open-source equivalent, which will decide how music and sound are driven.*
8. **Script entity (Luau).** Takes inputs and fires outputs through the same wiring, and keeps its
   state in components, so it is saved and rewound. It is sandboxed for maps downloaded from
   servers: no I/O, an instruction and memory budget per call, and deterministic maths only.
   *Done (2026-10-23): `engine/script` (Luau 0.741 from vcpkg), the `script` class naming
   `scripts/<name>.luau`, scripts that declare state, events, inputs and a start function, and a
   `game` table to fire events, send inputs by name, call themselves later and draw random
   numbers. Each call runs in a fresh state in a 4 GB-aligned arena, so the same calls allocate at
   the same offsets and tables iterate in the same order on every machine, with 200,000 steps and
   4 MB per call; the `^` operator, inexact maths functions and the optimize comments are
   rejected when a script loads. State is kept as doubles in the `Script` component (the schema
   gained a double type), and a call's effects happen only if it finishes. The editor's inspector
   offers each script's events and inputs for wiring, `docs/Scripting.md` describes scripts for
   level designers, a fuzz target runs scripts, and `levels/script_test.level` is a combination
   lock. Left for later: trigonometry with results of our own making, so scripts can turn and
   aim; queries of the game (who is where, what is alive), when a level needs them. 8b done
   (2026-10-23), after Tony asked to edit scripts in the editor and see which entities each is
   tied to: a Scripts window with a code editor of our own (Luau colours, line numbers, undo,
   find, the clipboard), scripts checked as they are typed with errors on their lines, unsaved
   text used by the inspector and by play, new scripts from a template (also from a script
   entity's inspector), and beside the code the entities running it (outlined in the view), the
   wiring in and out, and what doesn't line up: inputs or events the script lacks, game:send to
   names the level doesn't have, inputs nothing sends.*
9. **First playable arena.** *Dropped (2026-10-23): Tony chose to go on to making entities and
   their behaviour in the editor, which the arena would have been built with. `encounter_test`
   and `script_test` stand as M3's encounter and scripted level.*

## M4 plan

Agreed 2026-10-23. Tony's aim: the charger and gunner are templates, and new entities should be
made from scratch in the editor, their properties and behaviour included. Entity authoring comes
first; the rest of the editor tools follow. In order; each step builds, tests and commits on its
own, and everything a step adds to play is simulation state, so it saves, rewinds and replicates.

1. **Entity types.** *Done for enemies (2026-10-23).* An enemy type is its settings file, which
   the Game data window makes by copying another (the charger and gunner to start with), edits
   with undo and saves into the game's content folder; changes apply at once, in play too. A type
   also has a description, the colour of its box and the sound set it uses. Types are listed in
   Create > Enemy and the inspector, and levels name them in `type` as before. The same window
   already edits weapons, movement and rules (most of step 6). Pickup types wait: presets and each
   pickup's own properties cover them for now. Props wait for M5's models. A type's behaviour is
   step 3, and a script on a type comes with it, as a behaviour node.
2. **Per-entity overrides.** *Done (2026-10-23).* Any number setting of an enemy type, and its
   weapon, can be set on one placed enemy or spawner (a calmer gunner here, a faster charger
   there) without a new type: a property with the setting's key, edited in the inspector's "Its
   own settings". Each enemy keeps them in an `EnemyOverrides` component (up to 16), laid over its
   type's settings whenever they are read, so tuning the type still reaches the rest. The look
   (description, colour, sounds) stays the type's.
3. **Behaviour runtime.** *Done (2026-10-23).* Behaviour trees (sequence, fallback, parallel;
   invert, succeed, cooldown, time limit; has target, target visible, in reach, in range, within,
   health below, chance; see, hear, react, take cover, strike, chase, keep distance, strafe, fire
   a burst, search, patrol, wait) run in the simulation each tick, read again from the root,
   deterministic, with no per-tick allocation and each enemy's running state in a fixed-size
   component. Trees are text files, `behaviours/<name>.behaviour` (fuzzed), named by each enemy
   type, and documented for designers in `docs/Behaviours.md`. The charger and gunner share one
   standard tree, in which their settings decide what each does, and play as before (the
   existing enemy tests pass unchanged). A Luau script as a node, for an odd custom decision,
   came with step 4.
4. **Behaviour graph editor.** *Done (2026-10-26).* The Behaviours window
   (imgui-node-editor, MIT, from vcpkg) shows a tree laid out left to right as its file reads,
   and builds and edits it: nodes added by dragging from a node's edge, moved by linking,
   reordered by dragging, changed through their menu, copied and pasted as text. Edits go
   through the Game data window's undo and saving and play at once. Trees still being built open
   as drafts with their problems on the nodes. In play, the watched enemy's running nodes light
   up. A `script` node asks a behaviour script (`scripts/<name>.luau` with just a `decide`
   function) about the enemy and its target; it answers yes or no and may send inputs to the
   level's entities, and keeps no state (Tony's choice, 2026-10-26). Each call is a fresh
   sandbox, about 10 microseconds, so the docs say to put cheaper conditions first.
5. **Visual logic graph.** The level's wiring as nodes in the same graph window, with the outputs
   table as the text form.
6. **Game data for the rest.** Weapons, movement and rules are already edited and tuned live in
   the Game data window (step 1); still to come are new weapons made from others.
7. **The remaining editor tools.** Texture and material assignment with face alignment, editable
   level-geometry meshes, movers along paths and turning movers, the asset browser and hot reload
   of textures, materials, shaders and levels. Planned in detail when step 6 is done.

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
| Behaviour authoring | Our own behaviour-tree runtime in `engine/sim`, authored as graphs in the editor (2026-10-23). Existing libraries (BehaviorTree.CPP; its Groot2 editor isn't open source) keep running state in heap objects, allocate as they run and use wall-clock time, which breaks saves, rewinds and determinism. Only the graph widget is borrowed: imgui-node-editor |
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
