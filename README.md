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
| Left mouse | Set fire, to watch it spread (temporary debug brush — the real tools arrive in slice 7) |
| `R` | Rebuild the scene (temporary, while there is no player to move and the world is not generated yet) |
| `Esc` | Quit |

Walking, jumping and the tools arrive in later slices (see `devpost/checklist.md`).

## Self-test

```sh
./pixelworld --selftest   # runs the headless checks, writes selftest.png, exits 0 on pass
./pixelworld --smoke      # opens the window, renders 60 frames, closes itself
```

`--selftest` is how each build slice is verified without you having to watch the screen. It checks the physics directly: that sand falls and settles with nothing floating, that water spreads out and stays inside its basin, that fire spreads through a block of wood and burns it down to ash, and that the rendered image comes out the right size.

## Status

Slice 3 of 8 complete: fire burns. Wood, bark, leaves, mycelium, fungal caps and oil catch alight, spread the flames to what is next to them, burn for as long as their material allows, and leave ash (or nothing, for oil) behind. The scene now has two trees, a fallen log and an oil slick on the pond to set alight; hold the left mouse button to start a fire and watch it crawl and leave ash. The material table's 27 rows each carry their own burn speed and how easily they catch. Nothing reacts to you yet and the scene is fixed rather than endless — the player, the world generation, the tools and health come next. Progress lives in `devpost/checklist.md`.
