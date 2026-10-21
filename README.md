# Pixel World

A 2D side-view pixel sandbox where **every pixel is a material with its own behaviour**: sand falls and piles, water flows and pools, fire spreads on its own, lava crusts water into stone, and dynamite, acid and a spark wand let you tear the world apart. Walking around is the point; there is no score and no goal.

Built in C++17 with [raylib](https://www.raylib.com/). See `devpost/` for the plan documents (scope, PRD, technical spec, build checklist).

## Requirements

- macOS (developed and tested on Apple Silicon)
- `clang++` — Apple Command Line Tools (`xcode-select --install`)
- raylib 6.2 and pkg-config: `brew install raylib pkg-config`

## Build and run

```sh
make run          # compile and launch
make              # compile only
make debug        # build with AddressSanitizer + UBSan (pixelworld-debug)
make clean        # remove build output
```

## Controls

| Key | Action |
| --- | --- |
| `A` / `D` or ← / → | Walk left and right |
| `Space` / `W` / `↑` | Jump (only from the ground) |
| `1` / `2` / `3` | Choose a tool: spark wand, dynamite, acid flask |
| Left mouse | Use the selected tool, aimed at the mouse |
| `R` | Throw the world away and start again from the beginning |
| `Esc` | Quit |

## Self-test

```sh
./pixelworld --selftest   # runs the headless checks, writes selftest.png, exits 0 on pass
./pixelworld --smoke      # opens the window, renders 60 frames, closes itself
```

`--selftest` is how each build slice is verified without you having to watch the screen. It checks the physics directly: that sand falls and settles with nothing floating, that water spreads out and stays inside its basin, spreads across a floor as a shallow heap and goes still, that even a whole lakeside of water reaches both sides of its floor and settles without losing a single cell, that fire spreads through a block of wood and burns it down to ash, that lava, water, ice, soil, ash, oil and sulfur all react the way they should, that the player lands on the ground, jumps and comes back down, cannot walk through a stone wall but does plough through a small heap of grain — while a heavier heap of the same size stops them — that each tool does its own job (the spark wand stops at the first solid thing it meets and sets fire to it, dynamite digs a crater in dirt but barely scratches stone, the acid flask shatters into acid that eats its way into stone, the tools have a cooldown, and a thrown tool lands where you aimed) — and that the endless world behaves: the same chunk always generates the same, the terrain differs from place to place, caves exist, every hand-made structure stays inside its own chunk, no tree hangs a branch over the player's head, and a hole you blow in the world is still there after you walk 50 chunks away and come back.

## Damage and death

The player has 100 points of health, shown as a bar in the top-left corner.

| What | How much it hurts |
| --- | --- |
| Lava | 45 a second — about two seconds to die |
| Acid | 30 a second |
| Fire | 25 a second |
| An explosion | up to 60 in one hit, less the further away you are |

When the health runs out you die. The screen says **YOU DIED** for a moment and then you start again at the beginning of the world, whole. The world is **not** put back: every hole you blew, every tree you burnt and every fire you set is still there, and the world keeps going while you are dead.

## Status

Slice 8 of 8 complete: **the whole game**. Press `1`, `2` or `3` to pick up a tool, then click where you want it to go.

- **Spark wand** — a spark that flies straight and stops at the first solid thing it meets, setting a small fire there. Fire spreads on its own through wood, bark and leaves, and leaves ash behind.
- **Dynamite** — a thrown stick that arcs over the ground and blows a crater when it lands. Dirt and sand fly; stone mostly survives it, though not always.
- **Acid flask** — a thrown flask that shatters into a splash of acid. The acid eats into whatever it touches, spending itself as it goes, so it can carve a hole through stone that dynamite could not.

Every throw lands where you clicked, from a couple of cells away to the far side of the screen.

The world itself is endless: 256-cell-wide chunks are generated on demand as you walk and never thrown away, so the world keeps arriving and everything you destroy stays destroyed. The ground rolls through hills and snow-capped peaks, with caves and pockets of sand, gravel and clay underneath, ponds on the surface and lava pools in stone cups far below. Hand-made ruins, pillars, trees, giant trees, bushes, tufts and bones stand on the ground along the way.

The trees follow one rule: the leaves reach all the way down, but the wood does not. Only the trunk is wood, and it is narrow, so you can walk into the leaves, jump, and land on top of the trunk instead of being stopped dead by a branch at eye level.

Only the pixels near you are simulated, and only the chunks near you are built, so the world stays quick however far you walk. Press `R` to throw the whole thing away and start again.

Progress lives in `devpost/checklist.md`.
