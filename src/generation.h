#pragma once

#include <vector>

#include "world.h"

// World generation.
//
// Two kinds of world live here:
//
//   * The endless one. The world is cut into chunks of 256 columns, and each
//     chunk is built from its own number by a recipe that always gives the
//     same result. You can walk for ever and the terrain keeps arriving; walk
//     back and the ground is exactly as you left it.
//
//   * The hand-made test scenes. Small, fixed scenes the self-test runs its
//     checks against.

// How wide the hand-made test scenes are.
constexpr int TEST_SCENE_WIDTH = 480;

// Where a hand-made structure was put, so a test can check that it stayed
// inside its chunk.
struct StructurePlacement {
    int x;        // the world column of the pattern's left edge
    int y;        // the world row of the pattern's BOTTOM edge
    int width;    // how wide the pattern is
    int height;   // how tall the pattern is
    int kind;     // which kind of structure
    int variant;  // which pattern of that kind
};

// -- The endless world -----------------------------------------------------

// Builds the terrain for one chunk: the ground surface, the layers under it,
// caves, pockets of loose material, a pond, a lava pool and the hand-made
// structures that stand on it.
//
// The same chunk index always gives the same chunk, so the world can be
// forgotten and built again identically. `placements` is optional: when it is
// given, it is filled in with where the structures went.
void generateChunk(int index, Chunk& chunk,
                   std::vector<StructurePlacement>* placements = nullptr);

// Builds every chunk near `worldX` that does not exist yet. The world is
// endless, so this is called as the player explores.
void generateChunksAround(World& world, int worldX, int radius);


// The row of the ground surface at a world column: the first solid row. The
// same column always gives the same row, so neighbouring chunks agree where
// they meet.
int groundRow(int worldX);

// How much snow lies on this column: none at all in the lowlands, a cap on the
// mountain tops. Snow follows the mountain, not a line drawn across the map,
// so it only ever appears where there is a mountain to put it on.
int snowRowsAt(int worldX);

// How high the mountain is at this column, from 0 in the lowlands to 1 at the
// very top of a range.
float mountainHeightAt(int worldX);

// -- The hand-made test scenes ----------------------------------------------

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
