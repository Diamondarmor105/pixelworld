#pragma once

#include "world.h"

// How an explosion behaves.
//
// Every explosion in the game follows the same rules, so a stick of dynamite
// and a splash of lava hitting sulfur crust feel like the same kind of event.
//
// The rule the whole thing is built on: the blast carries a fixed amount of
// energy outward from its centre, and the blast looks along a straight line
// from the centre to every cell it can reach. Breaking a block costs that
// block's blast resistance, and whatever energy is left after a wall has been
// broken carries on to the next block along the same line.
//
// So a wall neither stops the blast outright nor lets it through for free. It
// is broken first, and what is behind it is left with only the remainder. A
// wall the blast cannot afford to break shields everything behind it.
//
// The radius says how far the blast reaches. The energy says how much material
// it can chew through on the way there.
struct BlastOptions {
    int radius = 16;          // how far the blast reaches, in cells
    float power = 1.0f;       // energy; 1.0 is a full blast of 380 units
    bool underwater = false;  // in water: half the radius, and no flames
    bool flames = true;       // set the hole on fire
    bool pushLiquids = true;  // shove liquids aside rather than deleting them
    int smokePuffs = 16;      // how many smoke clouds to puff out
};

// Blows a hole in the world around (cx, cy).
void blast(World& world, int cx, int cy, const BlastOptions& options);
