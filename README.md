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
| Left mouse | Set fire, to watch it spread (temporary debug brush — the real tools arrive in slice 7) |
| `R` | Rebuild the scene (temporary, while the world is not generated yet) |
| `Esc` | Quit |

The three tools arrive in a later slice (see `devpost/checklist.md`).

## Self-test

```sh
./pixelworld --selftest   # runs the headless checks, writes selftest.png, exits 0 on pass
./pixelworld --smoke      # opens the window, renders 60 frames, closes itself
```

`--selftest` is how each build slice is verified without you having to watch the screen. It checks the physics directly: that sand falls and settles with nothing floating, that water spreads out and stays inside its basin, spreads across a floor as a shallow heap and goes still, that even a whole lakeside of water reaches both sides of its floor and settles without losing a single cell, that fire spreads through a block of wood and burns it down to ash, that lava, water, ice, soil, ash, oil and sulfur all react the way they should, that the player lands on the ground, jumps and comes back down, cannot walk through a stone wall but does plough through a small heap of grain — while a heavier heap of the same size stops them — and that the rendered image comes out the right size.

## Status

Slice 5 of 8 complete: there is now a player. A 6×10 block with gravity walks left and right, jumps, lands on the ground, can be stopped by a wall, and is pushed up over small bumps instead of getting stuck on them. The camera follows with a small dead zone, so walking does not make the whole picture jitter, and it never looks above the sky or below the bottom of the world. Only the pixels near you are simulated — the rest of the world waits exactly as it was until you walk over to it.

The level has grown to go with it: three screens wide, with a stone wall to walk into and jump over, a gap to jump, a sand pit to walk down into and stand in, and everything from slice 4 still there. Sand now hangs in the air and falls into a heap you can climb when the world starts. If you stand under it, you get buried — and climb back out.

Water now follows the same short rules as everything else: fall, roll down a slope, spread sideways. It runs downhill and spreads out across a floor, but it does not find a mathematically flat level — a poured lake settles as a broad, shallow heap, like a very slippery grain, with a few surface pixels shuffling. That is a deliberate choice: the rules stay short enough to explain in one breath.

The last big piece is next: the endless world. Progress lives in `devpost/checklist.md`.
