---
doc: scope
status: approved
---

# Pixel World (working title — rename at review)

A 2D side-view pixel world where every individual pixel is simulated, so you can explore it, set it on fire, and blow it apart.

## The Unique Kernel

Every pixel on screen is alive: it has a material with its own physics rules, and it reacts to the pixels around it. Because of that, damage isn't painted on — it *happens*. Fire spreads through grass and wood on its own, explosions rip holes in the ground and fling material around, and whatever survives stays changed.

## Who It's For

The learner first — this is a toy to explore and mess with, in the spirit of [Noita](https://noitagame.com/). Beyond that, it's for anyone who enjoys a sandbox where the world reacts: no goal, no score, just "what happens if I set this on fire?"

## The Core Loop

You spawn into a generated landscape and start walking. You cross hills and mountains, drop into caves, and hunt for hand-made structures — ruins, trees, smaller foliage. When you find something interesting, you use a tool on it — set a wooden ruin alight and watch the fire spread across it, or throw an explosion into a cave wall and watch stone resist while dirt and sand blast away. Then you walk on and find the next thing.

The pull is that the world responds in ways you didn't exactly predict, so it's always worth trying one more thing.

## Inspiration & Identity

Noita is the reference: a 2D falling-sand simulation where materials interact, with per-pixel reactions. Perspective is a 2D platformer — side view, the player has gravity, and the world is built out of pixels. Mood is playful and experimental: no objectives and no score, just a world to poke at — though the world can kill you.

## Why This Matters to the Learner

They wanted to build a simulation where every pixel is simulated and can interact with the others, and to see what it's like to build one. Learning outcome is simply "getting something working."

## What "Working" Looks Like

You press start and you're standing on generated ground. You walk right, over a hill, and the terrain under you is made of real materials — grass on top, dirt below, stone deeper down, a sand pocket, a pool of water. You pass trees and foliage, and come to a wooden ruin on the surface. You hold the fire tool on it: fire catches, creeps along the wood, spreads to nearby grass and up into a tree, and leaves burnt ground behind. You drop into a cave and fire an explosion at the wall — dirt and sand are torn out and thrown, stone mostly holds. You keep walking.

The "oh, that's cool" beat: you set one small thing on fire or blast one hole, and the world visibly *keeps changing on its own* after you stop clicking.

## The POC Boundary

In:

- **2D side-view world**, platformer perspective, endless terrain generated in chunks as the player walks — the world keeps going in both directions rather than ending at a wall.
- **Simulation near the player only**: pixels on and around the screen are alive (they fall, flow, burn, melt); terrain off-screen is generated ahead of the player and simply holds still until it comes into view. This is what makes an endless world affordable.
- **The player**: a small figure with gravity that walks and jumps, standing on solid material rather than falling through it. Lava, fire, explosions, and acid all hurt the player, and they can die.
- **Generated terrain**: mostly solid ground, hills and mountains on the surface, caves carved underneath — generated continuously in chunks as the player explores in either direction.
- **A handful of materials with real differences** — grass, dirt, stone, sand, water, wood, plus air. Sand falls and piles, water flows and pools, grass and wood are flammable, stone resists explosions.
- **Hand-made structures placed by generation**: a small library of hand-authored shapes — ruins (wood and stone), trees, and smaller foliage — each with **several variations** so the world doesn't feel repetitive. Terrain generation picks shapes from this library and places them on the surface and in caves.
- **Two tools**: fire (spreads through flammables, leaves burnt material) and explosion (removes material, weaker against stone).
- **One world**, endlessly explorable in the demo video — the demo walks in one direction and keeps finding new terrain.

## Later

- More materials added to the data-driven set, and where cheap, more tools (a water pourer or a freeze tool were floated).
- Rigid bodies that tumble and stack realistically (a separate system; large).
- More varied or more numerous structures, and more variations in each.
- Saving and loading a world.
- The named biomes (volcanic depths, glacial peak) as full distinct regions — the materials already exist in smaller amounts.

(The detailed material list, which ones ship now, and how each behaves are defined in `prd.md > Features and Behavior` and `prd.md > Deferred From the POC`.)

## Explicitly Cut

- **Polished art, animation, and lighting** — effort goes into simulation behavior, not looks.
- **Sound and music** — not needed to prove the idea.
- **Menus, settings, and a title screen** — the demo starts in the world.
- **Enemies, combat, scoring, or a win condition** — this is a sandbox with no goal; the only threat is the world itself (lava, fire, explosions, acid).
- **Multiplayer** — out of reach for the timeframe.
