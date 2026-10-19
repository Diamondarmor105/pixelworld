#pragma once

#include <cstdint>
#include <vector>

#include "world.h"

// The per-pixel physics. One call to step() advances the whole world by one
// tick: every loose solid and liquid gets one chance to fall or flow.
//
// Rules (see devpost/spec.md > Components > Simulation):
//   * A loose solid tries straight down, then down-left, then down-right.
//   * A liquid does the same, then also tries sideways, and looks along the
//     surface up to `flowSlack` cells for somewhere lower to go, so pools find
//     their own level instead of heaping up.
//   * A material can move into empty space, or swap with a lighter loose solid
//     or liquid, so sand sinks through water.
//   * Anything that moved is marked for the rest of the tick, so nothing moves
//     twice in one frame.
//   * The horizontal scan direction alternates each tick, so material does not
//     drift consistently to one side.
class Simulation {
public:
    explicit Simulation(const World& world);

    void step(World& world);

    int frame() const { return frame_; }

private:
    std::vector<uint8_t> moved_;
    int frame_ = 0;
};
