#pragma once

#include "world.h"

// World generation. For now this builds hand-made scenes: one to look at, and
// small scenes used by the self-test to check that gravity, liquid spreading and
// fire actually work. The real endless chunk generation replaces (and extends)
// this in slice 6.

// The scene shown when the program runs: sky, layered ground, a sand column, a
// gravel pile, a soil bank, a water pool with an oil slick, two trees and a
// fallen log.
void buildTestScene(World& world);

// Self-test scenes.
// A block of sand hanging in mid-air above a stone floor — it must fall and
// settle into a pile with nothing floating.
void buildDropTestScene(World& world);

// A block of wood standing on a stone floor, lit in the middle — the fire must
// spread through it, burn it down and leave ash.
void buildBurnTestScene(World& world);
