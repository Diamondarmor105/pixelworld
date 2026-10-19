---
doc: prd
status: approved
---

# Pixel World (working title — learner has not chosen a name yet) — Product Requirements

A 2D side-view pixel sandbox where every pixel is a material with its own behavior, so the player can explore endless generated terrain, set it on fire, blow it apart, and melt it with acid.
Source: `scope.md > The Unique Kernel`, `scope.md > The Core Loop`, `scope.md > POC Boundary`.

## The Core Journey

1. The player opens the program. A world appears — the player is already standing on generated ground in a surface forest: packed dirt on top, foliage and trees, occasional ponds.
2. The player walks right or left and jumps. The camera follows them. New terrain is generated ahead as they go, in both directions, so the world never runs out.
3. The player encounters materials and structures: trees and foliage, wooden and stone ruins, sand and gravel pockets, water pools, and — if they go deep enough — oil pockets, lava pools, and sulfur crust.
4. The player switches tools with number keys (1, 2, 3) and aims with the mouse. Clicking uses the current tool.
5. **Spark Wand:** fires a projectile that travels until it hits something solid, then spawns a cluster of fire. Fire ignites nearby flammables (wood, bark, foliage, oil), which burn and spread.
6. **Dynamite:** thrown, arcs through the air, lands, and triggers a circular explosion. Dirt, sand, and gravel are torn out and thrown; stone mostly resists.
7. **Acid Flask:** thrown, arcs, shatters on impact, and splashes a pool of acid that eats through material and carves tunnels.
8. Materials react to each other by proximity, with no player involvement: lava touching water becomes stone and consumes the water; lava or fire next to snow or glacial ice turns it to water; burning organics become ash; oil ignites fiercely; sulfur touching lava explodes in a small crater; water soaking into dirt makes mud; water washes ash away.
9. Danger exists. Standing in lava or fire, or getting caught in an explosion or acid, drains the player's health. At zero health the player dies.
10. After death the player respawns at the starting area. **The world is exactly as they left it** — the tree they burned is still ash, the hole they blasted is still there, the lava pool is still hot.
11. The player keeps exploring, burning, and blowing things up for as long as they like. There is no goal, no score, and no ending.

## Screens and Layout

There is one surface: the game view itself. No menus, no title screen, no settings — the program starts in the world.

- The world fills the window, drawn with **large, chunky pixels** so individual grains of sand and drops of water are visible as they move.
- The camera follows the player horizontally (and vertically as they descend into caves), so the world scrolls past.
- A minimal HUD sits over the world: a **health bar**, and an **indicator showing the currently selected tool**. Nothing else.
- The mouse cursor doubles as the aim point for tools.

## Look and Feel

Direction from the learner (they explicitly want this, so it shapes the build):

- **Bright and friendly**, not dark or moody. Explicitly *not* copying Noita's dark earthy palette.
- **Chunky pixel scale** — pixels are large enough that individual grains of sand and drops of water are readable in motion. Seeing the simulation work is the whole point, so the zoom level is generous.
- **Natural, readable material colors**, each material visually distinct at a glance: greens for foliage, browns for dirt and bark, greys for stone, bright blues for water and ice, hot orange-red for lava and fire, black for oil, pale yellow-green for acid, greys for ash.
- Pixels are the only art direction: no sprites, animation, or lighting effects needed beyond each material's color.
- Avoid generic UI chrome; the HUD should be small and unobtrusive.

## Features and Behavior

### The world, generation, and the camera

- The world is a 2D pixel grid, endless horizontally, generated in chunks ahead of the player as they move (`scope.md > POC Boundary`).
- Terrain has a surface layer: packed dirt topped with foliage, trees and foliage of varied shapes, sometimes giant trees, and scattered surface ponds of water.
- Below the surface: hills and mountains at the surface, caves carved underneath, with clay, sand, and gravel pockets filling cave ceilings and floors, water pools, and hidden oil pockets deeper down.
- Each chunk includes hand-authored structures placed by generation — ruins (wood and stone), trees, and smaller foliage — **each with several variations** so the world doesn't feel repetitive.
- **Simulation is limited to on-screen pixels.** Off-screen terrain is generated and held frozen until it comes into view. This is what makes an endless world affordable.
- The player cannot fall out of the bottom of the world.

### Materials and how they behave

Materials are defined by behavior categories, not individually hand-coded:

| Category | Materials | Behavior |
| --- | --- | --- |
| Solids | Stone, Bedrock, Limestone, Basalt Columns, Obsidian, Bones | Stay in place; resist explosions (harder than dirt); do not fall. |
| Loose solids | Soil, Packed Dirt, Sand, Gravel, Clay, Snow | Fall when unsupported and pile up with a natural slope. |
| Liquids | Water, Lava, Oil, Acid, Mud | Flow, spread, and pool in cavities according to weight. |
| Flammable organics | Wood, Bark, Foliage, Mycelium, Fungal Cap | Catch fire, burn, spread fire to neighbors, and turn to Ash. |
| Meltable | Snow, Glacial Ice, Mud, Clay | Turn to Water when lava or fire is adjacent (no temperature tracking). |
| Byproduct | Ash | Light and crumbly; washes away in water. |

**Proximity reactions** (no player action required):

- Lava + Water → Stone, consuming the water. Large volumes of lava near water → Obsidian instead.
- Lava or Fire + Snow / Glacial Ice → Water (melts, creating flows).
- Lava or Fire + Wood / Bark / Foliage / Mycelium / Fungal Cap → Fire spreading, then Ash.
- Lava or Fire + Oil → ignites fiercely and sets adjacent flammables alight.
- Lava + Sulfur Crust → sudden ignition and a small explosion, carving a crater.
- Water + Soil or Packed Dirt → Mud (softens, then flows).
- Water + Ash → dissolves and clears the ash.

There is **no temperature system**: reactions are triggered by what is adjacent, not by tracked heat. Nothing cools down.

### Tools

Number keys select, mouse aims, click uses (`scope.md > POC Boundary`).

- **Spark Wand** — fires a projectile that travels until it hits something solid, then spawns a cluster of fire pixels with a small heat burst, igniting nearby oil and other flammables.
- **Dynamite** — a thrown object that arcs through the air, lands, and triggers a circular explosion: material inside the radius is removed and thrown outward. Stone resists more than dirt, sand, and gravel.
- **Acid Flask** — a thrown object that arcs, shatters on impact, and splashes a pool of corrosive acid that carves tunnels through hard rock.
- Thrown objects use **simple motion** — arc, land, trigger. Rotation, tumbling, and stacking are deferred (see below).

### The player, health, and death

- The player is a small figure with gravity that walks and jumps, and stands on solid material rather than falling through it.
- Health drains from contact with lava, fire, explosions, and acid. The health bar visibly drops.
- At zero health the player dies and respawns at the starting area.
- The world is **never reset**: destroyed terrain, burnt structures, spilled acid, and flowing water all persist across deaths.

### What the player sees when they succeed

- **Fire:** they light a wooden ruin and the fire visibly creeps along the wood into nearby foliage, and up into a tree, leaving burnt ground and ash behind.
- **Explosion:** they blast a cave wall and see dirt and sand thrown outward while stone mostly holds.
- **Acid:** they throw a flask at a hard wall and watch it carve a tunnel over the following seconds.
- **Lava meets water:** they break into a water pool near lava and the lava crusts over into stone while the water drains away.
- **Exploration:** they walk for a while and new terrain keeps arriving, with structures appearing at irregular intervals.

### Acceptance criteria

- [ ] Fire lit on wood or foliage spreads to adjacent flammable pixels without further clicks, and burned-out areas are left as Ash.
- [ ] Sand and gravel dropped or unsupported fall and form sloped piles rather than floating or forming flat towers.
- [ ] Liquids flow downward and sideways and pool in cavities instead of passing through solid material.
- [ ] Lava adjacent to water becomes Stone and the water is consumed; when the lava source is large or deep the result is Obsidian instead.
- [ ] Snow or Glacial Ice adjacent to lava or fire becomes Water.
- [ ] Water adjacent to Soil or Packed Dirt produces Mud.
- [ ] Water adjacent to Ash dissolves it.
- [ ] Oil adjacent to fire or lava ignites and burns more aggressively than wood.
- [ ] Sulfur Crust adjacent to lava produces a small explosion and leaves a crater.
- [ ] The Spark Wand projectile stops at the first solid material and spawns fire there.
- [ ] Dynamite arcs, lands, and produces a circular explosion that removes and throws dirt/sand/gravel, leaving stone mostly intact.
- [ ] The Acid Flask arcs, shatters, and produces acid that visibly carves into hard material.
- [ ] Walking in one direction for a sustained stretch produces continuous new terrain, and returning to a previously visited area shows it unchanged apart from what the player did to it.
- [ ] Off-screen terrain does not simulate: leaving a fire burning and walking away stops its spread until the player returns.
- [ ] Player health drops on contact with lava, fire, explosion, or acid, and reaches zero.
- [ ] Dying respawns the player at the starting area with the world unchanged from the moment of death.

## States and Boundaries

- **First use** — the program opens directly into the world, player standing on ground in the surface forest, full health, a default tool selected. No tutorial or intro screen.
- **Nothing to show** — not applicable: the world always exists and is generated on open.
- **Damage** — the health bar visibly drops as damage is taken. *(Assumption to confirm: the player flashes or otherwise shows they are being hurt — see Open Questions.)*
- **Death** — the player dies at zero health and respawns at the starting area. *(Assumption to confirm: health returns to full on respawn.)*
- **Persistence between sessions** — none. Closing the program discards the world. Saving and loading is deferred.
- **Boundary: what the player cannot do** — they cannot mine or place materials by hand, cannot build, and cannot pause or save. All interaction is via the three tools.
- **Boundary: off-screen simulation** — pixels outside the view are frozen. Anything the player ignores stops happening, and resumes when it returns to view.

## Product Decisions

- **Endless world with frozen off-screen pixels** — the learner chose this over the fixed-size world approved in `scope.md`, and `scope.md` was updated to match. Reason: exploration should keep going, and only nearby pixels simulating is the affordable way to have that.
- **Health drains; death respawns the player at the starting area; the ruined world is never reset** — learner's decision. Permanent destruction makes the player's messes meaningful.
- **No temperature system** — the learner clarified that reactions are proximity rules only (lava/fire next to ice turns it to water; no cooling stone). Reason: they wanted the *effect*, not a heat simulation.
- **Rigid bodies: "Version A now, grow toward Version B"** — thrown objects arc, land, and trigger; true rotation, tumbling, and stacking are deferred. Reason: full rigid bodies are the single biggest system in the project and would not fit the timeframe.
- **Biome variety deferred as a group** — five named regions is more generation work than the demo needs. The glacier peak is the first to go. The *materials* (snow, glacial ice, basalt, obsidian, sulfur, bones) stay available in smaller amounts so nothing looks out of place. Learner's words: "nice to have."
- **Materials are data-driven** — one behavior engine with materials described by properties, so the full 24-material list is affordable and adding more later is cheap. Learner's stated wish: "the more materials you can add, the better."
- **Three tools: Spark Wand, Dynamite, Acid Flask** — number keys select, mouse aims and fires. Learner's decision.
- **HUD is a health bar plus a current-tool indicator**, nothing more.
- **Chunky pixel scale, bright and friendly palette** — learner's choice, so individual grains and drops are visible.
- **Cave-ins come free from pixel physics** — unsupported loose material falls, so collapsed tunnels need no separate system.
- **No hand mining or building** — learner's decision: "all the interactions will come from tools."

## What We're Building

- An endless, chunk-generated 2D pixel world with a surface forest, hills and mountains, caves underneath, cave-fill material pockets, water pools, oil pockets, lava pools, and sulfur crust.
- Hand-authored structures (ruins, trees, foliage) with several variations each, placed by generation on the surface and in caves.
- A data-driven material engine covering the 24 named materials through behavior categories, including gravity, flow, burning, melting, dissolving, and solidification.
- Proximity reactions as listed above.
- A player with gravity, walking, jumping, a draining health bar, death, and respawn at the starting area with a persistent world.
- A camera that follows the player, and simulation limited to on-screen pixels.
- Three tools: Spark Wand, Dynamite, Acid Flask, with simple thrown-object motion.
- A minimal HUD: health bar and current-tool indicator.

## Deferred From the POC

- **True rigid bodies** — tumbling, rotating, stacking objects, and rock chunks crashing down as physics objects. Reason: a separate engine from falling-sand pixels; the largest risk to the timeframe. Version A covers the demo; the code should leave room to grow.
- **Biome variety** — the volcanic depths and glacial peak as distinct, named regions with their own generation rules. Reason: more generation work than the demo proves; the materials still appear in smaller amounts. Likely first upgrade once the core works.
- **Saving and loading a world.** Reason: not needed to prove the idea; closing the program discards the world.
- **More structure variations** beyond "several each." Reason: nice-to-have polish; the base set already prevents repetition.
- **More materials and a fourth tool** (a water pourer or a freeze tool were floated). Reason: only after the core works.

## Possible Later Enhancements

- Rigid bodies proper: everything in Version B, including cave-ins as tumbling debris.
- The volcanic depths and glacial peak as full regions, with lava vents and sheer ice cliffs.
- More materials (acid as a natural pool material, smoke, gases) and more tools.
- Saving and loading, so a world can be revisited.
- More structure types and larger, rarer set pieces.

## Non-Goals

- **Polished art, animation, or lighting effects** — effort goes into simulation behavior; materials are drawn as colored pixels. Source: `scope.md > Explicitly Cut`.
- **Sound and music** — not needed to prove the idea.
- **Menus, settings, pause, or a title screen** — the program starts in the world.
- **Enemies, combat, scoring, or a win condition** — the only threat is the world itself. (Player health and death are in scope; nothing attacks the player.)
- **Multiplayer** — out of reach for the timeframe.
- **Hand mining, digging, or building** — all interaction is via tools.

## Open Questions

- **The project name** — `Pixel World` is a placeholder. Not blocking: can be chosen before the spec is written or any time before publishing.
- **Damage feedback and respawn details** — does the player visually show being hurt (flash, knockback), and does health return to full on respawn? *Assumption on the table: yes to both.* Should be settled before `4-spec`, since it affects what gets built.
- **How death is presented** — a brief on-screen indication, a fade, or an instant teleport back to the start. *Assumption on the table: a brief, simple indication, then respawn.* Can be settled in `4-spec`.
- **Whether the Dynamite and Acid Flask are visible objects in flight** — *assumption: yes, drawn as a small colored object so the throw reads on screen.* Low stakes; can be settled during the build.
