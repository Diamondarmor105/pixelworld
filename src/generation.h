#pragma once

#include "world.h"

// World generation. For now this builds hand-made scenes: the one to look at,
// and the small scenes the self-test runs its checks against. The real endless
// chunk generation replaces (and extends) this in slice 6.

// The scene shown when the program runs: sky, layered ground, a sand column, a
// gravel heap, a soil bank, a water pool with an oil slick, two trees, a fallen
// log, a lava pool, a sulfur seam, snow, an ash pile and buried glacial ice.
void buildTestScene(World& world);

// Self-test scenes. Each one is built inside an already-constructed World, so
// the test chooses the size.

// A block of sand hanging in mid-air above a stone floor — it must fall and
// settle into a pile with nothing floating.
void buildDropTestScene(World& world);

// A block of wood standing on a stone floor, lit in the middle — the fire must
// spread through it, burn it down and leave ash.
void buildBurnTestScene(World& world);

// Lava in a stone well with water on top. `lavaHalfWidth` sets how wide the
// lava is: narrow makes stone, wide makes obsidian.
void buildLavaWaterScene(World& world, int lavaHalfWidth);

// Glacial ice and snow with flame pressed into them — both must melt to water.
void buildIceMeltsScene(World& world);

// Water lying on a bed of soil — the soil must soften into mud.
void buildMudScene(World& world);

// Water lying on a bed of ash — the ash must wash away.
void buildAshScene(World& world);

// Oil floating on lava — the oil must go up in flames on the spot.
void buildOilScene(World& world);

// Sulfur crust buried next to lava — the lava must blow a crater.
void buildSulfurScene(World& world);
