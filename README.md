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
| `R` | Rebuild the scene (temporary, while there is no player to move and the world is not generated yet) |
| `Esc` | Quit |

Walking, jumping and the tools arrive in later slices (see `devpost/checklist.md`).

## Self-test

```sh
./pixelworld --selftest   # runs the headless checks, writes selftest.png, exits 0 on pass
./pixelworld --smoke      # opens the window, renders 60 frames, closes itself
```

`--selftest` is how each build slice is verified without you having to watch the screen. It checks the physics directly: that sand falls and settles with nothing floating, that water spreads out and stays inside its basin, and that the rendered image comes out the right size.

## Status

Slice 2 of 8 complete: the world simulates itself. Sand, gravel, soil and ash fall and pile into natural slopes; water spreads out and pools in hollows; heavier material sinks through lighter. The material table now defines 27 materials (air and fire included) with their density, how they move, and what they react to — the hooks the next slices use for fire, reactions and tools. Nothing reacts to you yet, and the scene is fixed rather than endless — the player, the world generation, the tools and health come next. Progress lives in `devpost/checklist.md`.
