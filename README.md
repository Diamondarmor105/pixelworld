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
| `A` / `D` or `←` / `→` | Walk |
| `Space` / `W` | Jump |
| `Esc` | Quit |

More controls arrive as the tools, health and respawn are built (see `devpost/checklist.md`).

## Self-test

```sh
./pixelworld --selftest   # runs the headless checks, writes selftest.png, exits 0 on pass
./pixelworld --smoke      # opens the window, renders 60 frames, closes itself
```

`--selftest` is how each build slice is verified without you having to watch the screen.

## Status

Slice 1 of 8 complete: a window opens and shows a chunky pixel scene (sky, ground, stone, a sand column) built from the material grid. It does not move or respond yet — the simulation, the player and the tools come next. Progress lives in `devpost/checklist.md`.
