# Behaviours

An enemy decides what to do with a behaviour tree: a tree of small nodes, read from the top every
tick (128 times a second). Each enemy type names its tree with the `behaviour` setting in its
file (the Game data window shows it under Behaviour); a type that names none, or one that won't
load, uses the standard tree, which the charger and gunner both use.

Trees are text files, `behaviours/<name>.behaviour`, through the content layers like everything
else, so a game can have its own or replace the engine's. The editor's Behaviours window shows
and edits them as graphs (see below), and a file can be written by hand too.

## How a tree runs

Every node, when it runs, **succeeds**, **fails** or is still **running**.

- **Composites** choose which of their children run:
  - `sequence` runs its children in turn while each succeeds: "do this, then this".
  - `fallback` tries its children in turn until one doesn't fail: "try this, or else this".
  - `parallel` runs all its children every tick, and does what its first one does; the others
    act beside it (chasing while shooting).
- **Decorators** change what their one child does: `invert`, `succeed`, `cooldown`,
  `time_limit`.
- **Conditions** ask a question: `has_target`, `target_visible`, `health_below 0.3`...
- **Actions** do something: `chase`, `strike`, `fire_burst`, `patrol`...

The whole tree is read again from the top every tick. So a condition at the start of a sequence
keeps being checked: the moment it stops holding, the sequence fails and something else takes
over. An action carries on from tick to tick (a chase keeps its path, a search its spot) for as
long as it keeps being reached.

The settings of the enemy's type (and any it has of its own) say how well it does each thing:
`strike` swings for `melee_damage`, `fire_burst` shoots in bursts of `burst`, `take_cover` goes
by `cover_chance`. An action whose setting gives the enemy nothing to do fails: a type with no
weapon never fires, one with no `melee_damage` never swings.

## The file

One node a line. A node's children are the lines under it indented further (with spaces, the same
amount for each child). A node's setting, if it takes one, follows its name: `wait 2`. `#` starts
a comment, which belongs to the node below it (or the one on its line).

The standard tree, `content/behaviours/standard.behaviour`:

```
sequence
    # Calm, it watches and listens for players.
    see
    hear
    fallback
        sequence
            has_target
            fallback
                react
                take_cover
                strike
                # Fights at a distance with a weapon.
                parallel
                    chase
                    keep_distance
                    strafe
                    fire_burst
                search
        patrol
```

Read from the top: it always looks and listens. Then, if it is after a player, it reacts (for a
moment after first noticing them), or hides (if it just took a hit or ended a burst and decides
to), or swings (if the player is in reach), or chases them, keeping its distance, sidestepping and
shooting as it goes; once it has lost them, it searches. With no one to fight, it patrols.

A tree has at most 128 nodes, 32 deep, and 16 nodes that remember something (`cooldown`,
`time_limit`, `chance`, `wait`). A tree that breaks a rule doesn't load, and a warning names the
line.

## The Behaviours window

In the editor, the Behaviours window shows a tree as a graph read left to right, as the file reads
top to bottom: each node's children stand in a column to its right, run in order from the top.
The Game data window's "Edit behaviour" on an enemy type opens that type's tree.

- Drag from a node's right edge into empty space to add a node under it, or from its left edge
  to put one above it (wrapping it in a decorator or composite).
- Drag from a node's right edge onto another node to move that node, and what is under it, there.
- Drag a node up or down to reorder it among the nodes beside it.
- Right-click a node to add under it, put above it, change it to another kind, take it away
  keeping what is under it, move it up or down, copy, cut, paste, duplicate or delete it.
- Delete removes the chosen node and what is under it. Ctrl+C, X, V and D copy, cut, paste and
  duplicate, as the file's text, so trees can be pasted to and from these docs.
- The panel on the right edits the chosen node's setting and note (the note is written as
  comments above it), and with nothing chosen lists the tree's problems.

Every change applies at once, in play too, and is undone, redone and saved with the Game data
window's changes (Ctrl+Z, Ctrl+Y and Ctrl+S while the window has the focus). A change to the
engine's tree is saved as the game's own copy, which "Back to the engine's" takes away.

A tree that breaks a rule (a sequence with nothing under it yet, say) still opens, with its
problems on its nodes in red; until they are put right, the enemy types that use it run the
standard tree.

While playing in the editor, the nodes the watched enemy ran on its last tick light up: green
for those that ran (succeeded or are still running), orange for those it tried that failed. It
watches the enemy nearest the middle of your view, or the one chosen beside "Watching".

## Making a new one

1. In the Behaviours window, choose the tree to start from and press "New from this...". It is
   saved as `games/<game>/content/behaviours/<name>.behaviour`.
2. In the Game data window, set the enemy type's `behaviour` to `<name>` (or make a new type from
   the gunner first, so the gunner stays as it is).
3. Change it, and play. Problems with the file are in the console too.

Some ideas:

```
# A coward: fights until badly hurt, then runs back to its post.
            sequence
                has_target
                invert
                    health_below 0.3
                ...
```

```
# Hesitant: stands a second before every chase.
            sequence
                has_target
                wait 1
                chase
```

```
# Gives up after ten seconds of chasing.
                time_limit 10
                    chase
```

## Script nodes

For a decision no other node makes, a `script` node asks a Luau script (`scripts/<name>.luau`,
written like the level's scripts in [Scripting.md](Scripting.md)). The script returns a table
with just a `decide` function, which answers true (the node succeeds) or false (it fails):

```lua
-- Flanks only while a lone target is close and hurt.
return {
    decide = function(game, me)
        local target = me.target
        if target == nil or not target.visible then
            return false
        end
        return target.distance < 15 and target.health < 50 and me.health > me.max_health / 2
    end,
}
```

```
            sequence
                has_target
                script flank
                strafe
```

In the Behaviours window, the chosen script node's panel picks its script, opens it in the
Scripts window (**Edit**), or makes a new one from a template (**New**). The script plays as it
is being edited, saved or not, from the next start of play.

`me` is the enemy whose tree reached the node:

| | |
|---|---|
| `me.type` | Its type's name, such as `"gunner"` |
| `me.state` | What it is doing: `"idle"`, `"alerted"`, `"chase"`, `"attack"`, `"lost"`, `"search"`, `"cover"` or `"patrol"` |
| `me.state_time` | Seconds since it went into that state |
| `me.health`, `me.max_health` | Its health, and its type's |
| `me.hurt_time` | Seconds since it was last hurt; -1 if never |
| `me.position`, `me.yaw` | The middle of its box (`.x`, `.y`, `.z`, in metres), and the way it faces in degrees |
| `me.post` | Where it stands guard (its feet) |
| `me.ambush` | Whether it lies in ambush |
| `me:setting(name)` | Any number setting of its type, as this enemy has it: `me:setting("speed")` |
| `me.target` | While it is after a living player, that player; otherwise `nil` |
| `me.target.position`, `.velocity` | The middle of their box, and how fast they move (m/s) |
| `me.target.health`, `.armour` | |
| `me.target.distance` | Metres between the two |
| `me.target.visible`, `.in_reach`, `.in_range` | As `target_visible`, `target_in_reach` and `target_in_range` say |
| `me.target.last_seen`, `.seen_time` | Where it last saw them, and how many seconds ago |

`game.time`, `game:random()` and `print` work as in any script, and `game:send(name, input [,
seconds])` sends an input to the level's entities with that name (an alarm, a door), as if from
the enemy's own wiring; the input is sent only if `decide` finishes. A behaviour script keeps no
state, and has no events or inputs: the tree's own nodes (`cooldown`, `wait`, `chance`) do the
remembering.

A script node costs far more than any other node, about 10 microseconds each time it is asked,
which is every tick its tree reaches it. Put cheaper conditions before it, so it is asked only
when it matters. If its script is missing, doesn't load or fails, the node fails; the window shows
why on the node, and errors and what the script prints reach the console (each only once in a
row, as the node is asked every tick).

## Nodes

| Node | Setting | What it does |
|---|---|---|
| `sequence` | | Runs its children in turn while each succeeds: fails when one fails, runs while one runs, succeeds when all have |
| `fallback` | | Tries its children in turn until one doesn't fail, and does what that one does; fails if all do |
| `parallel` | | Runs all its children every tick and does what its first does |
| `invert` | | Success is failure and failure success |
| `succeed` | | Failure is success, so a sequence carries on past it |
| `cooldown` | seconds (2) | After its child succeeds, fails for this long without running it |
| `time_limit` | seconds (5) | Fails once its child has run this long without a break |
| `has_target` | | Whether it is after a living player |
| `target_visible` | | Whether it can see that player |
| `target_in_reach` | | Whether that player is in reach of a swing, and in sight |
| `target_in_range` | | Whether it has a weapon and that player is in sight within `attack_range` |
| `target_within` | metres (5) | Whether that player is within this distance |
| `health_below` | share (0.5) | Whether its health is below this share of its type's |
| `chance` | share (0.5) | Succeeds with this chance, decided when first reached and kept while it keeps being reached |
| `script` | a script's name | Asks the script's `decide` function (see Script nodes above); succeeds if it says yes |
| `see` | | Calm or searching, notices the nearest player in its view cone. Always succeeds |
| `hear` | | Calm, searching or having lost its target, notices players shooting or running nearby (unless it lies in ambush). Always succeeds |
| `react` | | Just after noticing a player, stands and turns to them for `reaction_time`; fails otherwise |
| `take_cover` | | Just hurt in a fight or done with a burst, may (`cover_chance`) run somewhere the target can't see and stay `cover_time`; fails when not hiding |
| `strike` | | With the target in reach, swings at it; succeeds as a swing lands, fails out of reach |
| `chase` | | Goes after the target, or to where it was last seen; fails once there (or long after) without finding it |
| `keep_distance` | | Chasing with the target in range, stops closing in at `keep_distance` |
| `strafe` | | Chasing with the target in range, sidesteps one way then the other (`strafe_time`) |
| `fire_burst` | | Chasing with the target in range, shoots in bursts |
| `search` | | Having lost its target, looks about where it was last seen for `search_time` |
| `patrol` | | Forgets its target and walks its patrol, or back to its post; always running |
| `wait` | seconds (1) | Stands still this long, then succeeds |

`client.show_ai 1` (Play > Show what enemies think in the editor) labels each enemy with what it is
doing. Each enemy also records which nodes ran each tick, which the Behaviours window lights up.
