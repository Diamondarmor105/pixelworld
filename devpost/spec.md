---
doc: spec
status: approved
---

# Pixel World (working title) — Technical Spec

## How This Works, In Plain Language

The whole game is **one grid of pixels**, **one loop**, and **one window**. Every piece below hangs off those three things. There is no database and nothing is saved, because the world only needs to exist while the program is open.

- **The window** — a blank rectangle that raylib opens. It shows the world, your health bar, and which tool you're holding.
- **The pixel grid** — the world's entire memory. For every pixel position, we store *one number*: which material is there (0 = air, 1 = stone, 2 = sand, and so on). That's it — a giant, flat list of numbers. Width and height are described below. Position `(x, y)` becomes a single flat index with `y * width + x`.
- **The simulation** — once per frame, we walk over the pixels near the screen and apply material rules: *does sand have air under it? then move it down. Is fire next to wood? then light it. Is lava next to water? then turn to stone.* Every visible behavior in the game — falling, flowing, burning, melting, dissolving, exploding — is one of these rule checks.
- **The renderer** — turns material numbers into colors, writes those colors into raylib's image buffer, and draws that image **scaled up 3×**, so one simulated pixel becomes a chunky 3×3 block on screen.
- **The world generator** — fills a strip of terrain with materials according to terrain rules: sky, hills, dirt layers, stone, caves, pockets of sand and clay, water pools, deeper lava, and structures.
- **Chunks (strips)** — the endless world, divided into vertical strips 256 pixels wide, each the full height of the world. A strip's number determines its terrain, so forgetting and regenerating a strip gives the same result. Only nearby strips are simulated; a strip you've damaged stays in memory so your destruction persists.
- **The player** — a small block with gravity that walks and jumps, stands on solid material, takes damage, and respawns.
- **The tools** — the spark wand (a projectile that sparks fire on impact), dynamite (a thrown object that explodes), and the acid flask (a thrown object that shatters into a pool of acid).
- **The HUD** — a health bar and a current-tool indicator, drawn as simple rectangles and text on top of the world.

**Why this shape and not something more elaborate:** the interesting part of this project is the pixel rules and the terrain recipe. Anything that doesn't serve those is a distraction. So: no engine, no framework, no database, no save files, no networking — one grid, one loop, one window.

## The Core Journey Through the System

Traced from `prd.md > The Core Journey`:

1. **You launch it** → raylib opens a 1440×810 window → the generator fills the starting strips → the renderer paints the grid → the player block is placed on top of the first solid ground. *You see a bright landscape.*
2. **You press D** → the player's x position moves right → the camera follows → when you near the right edge of the loaded strips, the next strip is generated from its number → the visible region of the grid is redrawn. *You keep walking and new terrain keeps arriving.*
3. **Every frame, simultaneously** → the simulation runs over the pixels near the screen → sand that lost its support falls, water flows into a hollow, fire spreads. *Things move on their own.*
4. **You press 1** → selected tool becomes the Spark Wand. **You aim with the mouse and click** → a projectile is created with a direction and speed → each frame it moves and checks the pixel it's in → when that pixel is solid, it stops and spawns fire there. *You set a ruin alight and the fire spreads.*
5. **You press 2 and click** → a dynamite object is created with a velocity → each frame it moves and is pulled down by gravity → when it lands, an explosion runs: pixels inside a radius are erased, some are flung outward, fire spawns at the rim, and if you were too close, you lose health.
6. **You take damage** → `player.health` drops → the HUD bar visibly shrinks → at zero, the player dies and respawns at the starting strip with full health. *The world is untouched by your death.*
7. **You walk back to where you started** → those strips are still in memory, so the burned tree is still burnt and the crater is still there. *Nothing regenerates over your changes.*

## Stack

- **Language: C++17**, compiled by `clang++` (Apple Command Line Tools, already installed).
- **Graphics library: [raylib 6.2](https://www.raylib.com/)** — installed via Homebrew, verified present on this machine (`brew list --versions raylib` → `6.2`). Gives us a window, keyboard and mouse input, a pixel buffer we can write directly into, nearest-neighbor scaling (crisp chunky pixels), and text drawing for the HUD. Documentation: [cheatsheet](https://www.raylib.com/cheatsheet/cheatsheet.html), [Image](https://www.raylib.com/cheatsheet/cheatsheet.html#image), [Texture](https://www.raylib.com/cheatsheet/cheatsheet.html#texture).
- **Build: a small `Makefile` using `pkg-config`.** Chosen over CMake because it's one readable file with one obvious command (`make run`). pkg-config resolves raylib's paths: `-I/opt/homebrew/Cellar/raylib/6.2/include -L/opt/homebrew/Cellar/raylib/6.2/lib -lraylib`.
- **No other dependencies.** No package manager for the app itself, no assets, no font files (raylib's built-in font covers the HUD).
- **Unverified:** I confirmed the raylib version and that pkg-config resolves it, but I have not yet compiled and run a raylib program on this machine. Step 1 of the build is a minimal "hello window" to prove the toolchain end to end before any game code.

## Where It Runs and How Someone Tries It

- **Runs locally on this Mac as a desktop window.** No server, no accounts, no API keys, no cost, no network.
- **To build and run:** `make run` in the project folder. First compile takes roughly 10–40 seconds; later compiles are faster.
- **To try it as a stranger would:** they can't, directly — a compiled C++ app can't be casually hosted the way a web page could. So the proof is the **demo video**: screen-record the window while playing, using macOS's built-in recorder (⌘⇧5) or QuickTime.
- **Submission requires both a short demo video and a public GitHub repository.** The repository will contain the source, the `Makefile`, and a `README.md` explaining how to build and play. **Deployment is not planned** — recorded as the learner's accepted tradeoff of choosing a compiled language (they were told the browser option would have been shareable by link).
- **On-screen controls (to document in `README.md`):** D/A or arrow keys to walk, W/space to jump, 1/2/3 to select tools, mouse to aim, left click to use, R to respawn (debug convenience).

## Look and Feel

Carried forward from `prd.md > Look and Feel` and `scope.md > Inspiration & Identity`, translated into what the code will actually do:

- **Pixel scale.** The simulated grid is **480 × 270 pixels**, drawn at **3× scale** into a **1440 × 810 window**. This is the "chunky" setting the learner asked for: a single sand grain is drawn as a visible 3×3 block.
- **Palette.** Bright and friendly, one distinct flat color per material: greens for foliage and mycelium, warm browns for soil and bark, greys for stone and limestone, blues for water and ice, hot orange-red for lava and fire, near-black for obsidian and oil, pale yellow-green for acid, light grey for ash, off-white for snow.
- **Sky and background** are a light, bright sky color at the top of the world — explicitly *not* Noita's dark mood.
- **Optional polish (learner can veto):** a small deterministic per-pixel color variation on each material so big fields of dirt or stone don't look flat. Cheap, and it makes the simulation read better.
- **The player** is a small solid rectangle (about 6 × 10 pixels) with a brighter facing-color, not a sprite. Consistency with `scope.md > Explicitly Cut`: no art.
- **HUD.** Health bar as a filled rectangle in the top-left (red on a dark frame). Current tool shown as text in the bottom-left ("1 SPARK WAND"). Nothing else on screen.
- **What the stack can't honor:** there is no easy path to a browser build, so no shareable link; and no animation or lighting effects. Both are inside the accepted tradeoffs.

## Components

Each component is one pair of files in `src/`, so `5-build` can build and test them one at a time.

### Grid and world storage
The world is a fixed-height, endless-width grid of one-byte material IDs, split into **chunks** of 256 wide × 512 tall (512 is the world height; the top ~180 rows are sky). A chunk is generated on demand from its index and kept in an in-memory map once created, so player damage survives. Reading a pixel is `materialAt(worldX, worldY)`; writing is `setMaterial(worldX, worldY, id)`.
PRD ref: `prd.md > The world, generation, and the camera`.

### World generation
Fills a single chunk from its index: a hash of the index seeds a deterministic recipe. Layers below ground are packed dirt, then stone, then deeper stone, with caves carved by a simple noise-style function, pockets of sand, gravel and clay, scattered water pools near the surface, lava pools deeper down, and bedrock across the very bottom row. Mountains and hills come from a smooth height curve derived from the index, with snowy peaks above a height threshold.
PRD ref: `prd.md > The world, generation, and the camera`.

### Structures
A small library of hand-authored shapes — a ruined wall, a fallen pillar, a tree, a giant tree, bushes and grass tufts, a buried ruin, bones — each stored as a tiny pattern of pixels, and **several variants per kind**. Placement is decided from the chunk's deterministic recipe (so it repeats identically for that chunk) and each structure is placed fully inside one chunk to avoid seams, sitting on the local ground level.

One rule governs the trees: **the leaves reach all the way down, but the wood does not.** Only the trunk is wood, and it is narrow. A branch of wood hanging over the player's face at eye level stops them dead — they try to jump, bump their head on the branch, and drop straight back down again — so a tree must never have one. The leaves do nothing at all (the player pushes straight through them), so the player can walk into a tree, jump, and land on top of the trunk.
PRD ref: `prd.md > The world, generation, and the camera`, `scope.md > POC Boundary` (hand-made structures with variations).

### Materials table
A single static table, one row per material, holding its category (solid, loose solid, liquid, gas, flammable, meltable, byproduct) and its properties: weight, whether it falls, **viscosity** (liquids: how freely it spreads), **friction** (loose solids: the angle of repose its pile holds), whether it burns and into what, what it melts into, what dissolves it, and how well it resists explosions. All 24 learner-specified materials appear here, grouped into the six behavior categories from the PRD. Adding a material later means adding one row.
PRD ref: `prd.md > Materials and how they behave`.

### Simulation
Once per frame, over the pixels in view plus a 16-pixel margin: solids do nothing; every mobile material (loose solids and liquids alike) is a continuous quantity, holding a mass per cell and moving by transferring a fraction of a unit rather than swapping whole cells. A cell pours down into any cell below with room, then spreads toward lower neighbours — the two beside it and the two diagonally below. Two material numbers shape that movement: `viscosity` scales how much a liquid spreads per tick (water snaps flat, lava and mud ooze), and `friction` is the angle of repose, letting a loose solid spread sideways only when the neighbouring surface is steeper than its slope — so sand still piles in a dune while water levels. A solid always falls straight down regardless of friction. Fire has a lifetime, spreads to adjacent flammable pixels, and turns into ash or air when it expires; reactions are checked on a small list of neighbour pairs (lava + water → stone, water + soil → mud, lava + sulfur → explosion, fire or lava + snow/ice → water, water + ash → air, lava + oil → fire). Movement is a single bottom-up pass over every mobile cell, **double-buffered**: the pass reads a copy of the mass grid taken before it starts and writes the live grid, so a cell always computes its outflow from the world as it was at the start of the tick and nothing can travel more than one cell per tick. Scatter writes add to the recipient's live mass rather than overwriting it, so the pass is exactly conservative. A cell is never left holding less than `kMassEmpty`; a transfer that would do so is refused and the material stays where it was, which keeps the visible surface from blinking between material and air.
PRD ref: `prd.md > Materials and how they behave`, plus the acceptance criteria list.

### Rendering
Takes only the visible region of the grid, writes one color per cell into a raylib image buffer, and draws it as a single scaled texture with nearest-neighbor filtering. One draw call per frame; the grid is never drawn pixel by pixel.
PRD ref: `prd.md > Screens and Layout`, `prd.md > Look and Feel`.

### Camera
Two floats tracking the world position of the top-left of the view, following the player with a small dead zone, clamped vertically to the world height and unclamped horizontally (the world is endless sideways).
PRD ref: `prd.md > The world, generation, and the camera`.

### Player
A small rectangle with position, velocity, gravity, and a health value. Walking sets horizontal velocity; jumping applies an upward velocity only when standing on solid material; falling is stopped by any solid or loose-solid pixel, which also lets the player stand on piles of sand. Damage sources (lava, fire, explosion, acid, plus falling sand/lava at head height) reduce health; at zero the player respawns at the starting position with full health.
PRD ref: `prd.md > The player, health, and death`.

### Tools and projectiles
`selectedTool` is one of three. The spark wand fires a small projectile that travels in a straight line until it enters a solid pixel, then spawns a cluster of fire there. Dynamite and the acid flask are thrown objects with position, velocity and gravity that trigger on landing — a circular explosion, or a splash of acid — and are drawn as a small colored block while in flight so the throw reads on screen. Explosions erase pixels in a radius, with stone and harder materials resisting proportionally, and fling a fraction of the erased material outward as loose pixels. Tools have a small cooldown so holding the mouse doesn't fire every frame.
PRD ref: `prd.md > Tools`.

### HUD and input
Draws the health bar and current tool text. Maps keys to actions (walk, jump, tool selection, respawn) and the mouse to aiming and using, in one small input block read at the top of each frame.
PRD ref: `prd.md > Screens and Layout`, `prd.md > States and Boundaries`.

### Main loop
Opens the window, generates the starting chunks, places the player, then loops forever: read input → update player and projectiles → step the simulation → draw the world → draw the HUD → present the frame (raylib's `EndDrawing`). Nothing blocks; the loop is the program.
PRD ref: `prd.md > The Core Journey`.

## Data Model

Where everything lives, and what happens to it when you leave and come back.

- **Material grid** — the pixels of one chunk, `std::vector<uint8_t>`, one byte per pixel, indexed `y * 256 + x`. Written by simulation, explosions, acid, and tools. This is the world.
- **Chunk map** — `std::unordered_map<int, Chunk>`, keyed by chunk index. A chunk is created the first time it's needed and **never unloaded**, which is what preserves your destruction. Memory grows as you explore; for a demo session that's harmless, and a cap is a later enhancement.
- **Player state** — position, velocity, health, facing, selected tool. Lives for the whole session; reset on respawn except the world.
- **Projectiles** — a small `std::vector` of in-flight dynamite and acid flasks; each is created on click and removed on landing.
- **Camera** — two floats, recomputed from the player every frame.
- **Persistence** — none, per `prd.md > States and Boundaries`. Closing the program loses the world. Nothing is written to disk.

## File Structure

```
Hackathon/
├── Makefile                  # build + run: make, make run, make debug
├── README.md                 # what it is, how to build, the controls
├── src/
│   ├── main.cpp              # window, game loop, wiring everything together
│   ├── materials.h           # material IDs + the properties table
│   ├── world.h/.cpp          # the grid, chunks, pixel read/write
│   ├── generation.h/.cpp     # the chunk recipe: terrain, caves, pockets
│   ├── structures.h/.cpp     # hand-authored shapes + deterministic placement
│   ├── sim.h/.cpp            # per-frame material rules and reactions
│   ├── render.h/.cpp         # visible grid → image → scaled draw
│   ├── camera.h/.cpp         # follows the player
│   ├── player.h/.cpp         # movement, gravity, collisions, health, death
│   ├── tools.h/.cpp          # spark wand, dynamite, acid flask, projectiles
│   └── hud.h/.cpp            # health bar, tool indicator, input mapping
├── devpost/                  # learning workspace (plan docs, not part of the app)
│   ├── learner-profile.md
│   ├── scope.md
│   ├── prd.md
│   └── spec.md
└── .gitignore
```

Note: headers and `.cpp` files are split so each piece can be built and tested on its own, and a `.h` file is where the *shape* of a piece is declared while the matching `.cpp` is where its *behavior* is written.

## External Services and Dependencies

Nothing external at runtime. Nothing to sign up for, no keys, no rate limits, no cost.

| Dependency | Version | How it's installed | Where it's used |
| --- | --- | --- | --- |
| raylib | 6.2 (verified installed) | `brew install raylib` | Window, input, image/texture, text |
| pkg-config (`pkgconf`) | 3.0.9 (verified installed) | `brew install pkg-config` | Resolves raylib's include/lib flags in the Makefile |
| clang++ (Apple Command Line Tools) | system version | `xcode-select --install` (already present) | Compiles the C++ |
| make | system version | already present | Runs the build |

## Important Failure Modes

- **The build can't find raylib** → shown as a compile error mentioning a missing header or library. Fallback: the `README.md` build note, and the `Makefile` uses pkg-config so the paths stay correct even if Homebrew's version bumps.
- **A crash with no message (`Segmentation fault`)** → the classic C++ failure from an out-of-range index. Fallback: all grid access goes through the bounds-checked `materialAt`/`setMaterial`, and `make debug` builds with AddressSanitizer, which prints the offending line.
- **The simulation gets slow (frame rate drops)** → shown by raylib's on-screen FPS counter, which we keep visible during development. Fallback: shrink the simulated region (never the visible area), or lower the pixel scale so fewer cells are simulated.
- **The player falls out of the world or gets stuck inside terrain after an explosion** → fallback: `R` respawns the player at the start; the world keeps any damage.
- **A structure straddles a chunk boundary and looks cut off** → fallback: structures are placed fully inside one chunk; only the placement rules can drift, not the shape.
- **A player-modified chunk gets regenerated by mistake** → would silently undo the player's destruction. Fallback: chunks are only generated when absent from the map, and the `README`-level acceptance check is "burn something, walk far away, come back, it's still burnt."

## What Was Simplified and Why

- **Endless sideways, fixed height** instead of an endlessly deep world — every chunk is a full-height column (512 pixels). Mountains, caves and volcanic depths all fit; infinite depth would add complexity for nothing.
- **Only near-screen cells simulate** instead of the whole world — required for an endless world, and it matches `prd.md > States and Boundaries` (off-screen frozen). The 16-pixel margin exists so material moving just off-screen doesn't behave oddly.
- **Chunks are never unloaded** instead of a sliding window with save/reload — a demo session is short, memory is cheap, and this guarantees the world stays ruined. Unloading would need saving changes per chunk.
- **Simple thrown objects** (a point with gravity and a landing trigger) instead of rigid bodies — this is the learner's agreed "Version A" (`prd.md > Product Decisions`). True tumbling and stacking would be a second physics system sharing the same world.
- **Fire is a material with a lifetime; "heat" is adjacency** instead of a temperature simulation — the learner's explicit decision. Fire spreads to neighbors and expires; nothing tracks degrees or cools down.
- **Structures placed inside single chunks** instead of spanning boundaries — avoids visible seams and cross-chunk bookkeeping.
- **The player is a colored rectangle**, not a sprite — art is cut by `scope.md > Explicitly Cut`.
- **No save files** — persistence is deferred in `prd.md > Deferred From the POC`.

## Decisions and Open Issues

**Learner decisions:**

- **C++ with raylib**, chosen over a browser-based JavaScript approach and over Rust/C++ alternatives. Reason given: the learner wants to understand the code, and prefers a compiled language. Tradeoffs accepted knowingly: a setup step, slower iteration (compile instead of instant refresh), cryptic crash messages, and no easily shareable browser link.
- **Local desktop app with a screen-recorded demo video**, no deployment. Accepted as the consequence of choosing C++.
- **`Makefile` + pkg-config** over CMake, for a build that is one readable file.

**Implementation details derived from those decisions (not learner choices, changeable on request):**

- Grid 480 × 270 at 3× scale in a 1440 × 810 window; chunk size 256 wide × 512 tall.
- Typed-array grid (`std::vector<uint8_t>`) with a flat index; material IDs as an enum.
- The materials table as a single static property table — this is what makes `prd.md`'s "the more materials the better" affordable.
- Alternating horizontal scan direction and a `moved this frame` flag in the simulation.
- 16-pixel simulation margin around the visible region.
- Structures placed fully inside a chunk.
- A hidden `R` key to respawn (development convenience, documented in the README).
- Optional per-pixel color jitter for material fields.

**One useful unknown, resolved here:** the learner asked what "chunks of terrain generation" means. Explained in plain language: the endless world is cut into fixed-width numbered columns; the number is fed to a deterministic recipe, so a chunk can be forgotten and recreated identically; a small window of chunks is held in memory; only pixels near the screen simulate; chunks the player has damaged stay in memory so the destruction persists. The build's first proof of this will be the PRD's existing check — walk a long way and see new terrain keep arriving, then walk back and confirm a burnt tree is still burnt.

**Open issues:**

- **Repository boundary.** The enclosing git repository is the whole Desktop folder, not this project — publishing that as the hackathon repository would leak unrelated files. Before `6-ship`, this folder should become its own repository (or the app should move into one). Flagged for the build.
- **The exact raylib 6.2 names** for creating and updating an image texture should be confirmed by compiling the hello-window step first; the reference used above is from raylib's documentation, not from a build on this machine.
- **Player damage from falling material** (being buried by sand) is unspecified in the PRD. Working assumption: it does not damage the player, only lava, fire, explosions and acid do. Trivial to change.
