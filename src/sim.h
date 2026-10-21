#pragma once

#include <cstdint>
#include <vector>

#include "world.h"

// The per-pixel physics. One call to step() advances the world by one tick:
// reactions, then fire, then one movement pass over every mobile cell.
//
// Rules (see devpost/spec.md > Components > Simulation):
//   * Materials react to whatever touches them, with no temperature tracked at
//     all: lava meets water and crusts into stone (obsidian when the lava is
//     deep), heat melts snow, ice, mud and clay into water, water turns soil
//     to mud and washes ash away, and lava on sulfur crust blows a crater.
//   * Fire spreads to neighbouring flammable cells (see their `igniteChance`),
//     burns for as long as their `burnTime`, then leaves `burnsInto` behind.
//   * Every MOBILE material - liquids and loose solids alike - is a continuous
//     quantity, not a whole cell. A cell holds a mass between 0 and 1 (a little
//     above 1 when compressed) and moves by TRANSFERRING a fraction of a unit,
//     never by swapping places. It pours down into a cell with room, and spreads
//     toward neighbours that are lower.
//
//     This is what makes a pool level and stay level. Whole-cell materials could
//     not express a flat surface: two equally full neighbours can only trade
//     places, which they do forever, so the surface jitters and a slope freezes.
//     With mass, equal neighbours have a difference of zero and therefore do
//     nothing at all, and any inequality flows itself flat.
//   * Two material properties shape the movement, both plain numbers in the
//     table in materials.cpp:
//       - `viscosity` (liquids): how freely the material spreads. 0 is water,
//         255 is cold mud. It scales the sideways share, so thick liquids ooze
//         to a level slowly and thin ones snap flat.
//       - `friction` (loose solids): the angle of repose. A solid only spreads
//         sideways when the neighbour surface is steeper than this, which is
//         what keeps sand a dune and gravel a slope while water levels. A solid
//         still falls straight down freely whatever its friction.
//   * One pass per tick, bottom-up, over every mobile cell. No second sweep and
//     no moved-mark: a transfer leaves no hole to refill and is computed once,
//     from a frozen read, so it cannot be double-counted.
class Simulation {
public:
    explicit Simulation(const World& world);

    // Advances everything that exists. Used by the self-tests, which build
    // small worlds.
    void step(World& world);

    // Advances just the region. Used by the game, so that the pixels a long way
    // off-screen are left exactly as they are.
    void step(World& world, const SimRect& region);

    int frame() const { return frame_; }

private:
    int frame_ = 0;
};
