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
//   * A liquid tries straight down, then down-diagonal, then sideways. On top of
//     that, a liquid cell at the top of its column may step sideways into the
//     liquid of a neighbouring column when that column's surface is at least
//     two rows lower. That is the one move the plain rules lack, and it is what
//     lets a slope cascade instead of standing forever; without it, water
//     poured into a stepped basin settles as a 45-degree staircase that never
//     changes again. The two-row allowance is what keeps a settled pool still:
//     a pool's own surface ruffles by about a row, and treating a one-row
//     difference as a slope made every surface cell hop between columns.
//     All: there is no pressure, no depth measurement and no periodic tidying
//     pass. Water therefore behaves like a very slippery granular material - it
//     runs downhill and spreads out, but a thin film on a flat floor may still
//     stop short of the far wall rather than creeping all the way.
//   * A material can move into empty space, or swap with a lighter loose solid
//     or liquid, so sand sinks through water and burning oil keeps burning as
//     it flows. A liquid that cannot fall or step aside shoves a lighter grain
//     out of the way instead.
//   * Anything that moved is marked for the rest of the tick, so nothing moves
//     twice in one frame.
//   * A tick runs the rules in two sweeps: bottom-up (which does the falling)
//     then top-down (which spreads material sideways into the gaps the first
//     sweep left). The scan order is fixed, and each cell instead has a standing
//     left/right preference of its own taken from its coordinates, so material
//     does not drift consistently to one side. Alternating the scan direction
//     each tick instead of the preference is what used to make a pool surface
//     flicker: the edge settled into a different shape on alternate ticks.
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
    // Rows the falling sweep moved something in. The sideways sweep only
    // revisits these, so a still or empty scene does not pay for a second pass.
    std::vector<uint8_t> dirtyRows_;
    int frame_ = 0;
};
