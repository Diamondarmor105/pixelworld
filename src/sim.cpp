#include "sim.h"

#include "blast.h"

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
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            tryIgnite(world, x + dx, y + dy, frame);
        }
    }
}

// Fire and burning fuel age by one tick, spread to whatever is next to them, and
// - when their time runs out - turn into their `burnsInto` (ash for wood and
// leaves, nothing for oil).
void updateFire(World& world, const SimRect& region, int frame) {
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

    // Only the pixels inside the region are looked at: a fire burning far
    // off-screen is left exactly as it is until it comes into view.
    for (int y = region.y0; y <= region.y1; ++y) {
        for (int x = region.x0; x <= region.x1; ++x) {
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
    for (int y = region.y0; y <= region.y1; ++y) {
        for (int x = region.x0; x <= region.x1; ++x) {
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
    if (m == Mat::Acid) return true;
    const MaterialInfo& info = materialInfo(m);
    return info.meltable || info.flammable || info.washedByWater || info.acidCorrodes;
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

// A small explosion, blown by lava reaching sulfur crust. It uses the same
// rules as every other explosion in the game: the wave breaks the blocks in
// the line of sight first, and what is left of the energy carries on to the
// next block. A stone wall near the sulfur is broken; a wall the blast cannot
// afford shields everything behind it.
void explode(World& world, int cx, int cy) {
    BlastOptions options;
    options.radius = 8;
    options.power = 0.9f;
    options.smokePuffs = 8;
    blast(world, cx, cy, options);
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

    // Acid eats through anything marked as vulnerable to it, and is used up
    // doing the eating: a splash of acid can carve a hole and then be gone.
    // The acid spends two thirds of what it has left on every cell it
    // dissolves, so a thin film of acid nibbles away slowly while a thick
    // pool bites deep.
    if (a == Mat::Acid || b == Mat::Acid) {
        const bool acidIsA = (a == Mat::Acid);
        const int ax = acidIsA ? x : nx;
        const int ay = acidIsA ? y : ny;
        const int tx = acidIsA ? nx : x;
        const int ty = acidIsA ? ny : y;
        const Mat target = world.at(tx, ty);
        if (target != Mat::Acid && materialInfo(target).acidCorrodes) {
            world.set(tx, ty, Mat::Air);
            const float mass = world.mass(ax, ay);
            const float left = mass / 3.0f;
            if (left > kMassEmpty) world.setMass(ax, ay, Mat::Acid, left);
            else world.setMass(ax, ay, Mat::Air, 0.0f);
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

    // Lava reaching sulfur crust blows a crater and lights the rim.
    if (cold == Mat::SulfurCrust && hot == Mat::Lava) explode(world, hx, hy);
}

// Every pair of touching cells that knows about each other gets a chance to
// react. Scanning right and down covers each pair exactly once.
void applyReactions(World& world, const SimRect& region, int frame) {
    for (int y = region.y0; y <= region.y1; ++y) {
        for (int x = region.x0; x <= region.x1; ++x) {
            if (!canReact(world.at(x, y))) continue;
            reactPair(world, x, y, x + 1, y, frame);
            reactPair(world, x, y, x, y + 1, frame);
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
    static const int dx[4] = {-1, +1, -1, +1};
    static const int dy[4] = {+1, +1, 0, 0};
    for (int i = 0; i < 4; ++i) {
        const int nx = x + dx[i];
        const int ny = y + dy[i];
        if (!world.inBounds(nx, ny)) continue;
        const Mat there = world.at(nx, ny);
        const bool mobileThere = materialInfo(there).flows || materialInfo(there).falls;
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
        bool alongTakes = along == Mat::Air || alongFlows || alongFalls || alongRises;
        if (info.falls && materialInfo(along).flows && self != Mat::PackedDirt) alongTakes = true;
        if (alongTakes) {
            const float alongMass = ((info.falls && materialInfo(along).flows) ? 0.0f :
                                    ((alongFlows || alongFalls || alongRises) ? world.massAt(x, gy) : 0.0f));
            const float room = 1.0f - alongMass;
            if (room > 0.0f) {
                float flow = std::min(myMass, room);
                if (myMass - flow < kMassEmpty) flow = myMass;
                if (flow > kFlowMin) {
                    bool solidIntoLiquid = info.falls && materialInfo(along).flows && self != Mat::PackedDirt;
                    float liquidMass = solidIntoLiquid ? world.mass(x, gy) : 0.0f;
                    Mat liquidMat = along;
                    const Mat into = solidIntoLiquid ? self : ((alongFlows || alongFalls || alongRises) ? along : self);
                    const float alongNow = solidIntoLiquid ? 0.0f : ((alongFlows || alongFalls || alongRises) ? world.mass(x, gy) : 0.0f);
                    world.setMass(x, y, self, world.mass(x, y) - flow);
                    world.setMass(x, gy, into, alongNow + flow);
                    // Displace liquid upward when solid sinks into it.
                    if (solidIntoLiquid && liquidMass > 0.0f) {
                        // Find first air cell above.
                        for (int yy = gy - 1; yy >= 0; --yy) {
                            if (world.at(x, yy) == Mat::Air) {
                                world.setMass(x, yy, liquidMat, liquidMass);
                                break;
                            }
                        }
                    }
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
    // Everything that exists: the self-tests build small worlds and want the
    // whole of them simulated.
    step(world, world.bounds());
}

void Simulation::step(World& world, const SimRect& region) {
    if (region.x1 < region.x0 || region.y1 < region.y0) return;

    // Reactions first (lava crusts, ice melts, sulfur blows), then fire, then
    // movement.
    applyReactions(world, region, frame_);
    updateFire(world, region, frame_);

    // One movement pass, bottom-up, over every mobile cell - see the note above
    // `flowMass` for why a single mass-transfer pass replaced the two whole-cell
    // sweeps, and World::beginMassSnapshot for why the pass reads a start-of-tick
    // copy of the mass grid rather than the grid it is writing.
    world.beginMassSnapshot(region);
    const float area = static_cast<float>(region.x1 - region.x0 + 1) *
                       static_cast<float>(region.y1 - region.y0 + 1);
    float budget = area * 0.5f;
    for (int y = region.y1; y >= region.y0; --y) {
        for (int x = region.x0; x <= region.x1; ++x) {
            flowMass(world, x, y, budget);
        }
    }

    ++frame_;
}
