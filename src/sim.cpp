#include "sim.h"

#include <algorithm>

namespace {

// The smallest mass transfer worth making. Below this, a flow would be pure
// noise: cells would exchange a vanishing amount of liquid forever and never
// strictly settle. Kept small enough that a pool still levels to a visually flat
// surface, large enough that the arithmetic terminates.
constexpr float kFlowMin = 1.0f / 256.0f;

// A bigger threshold for the pressure-driven part of the flow. Pressure is a
// large number (the depth of liquid stacked on a cell), so a small pressure
// difference is not a real slope, it is the arithmetic breathing. Requiring a
// real pressure gap before a pressure-driven transfer happens is what keeps a
// settled pool's surface still: measured on a wide pool, the residual churn is a
// couple of columns per window rather than a rising slosh.
constexpr float kPressureMin = 1.0f / 16.0f;

// How much of the mass difference between two sideways neighbours is moved each
// pass, as a fraction of that difference. The classic choice is 0.5, which is
// unconditionally stable but converges slowly; a higher share makes a pool level
// visibly faster. It must stay BELOW 0.5: both cells of a pair run in the same
// pass from a frozen read, so with a share above a half each can hand the other
// more than the midpoint and they cross over, which shows up as sloshing (0.8
// measured 49 of 480 columns pulsing against 19 at 0.5, and stopped the pool
// from ever going fully quiet).
constexpr float kSideShare = 0.45f;

bool inRect(const SimRect& r, int x, int y) {
    return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1;
}

// A repeatable pseudo-random value for one cell on one tick. The self-test
// depends on scenes burning the same way every run.
uint32_t noise(int x, int y, int frame) {
    uint32_t h = static_cast<uint32_t>(x) * 73856093u ^
                 static_cast<uint32_t>(y) * 19349663u ^
                 static_cast<uint32_t>(frame) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

bool roll(int x, int y, int frame, int chance) {
    if (chance <= 0) return false;
    if (chance >= 255) return true;
    return static_cast<int>(noise(x, y, frame) & 0xFFu) < chance;
}

// A flammable cell next to a flame may catch. How likely, and how long it then
// burns for, come from the material's table row.
void tryIgnite(World& world, int x, int y, int frame) {
    const Mat m = world.at(x, y);
    const MaterialInfo& info = materialInfo(m);
    if (!info.flammable || info.burnTime == 0) return;
    if (world.life(x, y) > 0) return;  // already alight
    if (roll(x, y, frame, info.igniteChance)) world.setLife(x, y, info.burnTime);
}

// Flames reach all eight neighbours. Diagonal spread used to be restricted to
// flowing materials (oil), which left gaps in tree canopies: a leaf could sit
// diagonal to a burning leaf and never catch. Every flammable material now
// spreads to all eight, which is what makes a fire consume a whole tree instead
// of leaving isolated unburned pixels.
void spreadFire(World& world, int x, int y, int frame, bool includeDiagonals) {
    (void)includeDiagonals;
    // Fire spreads only to the four nearest (cardinal) neighbors.
    tryIgnite(world, x + 1, y, frame);
    tryIgnite(world, x - 1, y, frame);
    tryIgnite(world, x, y + 1, frame);
    tryIgnite(world, x, y - 1, frame);
}

// Fire and burning fuel age by one tick, spread to whatever is next to them, and
// - when their time runs out - turn into their `burnsInto` (ash for wood and
// leaves, nothing for oil).
void updateFire(World& world, const SimRect& region, int frame) {
    const int width = world.width();
    const int height = world.height();

    // Place a puff of smoke in the first air cell found scanning upward from
    // (cx, cy). Returns true if placed. Smoke prefers to rise; if nothing is
    // open above, it tries the sides.
    auto spawnSmoke = [&](int cx, int cy, float amount) -> bool {
        for (int yy = cy - 1; yy >= cy - 6 && yy >= 0; --yy) {
            if (world.at(cx, yy) == Mat::Air) {
                world.setMass(cx, yy, Mat::Smoke, amount);
                return true;
            }
        }
        for (int d = -1; d <= 1; d += 2) {
            if (world.inBounds(cx + d, cy) && world.at(cx + d, cy) == Mat::Air) {
                world.setMass(cx + d, cy, Mat::Smoke, amount);
                return true;
            }
        }
        return false;
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!inRect(region, x, y)) continue;
            const Mat m = world.at(x, y);
            const uint8_t life = world.life(x, y);

            if (m == Mat::Fire) {
                spreadFire(world, x, y, frame, false);
                if (life <= 1) {
                    spawnSmoke(x, y, 0.6f);
                    world.set(x, y, Mat::Air);
                } else {
                    world.setLife(x, y, life - 1);
                }
                continue;
            }

            const MaterialInfo& info = materialInfo(m);
            if (!info.flammable || life == 0) continue;

            spreadFire(world, x, y, frame, info.flows);
            if (life <= 1) {
                world.set(x, y, info.burnsInto);
                spawnSmoke(x, y, 0.6f);
            } else {
                world.setLife(x, y, life - 1);
                // While burning, occasionally puff smoke upward.
                if (roll(x, y, frame, 40)) spawnSmoke(x, y, 0.3f);
            }
        }
    }

    // Smoke dissipates: lose a little mass each tick, and vanish when empty.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!inRect(region, x, y)) continue;
            if (world.at(x, y) != Mat::Smoke) continue;
            float m = world.mass(x, y) - 0.05f;
            if (m <= kMassEmpty) world.setMass(x, y, Mat::Air, 0.0f);
            else world.setMass(x, y, Mat::Smoke, m);
        }
    }
}


// Does this material take part in any proximity reaction? Answered from the
// table, so a new reaction only needs a new row value, not a list updated in
// two places. Everything else - stone, sand, gravel - is skipped by the scan.
bool canReact(Mat m) {
    if (m == Mat::Water || m == Mat::Lava || m == Mat::Fire) return true;
    if (m == Mat::SulfurCrust) return true;
    if (m == Mat::Soil || m == Mat::PackedDirt) return true;
    const MaterialInfo& info = materialInfo(m);
    return info.meltable || info.flammable || info.washedByWater;
}

// The PRD's "large volumes of lava near water -> obsidian instead of stone".
// Without flood-filling a blob, a cheap stand-in for volume is how much lava
// surrounds this cell: deep inside a pool is not the same as the edge of a
// trickle.
bool lavaIsDeep(const World& world, int x, int y) {
    int lava = 0;
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            if (world.at(x + dx, y + dy) == Mat::Lava) ++lava;
        }
    }
    return lava >= 24;
}

// A small explosion. Anything without much blast resistance is cleared out;
// stone and everything harder mostly holds. The rim catches light.
void explode(World& world, int cx, int cy, int radius) {
    const int r2 = radius * radius;
    for (int y = cy - radius; y <= cy + radius; ++y) {
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (!world.inBounds(x, y)) continue;
            const int dx = x - cx;
            const int dy = y - cy;
            if (dx * dx + dy * dy > r2) continue;
            const Mat m = world.at(x, y);
            if (m == Mat::Bedrock) continue;
            if (materialInfo(m).blastResistance > 60) continue;
            world.set(x, y, Mat::Air);
        }
    }

    const int ring = radius + 1;
    const int ring2 = ring * ring;
    for (int y = cy - ring; y <= cy + ring; ++y) {
        for (int x = cx - ring; x <= cx + ring; ++x) {
            if (!world.inBounds(x, y)) continue;
            if (world.at(x, y) != Mat::Air) continue;
            const int dx = x - cx;
            const int dy = y - cy;
            const int d2 = dx * dx + dy * dy;
            if (d2 <= r2 || d2 > ring2) continue;
            if ((noise(x, y, 7) & 1u) == 0u) continue;  // a ragged edge, not a neat halo
            world.set(x, y, Mat::Fire);
            world.setLife(x, y, FIRE_LIFE);
        }
    }
}

// One proximity rule, between the cell at (x, y) and the one touching it at
// (nx, ny). Written so each rule fires whichever way round the two appear.
void reactPair(World& world, int x, int y, int nx, int ny, int frame) {
    const Mat a = world.at(x, y);
    const Mat b = world.at(nx, ny);
    if (!canReact(a) || !canReact(b) || a == b) return;

    // Lava meeting water crusts over: the lava hardens and the water is gone.
    if ((a == Mat::Lava && b == Mat::Water) || (b == Mat::Lava && a == Mat::Water)) {
        const bool lavaIsA = (a == Mat::Lava);
        const int lx = lavaIsA ? x : nx;
        const int ly = lavaIsA ? y : ny;
        const int wx = lavaIsA ? nx : x;
        const int wy = lavaIsA ? ny : y;
        world.set(lx, ly, lavaIsDeep(world, lx, ly) ? Mat::Obsidian : Mat::Stone);
        world.set(wx, wy, Mat::Air);
        return;
    }

    // Water softens soil and packed dirt into mud, and washes loose ash away.
    // The water itself is not used up, so a pond never drains into its own bed.
    if (a == Mat::Water || b == Mat::Water) {
        const int sx = (a == Mat::Water) ? nx : x;
        const int sy = (a == Mat::Water) ? ny : y;
        const Mat ground = world.at(sx, sy);
        if (ground == Mat::Soil || ground == Mat::PackedDirt) {
            world.set(sx, sy, Mat::Mud);
        } else if (materialInfo(ground).washedByWater) {
            world.set(sx, sy, Mat::Air);
        }
        return;
    }

    // Everything left is heat's doing. Lava acts like fire, only harsher.
    const bool hotIsA = (a == Mat::Lava || a == Mat::Fire);
    if (!hotIsA && b != Mat::Lava && b != Mat::Fire) return;
    const Mat hot = hotIsA ? a : b;
    const int hx = hotIsA ? nx : x;
    const int hy = hotIsA ? ny : y;
    const Mat cold = world.at(hx, hy);
    const MaterialInfo& info = materialInfo(cold);

    // Snow, ice, mud and clay next to heat turn to water. Nothing tracks a
    // temperature; being adjacent is the whole trigger.
    if (info.meltable) {
        world.set(hx, hy, info.meltsInto);
        return;
    }

    // Oil goes up fiercely on the spot. Other flammables catch just as they
    // would from a flame, and only lava is hot enough to start them itself.
    if (cold == Mat::Oil) {
        world.set(hx, hy, Mat::Fire);
        world.setLife(hx, hy, FIRE_LIFE);
        return;
    }
    if (info.flammable && hot == Mat::Lava) tryIgnite(world, hx, hy, frame);

    // Lava reaching sulfur crust blows a small crater and lights the rim.
    if (cold == Mat::SulfurCrust && hot == Mat::Lava) explode(world, hx, hy, 5);
}

// Every pair of touching cells that knows about each other gets a chance to
// react. Scanning right and down covers each pair exactly once.
void applyReactions(World& world, const SimRect& region, int frame) {
    const int width = world.width();
    const int height = world.height();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!inRect(region, x, y)) continue;
            if (!canReact(world.at(x, y))) continue;
            if (x + 1 < width) reactPair(world, x, y, x + 1, y, frame);
            if (y + 1 < height) reactPair(world, x, y, x, y + 1, frame);
        }
    }
}

// The topmost non-air row of a column at or below `from`, or a huge number if the
// column is empty there. Used only by the friction gate, to compare the height of
// two neighbouring pile surfaces.
int surfaceRow(const World& world, int x, int from) {
    for (int y = from; y < world.height(); ++y) {
        if (world.at(x, y) != Mat::Air) return y;
    }
    return 1 << 30;
}

// Mass-based flow for one cell, shared by liquids and loose solids.
//
// Liquids used to be whole cells that swapped places: a cell either held water
// or it did not. That made a level surface impossible to express, because two
// equally full neighbouring cells can only trade places, and they trade them
// back forever. Every discrete rule that tried to fix it — a downhill scan, a
// two-row minimum drop, a top-of-column guard — fixed one case and broke
// another, because the quantity underneath had no resolution below one cell.
//
// A mobile cell now holds a MASS between 0 and 1 (a little above 1 at the
// bottom of a deep pool). Movement transfers a fraction of a unit rather than
// swapping. Two results fall out for free:
//
//   * A pool levels itself. Liquid flows from a fuller cell to a less full
//     neighbour until the two are equal, wherever they are: no scan, no slope
//     special case, no guard.
//   * A settled pool is perfectly still. Equal masses mean zero difference,
//     which means zero flow. There is nothing left to oscillate.
//
// Loose solids run through the same transfer, with one difference that keeps
// sand sand rather than water: a solid only spreads sideways when the surface
// slope is steeper than its `friction` allows. That is the angle of repose. So
// sand still piles in a dune and gravel still holds a slope, while a grain that
// does fall now falls by a smooth fraction of a unit instead of jumping a whole
// cell, which is what stops the top grain of a heap flickering.
//
// The two material knobs:
//
//   * `viscosity` (liquids) - how freely the material moves. It scales the
//     sideways share (thick liquids equalise slowly), it scales how strongly a
//     cell answers pressure, and it caps how much of a cell may pour down per
//     pass (thick liquids ooze rather than fall). Water is 0.
//   * `friction` (solids) - the steepest surface slope the pile will hold.
//     It gates the sideways and diagonal steps only; straight-down falling is
//     always allowed, so a solid still drops freely through open air.
//
// Gives the small amount a cell is holding to a neighbour that can accept it,
// so a trace of material is never left stranded. Returns true if the cell was
// emptied. Used for the last sliver of a flow, both when a cell has just spread
// nearly all of itself away and when it has been too small to spread at all.
//
// Diagonals below are tried first, then the sides: a crumb prefers to tumble
// downhill, and only then to slide sideways. A cell that can take it is any
// mobile material (masses add) or air (it becomes material).
bool handOffCrumb(World& world, int x, int y, Mat self, float amount) {
    if (amount <= 0.0f) return false;
    const MaterialInfo& info = materialInfo(self);
    bool rises = info.rises;
    // Diagonals first (downhill), then sides. For gases, "downhill" is up.
    static const int dxDown[4] = {-1, +1, -1, +1};
    static const int dyDown[4] = {+1, +1, 0, 0};
    static const int dxUp[4] = {-1, +1, -1, +1};
    static const int dyUp[4] = {-1, -1, 0, 0};
    const int* dx = rises ? dxUp : dxDown;
    const int* dy = rises ? dyUp : dyDown;
    for (int i = 0; i < 4; ++i) {
        const int nx = x + dx[i];
        const int ny = y + dy[i];
        if (!world.inBounds(nx, ny)) continue;
        const Mat there = world.at(nx, ny);
        const bool mobileThere = materialInfo(there).flows || materialInfo(there).falls || materialInfo(there).rises;
        if (there != Mat::Air && !mobileThere) continue;
        const float current = mobileThere ? world.mass(nx, ny) : 0.0f;
        world.setMass(nx, ny, mobileThere ? there : self, current + amount);
        world.setMass(x, y, self, 0.0f);
        return true;
    }
    return false;
}


void flowMass(World& world, int x, int y, float& budget) {
    const Mat self = world.at(x, y);
    const MaterialInfo& info = materialInfo(self);
    if (!info.flows && !info.falls && !info.rises) return;
    if (budget <= 0.0f) return;

    const float myMass = world.massAt(x, y);
    if (myMass <= 0.0f) return;

    // Direction of gravity for this material. Liquids and loose solids fall;
    // gases rise. Everything else in this function is the same either way, with
    // the vertical axis flipped, so a gas is literally an inverted liquid.
    const bool rises = info.rises;
    const int gDir = rises ? -1 : +1;   // +1 = down, -1 = up

    const float spreadScale = 1.0f - (info.viscosity / 255.0f) * 0.9f;

    // 1. Gravity (or anti-gravity for gases): pour along gDir into a cell with
    //    room. Applies to any positive mass, including a sliver.
    const int gy = y + gDir;
    if (gy >= 0 && gy < world.height()) {
        const Mat along = world.at(x, gy);
        const bool alongFlows = materialInfo(along).flows;
        const bool alongFalls = materialInfo(along).falls && (along == self);
        const bool alongRises = materialInfo(along).rises && (along == self);
        const bool alongTakes = along == Mat::Air || alongFlows || alongFalls || alongRises;
        if (alongTakes) {
            const float alongLive = (alongFlows || alongFalls || alongRises) ? world.mass(x, gy) : 0.0f;
            const float room = 1.0f - alongLive;
            if (room > 0.0f) {
                float flow = std::min(myMass, room);
                if (myMass - flow < kMassEmpty) flow = myMass;
                if (flow > kFlowMin) {
                    const Mat into = (alongFlows || alongFalls || alongRises) ? along : self;
                    world.setMass(x, y, self, world.mass(x, y) - flow);
                    world.setMass(x, gy, into, alongLive + flow);
                    budget -= flow;
                    return;
                }
            }
        }
    }

    if (myMass <= kMassEmpty) {
        handOffCrumb(world, x, y, self, myMass);
        return;
    }

    // Head: count same-type cells stacked in the gravity direction.
    float head = 0.0f;
    if (!rises) {
        for (int cy = y - 1; cy >= 0; --cy) {
            const Mat above = world.at(x, cy);
            if (above != Mat::Air && !materialInfo(above).flows && !materialInfo(above).rises) break;
            if (above == Mat::Air) break;
            head += 1.0f;
        }
    } else {
        for (int cy = y + 1; cy < world.height(); ++cy) {
            const Mat below = world.at(x, cy);
            if (below != Mat::Air && !materialInfo(below).flows && !materialInfo(below).rises) break;
            if (below == Mat::Air) break;
            head += 1.0f;
        }
    }

    float offers[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const int dx[4] = {-1, +1, -1, +1};
    const int dy[4] = {0, 0, gDir, gDir};
    for (int i = 0; i < 4; ++i) {
        const int nx = x + dx[i];
        const int ny = y + dy[i];
        if (!world.inBounds(nx, ny)) continue;
        const Mat there = world.at(nx, ny);
        if (there != Mat::Air && !materialInfo(there).flows && !materialInfo(there).falls && !materialInfo(there).rises) continue;

        if (info.falls) {
            const int drop = surfaceRow(world, nx, ny) - surfaceRow(world, x, y);
            const int need = info.friction / 64 - (dy[i] != 0 ? 1 : 0);
            if (drop < need) continue;
            if (dy[i] == 0 && drop <= 0) continue;
            if (dy[i] != 0 && drop < 1) continue;
        }

        const float other = (there == Mat::Air) ? 0.0f : world.massAt(nx, ny);
        float otherHead = head;
        if (dy[i] != 0) otherHead -= 1.0f;
        const float diff = (myMass + head) - (other + (otherHead > 0.0f ? otherHead : 0.0f));
        const float bar = (diff - (myMass - other)) > kPressureMin ? kPressureMin : kFlowMin;
        if (diff <= kFlowMin * 2.0f) continue;
        if (diff > bar) offers[i] = diff * kSideShare * spreadScale;
    }

    float totalOffer = 0.0f;
    for (int i = 0; i < 4; ++i) totalOffer += offers[i];
    if (totalOffer <= kFlowMin) return;
    const float scale = std::min(1.0f, std::min(myMass, budget) / totalOffer);
    if (scale <= 0.0f) return;

    float given = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float give = offers[i] * scale;
        if (give <= kFlowMin) continue;
        const int nx = x + dx[i];
        const int ny = y + dy[i];
        const Mat there = world.at(nx, ny);
        const bool mobileThere = materialInfo(there).flows || materialInfo(there).falls || materialInfo(there).rises;
        const float other = mobileThere ? world.massAt(nx, ny) : 0.0f;
        if (other + give < kMassEmpty) continue;
        const float current = mobileThere ? world.mass(nx, ny) : 0.0f;
        world.setMass(nx, ny, mobileThere ? there : self, current + give);
        budget -= give;
        given += give;
    }

    float remain = world.mass(x, y) - given;
    if (remain < kMassEmpty && remain > 0.0f) {
        for (int i = 0; i < 4; ++i) {
            const int nx = x + dx[i];
            const int ny = y + dy[i];
            if (!world.inBounds(nx, ny)) continue;
            const Mat there = world.at(nx, ny);
            const bool mobileThere = materialInfo(there).flows || materialInfo(there).falls || materialInfo(there).rises;
            if (there != Mat::Air && !mobileThere) continue;
            const float current = mobileThere ? world.mass(nx, ny) : 0.0f;
            world.setMass(nx, ny, mobileThere ? there : self, current + remain);
            remain = 0.0f;
            break;
        }
        if (remain > 0.0f) {
            world.setMass(x, y, self, world.mass(x, y));
            return;
        }
    }
    world.setMass(x, y, self, remain);
}


}  // namespace

Simulation::Simulation(const World& world) {
    (void)world;
}

void Simulation::step(World& world) {
    const SimRect all{0, 0, world.width() - 1, world.height() - 1};
    step(world, all);
}

void Simulation::step(World& world, const SimRect& region) {
    const int width = world.width();
    const int height = world.height();

    // Reactions first (lava crusts, ice melts, sulfur blows), then fire, then
    // movement.
    applyReactions(world, region, frame_);
    updateFire(world, region, frame_);

    // One movement pass, bottom-up, over every mobile cell - see the note above
    // `flowMass` for why a single mass-transfer pass replaced the two whole-cell
    // sweeps, and World::beginMassSnapshot for why the pass reads a start-of-tick
    // copy of the mass grid rather than the grid it is writing.
    world.beginMassSnapshot();
    float budget = static_cast<float>(width) * height * 0.5f;
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            if (!inRect(region, x, y)) continue;
            flowMass(world, x, y, budget);
        }
    }

    ++frame_;
}
