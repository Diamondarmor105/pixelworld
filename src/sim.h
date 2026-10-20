#pragma once

#include <cstdint>
#include <vector>

#include "world.h"

// The part of the grid the simulation works on this tick, in cells, inclusive at
// both ends. Only the pixels near the player are simulated; the rest of the
// world is held still until it comes into view.
struct SimRect {
    int x0;
    int y0;
    int x1;
    int y1;
};

// The per-pixel physics. One call to step() advances the world by one tick:
// reactions, then fire, then every loose solid and liquid in the region gets
// one chance to fall or flow.
//
// Rules (see devpost/spec.md > Components > Simulation):
//   * Materials react to whatever touches them, with no temperature tracked at
//     all: lava meets water and crusts into stone (obsidian when the lava is
//     deep), heat melts snow, ice, mud and clay into water, water turns soil
//     to mud and washes ash away, and lava on sulfur crust blows a crater.
//   * Fire spreads to neighbouring flammable cells (see their `igniteChance`),
//     burns for as long as their `burnTime`, then leaves `burnsInto` behind.
//   * A loose solid tries straight down, then down-left, then down-right.
//   * A liquid is a continuous quantity, not a whole cell: every liquid cell
//     holds a mass between 0 and 1 (a little above 1 when compressed). It moves
//     by TRANSFERRING a fraction of a unit, never by swapping places. It pours
//     down into a cell with room, and it equalises sideways by giving half the
//     difference to a less filled neighbour.
//
//     This is what makes a pool level and stay level. Whole-cell liquids could
//     not express a flat surface: two equally full neighbours can only trade
//     places, which they do forever, so the surface jitters and a slope freezes.
//     With mass, equal neighbours have a difference of zero and therefore do
//     nothing at all, and any inequality flows itself flat. There is no pressure
//     scan, no depth measurement, no minimum drop and no periodic tidying pass.
//   * A material can move into empty space, or swap with a lighter loose solid,
//     so sand sinks through water and burning oil keeps burning as it flows.
//   * Anything that moved is marked for the rest of the tick, so nothing moves
//     twice in one frame. Liquids do not use that mark: a mass transfer is not a
//     move, and a cell may legitimately both receive and give in one tick.
//   * Solids run first, bottom-up, then liquids, bottom-up. Liquids need only
//     one pass because a transfer is symmetric and converges.
class Simulation {
public:
    explicit Simulation(const World& world);

    // Advances the whole grid. Used by the self-tests, which build small worlds.
    void step(World& world);

    // Advances just the region. Used by the game, so that the pixels a long way
    // off-screen are left exactly as they are.
    void step(World& world, const SimRect& region);

    int frame() const { return frame_; }

private:
    std::vector<uint8_t> moved_;
    int frame_ = 0;
};
