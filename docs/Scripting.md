# Scripting

A script entity runs a Luau script, for logic that wiring alone makes awkward: a combination lock,
a puzzle with a time limit, a sequence that picks something at random. It plugs into the same
wiring as every other entity, so triggers, doors, spawners and encounters reach it and it reaches
them.

## Making one

1. In the editor, Create > Script, then in the inspector press **New script...** and give it a
   name (lowercase letters, digits, `-` and `_`). That makes `scripts/<name>.luau` in the game's
   content folder from a template, sets the entity to run it and opens it in the Scripts window.
   (Or choose an existing script in the inspector, and press **Edit script**.)
2. Write it in the Scripts window. It is checked as you type: a line that stops it loading is
   marked red with the reason. Beside it are:
   - the entities that run it (outlined in the view; click one to select it),
   - what it declares,
   - the wiring into and out of those entities,
   - what doesn't line up: inputs or events the script doesn't have, `game:send` to a name no
     entity has, inputs nothing sends. Those with a line are marked on it too.
3. Wire it in the outputs table like any entity: its events and inputs are listed there.
4. Play. What is in the Scripts window plays, saved or not. Ctrl+S saves the script; errors and
   what it prints appear in the console.

Scripts can be written in another editor too (**Open outside**, for VS Code with the Luau
extension, say): the Scripts window follows the file as long as it has no unsaved changes of its
own.

`content/scripts/combination.luau`, played in `--map script_test`, is a complete example.

Scripts also make decisions for enemies: a behaviour tree's `script` node asks a script that
returns just a `decide` function. [Behaviours.md](Behaviours.md) says how.

## What a script returns

```lua
local ORDER = { "left", "right", "middle" } -- locals and helper functions are fine

return {
    -- Kept between calls, and saved with the game. Numbers and true/false only, at most 16.
    state = { step = 0, solved = false },

    -- Events it fires, which outputs wire to other entities.
    events = { "on_step", "on_solved" },

    -- Inputs other entities' outputs send it. Each is a function given the game.
    inputs = {
        press = function(game)
            game.state.step += 1
            game:fire("on_step")
        end,
    },

    -- Optional: called once, as play begins.
    start = function(game) end,
}
```

## The game

| | |
|---|---|
| `game.state` | The declared values. Change them; they must stay the kinds they started as. |
| `game.time` | Seconds since play began. |
| `game:fire(event)` | Fires one of its events through its wiring. |
| `game:after(seconds, input)` | Sends one of its own inputs to itself later: timers and time limits. |
| `game:send(name, input [, seconds])` | Sends an input to every entity with that name, now or later. |
| `game:random()` | A number from 0 up to 1. `game:random(m, n)`: a whole number from m to n. |
| `print(...)` | Writes a line to the console. |

## Rules

- **Only the declared state survives a call.** Every call starts afresh, so a global or a local
  set in one call is gone by the next. Keep what matters in `game.state`.
- **Names are checked when they can be.** `game:fire("on_x")` or `game:after(1, "tick")` naming
  an event or input the script doesn't declare stops it loading; `game:send` to a name no entity
  has is a warning. Names worked out while the game runs are only checked when the call happens.
- **A call that fails does nothing.** If it errors, it changes no state and fires nothing, and the
  error is logged with the script's name and line.
- **A call has a budget**: 200,000 steps (loop turns and function calls) and 4 MB of memory. A
  call that goes over fails; scripts react to events, they don't run every frame (behaviour
  scripts, asked every tick, should be quick).
- **The same everywhere.** The game must play the same on every machine, so a script can't use
  what differs between them: the `^` operator (multiply instead), maths functions other than
  `abs`, `ceil`, `clamp`, `floor`, `fmod`, `lerp`, `max`, `min`, `round`, `sign` and `sqrt` (and
  `math.pi`, `math.huge`), `math.random` (use `game:random`), or the `--!optimize` and
  `--!native` comments. These are errors when the script loads.
- **No files, no operating system, no loading code**: `os`, `io`, `require`, `loadstring`,
  `getfenv`/`setfenv` and `debug` aren't there. Levels downloaded from servers can carry scripts,
  so scripts can do nothing but play the level.
