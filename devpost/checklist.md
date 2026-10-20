---
doc: checklist
status: approved
---

# Build Checklist

Build mode: fast

## Slices

- [x] **1. A window opens and shows a chunky pixel world**
  Becomes usable: Running `make run` opens a 1440×810 window containing a bright sky, solid ground, and a pile — drawn as visible 3×3 pixel blocks. Nothing moves yet.
  Why now: Proves the riskiest unknown first — that raylib builds and that a grid of material numbers can be turned into chunky pixels on screen. Everything else lands on this path, so it is bootstrapping and first behavior in one slice.
  PRD ref: `prd.md > Screens and Layout`, `prd.md > Look and Feel`
  Spec ref: `spec.md > Stack`, `spec.md > Components > Grid and world storage`, `spec.md > Components > Rendering`, `spec.md > File Structure`, `spec.md > Where It Runs and How Someone Tries It`
  Build: Write the `Makefile` (clang++, `-std=c++17`, pkg-config for raylib) with `make`, `make run`, `make debug`. Create `src/materials.h` (material IDs + starter color table), `src/world.h/.cpp` (flat `std::vector<uint8_t>` grid, 480×270, bounds-checked `materialAt`/`setMaterial`), `src/render.h/.cpp` (visible grid → raylib image buffer → nearest-neighbour `Texture2D` drawn at 3×), and `src/main.cpp` (window + main loop, Esc quits). Add a `--selftest` mode that renders 60 frames, writes `selftest.png`, prints a one-line summary, and exits — this is the mechanical check reused by every later slice. Add a hand-made static scene (sky, packed dirt and stone ground, a sand column) so there is something to look at. Write `README.md` (build, run, controls) and extend `.gitignore` for the compiled binary and `selftest.png`.
  Verify (mechanical): `make` exits 0 with no errors; `./pixelworld --selftest` prints `grid 480x270 | wrote selftest.png` plus two PASS lines and exits 0; `sips -g pixelWidth -g pixelHeight selftest.png` reports 480×270; `./pixelworld --smoke` opens the window, renders 60 frames, reports the world covering at least 99% of the frame, and exits 0.
  Learner check: Run `make run`. You should see a bright sky, brown ground, and a visible pile of sand grains, each one a chunky block — not blurry. Press Esc to close. Does it look like a pixel world yet?
  Commit: `Add window, pixel grid, and chunky renderer`

- [x] **2. Gravity works: sand, gravel and water move on their own**
  Becomes usable: Grains fall, pile up with a natural slope, and water spreads out and pools in hollows — no clicking required.
  Why now: This is the first real piece of the kernel. Everything else in the project is a different rule applied to this same per-pixel step, so it must exist and be trustworthy before anything is layered on it.
  PRD ref: `prd.md > Materials and how they behave`, `prd.md > Acceptance criteria`
  Spec ref: `spec.md > Components > Materials table`, `spec.md > Components > Simulation`, `spec.md > Data Model`
  Build: Fill `src/materials.h` with all 24 learner-specified materials as a single static table (id, name, category, color, weight, flammable, melts-into, dissolves-in, explosion resistance) — this is the table that makes adding materials cheap. Write `src/sim.h/.cpp` with `stepSimulation(X)`: loose solids try down, down-left, down-right; liquids also try sideways and spread up to a slack distance so pools level out; heavier materials sink through lighter ones; a `moved this frame` flag stops double-processing; the horizontal scan direction alternates each frame so material doesn't drift consistently one way. Simulation covers the visible region plus a 16-pixel margin. Keep the `--selftest` scenes as the sand-and-water check.
  Verify (mechanical): `./pixelworld --selftest` prints and passes `sand settles: PASS` (after 2000 steps no sand cell has air directly beneath it, and the pile's top row is well below where the column started) and `water spreads and stays put: PASS` (water poured in one place keeps every cell, covers at least a quarter more columns than it started with, and none of it leaves the basin or climbs above the rim).
  Learner check: Run `make run`. Watch the sand column, gravel heap and soil bank settle into slopes, and the water spread out and pool in the hollow. Do they move the way you pictured? Press `R` to rebuild the scene and watch again.
  Commit: `Add material table and falling/flowing simulation`

- [x] **3. Fire spreads through wood and foliage and leaves ash**
  Becomes usable: You can set something alight and watch it take hold and crawl across it on its own, leaving burnt ground and ash behind.
  Why now: This is the "oh, that's cool" beat from the scope and the second half of the kernel — the world changing by itself after you stop clicking. It is the earliest point where the project proves its own idea, so learner feedback here can still shape everything after it.
  PRD ref: `prd.md > Materials and how they behave`, `prd.md > What the player sees when they succeed`
  Spec ref: `spec.md > Components > Simulation`, `spec.md > Components > Materials table`
  Build: Add a `life` value per pixel (parallel `std::vector<uint8_t>`) and a Fire material that ages and expires into air or ash. Flammable materials (wood, bark, foliage, mycelium, fungal cap) catch from adjacent fire, burn into fire, then to ash; oil ignites easily and burns faster and spreads further (the PRD's "stronger fire"). Ash is a light loose solid that falls and piles. Add a temporary mouse brush that paints fire where you click, so fire is try-able before the tools exist — marked in the README as temporary and removed in slice 7.
  Verify (mechanical): `./pixelworld --selftest` prints and passes `fire spreads: PASS` (a wood block ignited at one pixel loses more than 60% of its wood cells within 400 steps, produces ash cells, and the fire count returns near zero as it burns out).
  Learner check: Run `make run`, click on the wooden area, and watch the fire crawl along it and leave ash and burnt ground. Does the spread feel right — too fast, too slow, too eager?
  Commit: `Add fire, burning, and ash`

- [x] **4. Materials react to each other**
  Becomes usable: Water meeting lava crusts into stone, fire melting snow and ice into water, water turning dirt to mud, water washing ash away, oil igniting, and sulfur exploding when lava reaches it.
  Why now: These are the reactions that make the world feel alive and unpredictable, and they are pure additions to the simulation core from slice 2 — cheap now, awkward later.
  PRD ref: `prd.md > Materials and how they behave`, `prd.md > Acceptance criteria`
  Spec ref: `spec.md > Components > Simulation`, `spec.md > Components > Materials table`
  Build: Add the neighbour-pair reaction list to `sim.cpp`: lava + water → stone (obsidian when a large lava volume is involved), fire or lava + snow/glacial ice → water, water + soil/packed dirt → mud (mud then flows), water + ash → air, lava + oil → fire, lava + sulfur crust → a small explosion with a crater. Extend the selftest scene to hold a lava pool, a pond, an ice block, a soil bank, an ash pile, an oil pocket, and a sulfur crust so each reaction is observable and checkable.
  Verify (mechanical): `./pixelworld --selftest` prints one PASS line per reaction: `lava+water→stone: PASS`, `ice melts: PASS`, `water+soil→mud: PASS`, `ash dissolves: PASS`, `oil ignites: PASS`, `sulfur explodes: PASS`.
  Learner check: Run `make run`. Break the ground between the pond and the lava and watch the water crust it into stone; set fire near the ice and watch it turn to water. Any reaction that looks wrong or happens too suddenly?
  Commit: `Add material reactions: solidification, melting, mud, dissolving, oil, sulfur`

- [x] **5. You can walk and jump around the world**
  Becomes usable: A small player block with gravity walks left and right, jumps, stands on ground, can't fall through solid material, and the camera follows them.
  Why now: The player is a separate small system that needs a stable simulation underneath it. Building it against a small hand-made level proves movement and collision before the endless world adds its own moving parts.
  PRD ref: `prd.md > The player, health, and death`, `prd.md > The world, generation, and the camera`
  Spec ref: `spec.md > Components > Player`, `spec.md > Components > Camera`
  Build: Add `src/player.h/.cpp` — position, velocity, gravity, walk speed, jump only when standing on solid or loose-solid material, collision resolution against the grid, and a 6×10 drawn rectangle. Add `src/camera.h/.cpp` — two floats following the player with a small dead zone, clamped vertically to the world height and unclamped horizontally. Replace the static scene with a small hand-made level: a hill to climb, a gap to jump, a stone wall, and a sand pit to sink into.
  Verify (mechanical): `./pixelworld --selftest` prints and passes `player rests on ground: PASS` (y stabilises on top of the ground row), `player jumps and lands: PASS` (y rises then returns to the same resting row), `player blocked by stone: PASS` (walking into a stone wall does not change x past it).
  Learner check: Run `make run` and move with A/D or arrows, jump with space. Walk up the hill, jump the gap, try to walk through the stone wall, stand in the sand pit. Does the movement feel right — walk speed, jump height, gravity?
  Commit: `Add player movement, collision, and following camera`

- [ ] **6. The world is endless and full of things to find**
  Becomes usable: You can walk in one direction indefinitely and keep meeting new terrain — hills, mountains with snowy peaks, cave systems, sand and clay pockets, ponds, deep lava, and hand-made structures (ruins, trees, foliage, bones) — and when you walk back, everything you destroyed is still destroyed.
  Why now: This is the exploration half of the product and the last big subsystem. It sits after the player because the camera and movement drive what needs generating, and its determinism and chunk-boundary behaviour are the highest remaining risk in the project.
  PRD ref: `prd.md > The world, generation, and the camera`, `prd.md > States and Boundaries` (off-screen running freely), `prd.md > Acceptance criteria`
  Spec ref: `spec.md > Components > World generation`, `spec.md > Components > Structures`, `spec.md > How This Works, In Plain Language` (the chunk explanation), `spec.md > What Was Simplified and Why`
  Build: Add `src/generation.h/.cpp`: a chunk is 256 wide × 512 tall; its index seeds a deterministic recipe that produces a smooth ground-height curve with hills and at least one mountain, snow above a height threshold, layered packed dirt → stone → bedrock across the bottom, caves carved by a simple noise function, pockets of sand, gravel and clay, surface water ponds, deeper lava pools with sulfur crust nearby. Add `src/structures.h/.cpp`: a small library of hand-authored pixel patterns — ruined wall, fallen pillar, tree, giant tree, bush, grass tuft, buried ruin, bones — each with at least three variants, chosen and positioned by the chunk's own recipe and placed fully inside one chunk, sitting on the local ground. Hold chunks in an `unordered_map` keyed by index, never unload them (this is what keeps your destruction), generate a window of ±2 chunks around the camera, and simulate only the visible region plus the 16-pixel margin.
  Verify (mechanical): `./pixelworld --selftest` prints and passes `chunks are deterministic: PASS` (generating chunk 12 twice gives byte-identical grids), `terrain varies: PASS` (ground height differs across 30 consecutive chunks), `caves exist: PASS` (each chunk has air below the surface), `structures stay in their chunk: PASS` (no structure pixel lies outside its chunk's bounds), `damage survives: PASS` (a pixel changed in chunk 3 is still changed after 50 more chunks are generated).
  Learner check: Run `make run` and walk right for a long stretch — new terrain should keep arriving, with hills, caves, ponds, trees and ruins appearing. Burn or blast something, walk far away, then walk all the way back: it should still be burnt or blasted. Does the world feel varied enough, or too repetitive?
  Commit: `Add chunked endless terrain generation and hand-made structures`

- [ ] **7. Three tools: spark wand, dynamite, acid flask**
  Becomes usable: Number keys 1/2/3 switch tools, the mouse aims, and clicking uses the selected tool — a spark that lights fire on impact, a thrown stick of dynamite that blows a crater, and a thrown flask that shatters into acid and carves tunnels.
  Why now: With the world and player working, tools are the interaction that completes the core loop from the PRD. It is also the last thing the temporary fire brush was standing in for, so the debug control goes away here.
  PRD ref: `prd.md > Tools`, `prd.md > The Core Journey` (steps 4-7)
  Spec ref: `spec.md > Components > Tools and projectiles`, `spec.md > Components > HUD and input`
  Build: Add `src/tools.h/.cpp` and `src/hud.h/.cpp`. Selected tool from keys 1/2/3. Spark wand: a projectile travelling in a straight line that stops in the first solid cell and spawns 3–8 fire cells. Dynamite: a thrown object with an arc, gravity and a landing trigger, exploding in a radius of about 14 pixels — erasing material, flinging a fraction outward as loose pixels, spawning fire at the rim, with resistance read from the material table so stone survives more than dirt. Acid flask: a thrown object that on impact spawns about 40 acid cells; acid converts dissolvable material to air and consumes itself as it works. A short cooldown on all tools. HUD shows the current tool as text. Remove the temporary fire brush from slice 3 and update the README.
  Verify (mechanical): `./pixelworld --selftest` prints and passes `wand stops at solid and lights fire: PASS`, `dynamite crater in dirt but not stone: PASS` (dirt cells removed far exceed stone cells removed at the same radius), `flask pours acid and acid carves stone: PASS` (stone count drops over 600 steps after impact), `tool cooldown: PASS` (holding the button does not fire every frame).
  Learner check: Run `make run`, press 1/2/3 to switch tools. Torch a tree with the spark wand, blow a hole in a hill with dynamite, carve into stone with the acid flask. Which tool feels best? Is the dynamite too strong or too weak, and does the acid carve fast enough?
  Commit: `Add spark wand, dynamite, and acid flask tools`

- [ ] **8. Damage, death, and respawn**
  Becomes usable: Lava, fire, explosions and acid drain your health, the health bar shrinks visibly, and at zero you die and come back at the starting area with the world exactly as you left it.
  Why now: Complete the core journey's last step. It depends on tools (to damage yourself) and on the world (to respawn into a persistent place), so it can only be done now.
  PRD ref: `prd.md > The player, health, and death`, `prd.md > States and Boundaries`, `prd.md > Acceptance criteria`
  Spec ref: `spec.md > Components > Player`, `spec.md > Components > HUD and input`, `spec.md > Data Model`
  Build: Give the player a health value (0–100). Drain it while overlapping lava, fire or acid, and from proximity to an explosion. Draw the health bar top-left with a dark frame. Add a brief death indication, then respawn at the starting x with full health, leaving every chunk untouched. Keep `R` as a debug respawn. Document damage in the README.
  Verify (mechanical): `./pixelworld --selftest` prints and passes `lava drains health: PASS`, `death respawns at start with full health: PASS`, `world damage survives respawn: PASS` (a pixel erased before death is still erased after respawn, and a fire burning before death is still burning).
  Learner check: Run `make run`, burn something you can see, then walk into the lava and die. You should come back at the start — and the thing you burned should still be burnt. Does the health drain feel fair, and is dying and walking back an acceptable cost?
  Commit: `Add health, damage, death, and respawn`

## Hands-on Checkpoints

- [x] Early usable behavior explored — after slice 3 (fire spreads on its own), so feedback can shape the world, player and tools that follow
  Tried it, confirmed the fire feels right — no change wanted to the spread speed or eagerness. Carrying on as planned.
- [ ] Final kick-the-tires exploration and feedback completed

## Final Review

- [ ] Final review complete — feedback resolved and learner confirms ready to ship

## Code Tour and App Map

- [ ] Learning activity complete — guided route, focused alternative, prior practice connected, or brief recap
- [ ] Optional edit and transfer reflection addressed — offered/declined/already covered/not applicable as appropriate
- [ ] `devpost/app-map.html` generated from finished code, checked, and shown, including a project-grounded practice to reuse

Activity and evidence:
Route and stops:
Edit outcome:
Reflection:
Activity mode:

## Revisions

- Slice 5 kept the level that slices 1–4 built and added the wall, gap and sand pit to it, rather than throwing it away for a fresh small one. The small hand-made level was only ever a stand-in for the endless world in slice 6, so replacing the good one would have deleted work and lost everything worth looking at for no gain. The wall, the gap and the sand pit are all there as asked.
- Slice 5's player steps up to 2 cells while walking. Without it the rolling ground — which rises one cell every few columns — stops you dead every few steps, which is unplayable. Two cells is enough to walk over bumps and the edges of small piles without noticing, and far less than the wall, the cliff or the side of the gap, which still stop you exactly as they should.
- Slice 5's leaves do not block the player: you push through foliage, so trees are scenery rather than barriers. The trunks were also cut down to below jump height (20 and 16 cells, from 34 and 26) for the same reason — the level is a strip, so anything taller than a jump walls off half of it. Trees are still there to burn.
- Slice 5's sand column now hangs above the ground instead of resting on it. Resting on the ground it stayed a 51-cell rectangular wall (a vertical face of sand is stable), which looked artificial and blocked the road. Hanging in the air it falls and spreads into a heap you can walk up, which is what it was always meant to demonstrate.
- Slice 5's camera is clamped left and right as well as top and bottom, which the spec deliberately left open. That is only because the level is a fixed 1440 cells wide right now; when slice 6 makes the world endless the horizontal clamp has nothing to clamp against and simply stops mattering.
- Slice 5's player climbs out of anything that lands on them rather than being stuck forever. Material does not know the player exists, so sand falling on you fills the cells you occupy and would otherwise lock the game. While buried, gravity is off and they rise steadily out, in whatever direction is being held. They also stand on sand rather than sinking through it, which is `spec.md > Components > Player` in its own words ("falling is stopped by any solid or loose-solid pixel, which also lets the player stand on piles of sand"), so "sand pit" here means a dish you walk down into and stand in.

- Slice 4's water reactions do **not** use the water up — only lava does that. The PRD asks for "consuming the water" on the lava rule alone, and taking water off the soil and ash rules as well would have drained every pond in the game into its own bed within seconds, which is worse than not doing it. So water turns soil and packed dirt into mud and washes ash away while staying exactly as much water as it was. The "meltable" rows also include mud and clay turning back into water, which is the PRD's own table rather than a change of plan.
- Slice 4's obsidian rule ("large volumes of lava near water") is decided by counting lava in a 7×7 square around the cell — 24 or more out of 49 means deep. Getting a true volume would mean flood-filling a blob every time water touches lava, which is far too much work per tick. The result is the same in practice: a thin trickle cools into stone, a pool cools into obsidian.
- Slice 4's reaction scenes: the plan asked for the lava pool, pond, ice, ash, oil and sulfur to all live in the self-test scene. They went into the **playable** scene instead (a lava pool in a stone cup, a buried sulfur seam, snow on the left, an ash pile, and glacial ice buried under the sand column) so they are all there to find and play with, and the six checks each got their own small scene so every rule is measured in isolation. A full run of the playable world confirms it settles and stays put: after 1800 ticks the pond still holds all 528 water cells, the lava is still in its cup, and only 27 dirt cells at the pond's edge have turned to mud.

- Slice 3's fire is a probabilistic, burn-in-place model rather than the planned "flammable material turns into fire, then to ash". A flammable cell that catches keeps its own material and is given a `life` counter set from its `burnTime`; while `life` is positive the cell is drawn as flickering flame and spreads to its neighbours, and when `life` runs out it becomes its `burnsInto` (ash for wood, bark, leaves, mycelium and fungal caps; air for oil). Brushed-on flames are separate `Fire` cells that live a fixed `FIRE_LIFE` (8) ticks. Each neighbour catches with a probability per tick (`igniteChance`, 0–255) drawn from a repeatable per-cell hash, so fire creeps outward organically instead of advancing as a hard line — and the self-test burns the same way every run because of that reproducibility. "Oil burns faster and spreads further" is the short `burnTime`, high `igniteChance`, and the fact that flowing materials also ignite their diagonals.
- Slice 2's water check is `water spreads and stays put` rather than `water flattens`. A per-pixel liquid can be made to level exactly, but this implementation uses a pressure model (a cell with liquid stacked above it reaches sideways in proportion to that depth) and it levels *approximately*: water settles into a hollow and stops moving, but its surface can stay a few cells uneven, and a thin film's leading edge cannot crawl further on flat ground. The check therefore measures what the model actually promises — volume kept, water spreading noticeably wider than it started, and staying inside its basin — instead of an exact surface height, which would fail on a true but ugly liquid. Sand, gravel, soil and ash are unaffected: they settle and pass exactly as planned.
- Slice 2's material table uses behaviour fields (`density`, `falls`, `flows`, `flowSlack`, `flammable`, `burnsInto`, `meltable`, `meltsInto`, `washedByWater`, `blastResistance`, `acidCorrodes`) rather than the planned `weight`/`category`/`dissolves-in` wording. It covers the same information plus the melt and burn targets the later reaction slices need, so adding a material stays a one-row edit.
- Slice 1's verification now uses a headless `--selftest` (exports a 480×270 grid image) plus a `--smoke` window run that measures how much of the frame the world covers, replacing the planned 1440×810 screenshot check — the first build revealed this display's usable height is only 796 px, so a fixed 1440×810 window would have been clipped. The viewport (window, grid size and pixel scale) is now derived from the monitor and framebuffer at startup and the world is centred, which also makes the window work on smaller displays.
