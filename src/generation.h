#pragma once

#include "world.h"

// World generation. For now this builds hand-made scenes: one to look at, and
// two small scenes used by the self-test to check that gravity and liquid
// spreading actually work. The real endless chunk generation replaces (and
// extends) this in slice 6.

// The scene shown when the program runs: sky, layered ground, a sand column, a
// gravel pile, a soil bank and a water pool.
void buildTestScene(World& world);

// Self-test scenes.
// A block of sand hanging in mid-air above a stone floor — it must fall and
// settle into a pile with nothing floating.
void buildDropTestScene(World& world);

// Water piled up in one side of a stone basin — it must spread out and level.
void buildPoolTestScene(World& world);
