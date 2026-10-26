# Wiring

A level's logic is wiring between entities: when something happens to one entity (a player walks
into a trigger, a door finishes opening, a counter reaches its count), it sends an input (open,
enable, add) to every entity with a name, now or after a delay. Triggers, relays, counters,
timers, movers, pickups, enemies, spawners, encounters and script entities all take part, so most
level logic needs no script at all.

Each wire is an output of the entity it starts from: an event of that entity, the name of the
target, the input it sends and a delay. An output reaches every entity with the target name, so
two doors named `gate` open together.

## The Logic window

In the editor, the Logic window shows the wiring as a graph. Each entity is a node with its inputs
down its left side and its events down its right side; each output is a link from the event to
the input of every entity it names, with its delay written halfway along.

- Drag from an event to an input (or from an input to an event) to wire them. A target without a
  name is given one (`mover_1`), since outputs find their targets by name.
- Click a link to see it beside the graph: change its event, its input or its delay there, or
  take it away. Delete takes the chosen links away; with no link chosen, it deletes the chosen
  entities.
- Choosing a node chooses its entity in the level, and choosing an entity in the view or the
  hierarchy chooses its node. Double-click a node to show its entity in the view.
- At first the graph lays itself out by the wiring: what starts things on the left, what they set
  off to the right. Drag nodes wherever reads best: each keeps its place, saved in the level (as
  the entity's `graph` line, which play ignores). New entities are laid out below the ones you
  have placed. Tidy up lays everything out again, as one undo.
- It shows the entities that are wired to something; tick "Every entity with events or inputs"
  for the rest too. F shows what is chosen, or everything.

Mistakes show in red: an output naming no entity, an event the entity doesn't have, or an input
its target doesn't take (after a script entity's script changed, say). The level still plays;
such wires do nothing, and play warns about them when it starts.

## The outputs table

The inspector's outputs table is the same wiring as text: the chosen entity's outputs, a row
each, with a last row for adding one. Use it to type a name for a target that doesn't exist yet,
or to wire to several entities by a name they share.
