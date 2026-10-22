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
void explode(World& world, int cx, int cy, std::vector<BlastEvent>* blasts) {
    BlastOptions options;
    options.radius = 8;
    options.power = 0.9f;
    options.smokePuffs = 8;
    blast(world, cx, cy, options);
    if (blasts) blasts->push_back(BlastEvent{cx, cy, options.radius});
}

// One proximity rule, between the cell at (x, y) and the one touching it at
// (nx, ny). Written so each rule fires whichever way round the two appear.
void reactPair(World& world, int x, int y, int nx, int ny, int frame,
                std::vector<BlastEvent>* blasts) {
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
    //
    // Mud holds its shape. That matters more than it sounds: if wet earth were
    // allowed to run away like a liquid, the layer of mud would flow out of
    // the pond, the water would follow it down into the fresh dirt underneath,
    // that dirt would turn to mud and run away too, and the whole hillside
    // would be washed into the sea. One wet layer, and no more.
    if (a == Mat::Water || b == Mat::Water) {
        const int sx = (a == Mat::Water) ? nx : x;
        const int sy = (a == Mat::Water) ? ny : y;
        const Mat ground = world.at(sx, sy);
        if (ground == Mat::Soil || ground == Mat::PackedDirt) {
            // The soil keeps its amount; it is only the material that changes.
            const float amount = world.mass(sx, sy);
            if (amount > 0.0f) world.setMass(sx, sy, Mat::Mud, amount);
            else world.set(sx, sy, Mat::Mud);
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
    if (cold == Mat::SulfurCrust && hot == Mat::Lava) explode(world, hx, hy, blasts);
}

// Every pair of touching cells that knows about each other gets a chance to
// react. Scanning right and down covers each pair exactly once.
void applyReactions(World& world, const SimRect& region, int frame,
                    std::vector<BlastEvent>* blasts) {
    for (int y = region.y0; y <= region.y1; ++y) {
        for (int x = region.x0; x <= region.x1; ++x) {
            if (!canReact(world.at(x, y))) continue;
            reactPair(world, x, y, x + 1, y, frame, blasts);
            reactPair(world, x, y, x, y + 1, frame, blasts);
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
// Can a cell that holds `there` take material of kind `incoming`?
//
// A cell holds one kind of material and only ever one kind. Air takes
// anything and becomes it. A cell already holding the same kind takes more of
// the same and stays that kind. A cell holding anything else refuses.
//
// This is the rule that keeps water water. Pouring water over mud used to
// turn the water into mud as it spread, because the receiving cell decided
// what it wanted to be rather than what was being poured into it: a pool of
// water touching a bed of soil lost its own mass a little at a time and the
// ground turned to mud and drained away. Materials move; they do not change
// what they are.
bool takesMaterial(Mat there, Mat incoming) {
    return there == Mat::Air || there == incoming;
}

// Gives the small amount a cell is holding to a neighbour that can accept it,
// so a trace of material is never left stranded. Returns true if the cell was
// emptied. Used for the last sliver of a flow, both when a cell has just spread
// nearly all of itself away and when it has been too small to spread at all.
//
// Diagonals below are tried first, then the sides: a crumb prefers to tumble
// downhill, and only then to slide sideways. A cell that can take it is empty
// air (it becomes material) or already holds the same material (masses add).
// A neighbour that is already full is passed over: a cell holds one unit and
// no more, and a crumb is not worth pushing one over.
bool handOffCrumb(World& world, int x, int y, Mat self, float amount) {
    if (amount <= 0.0f) return false;
    static const int dx[4] = {-1, +1, -1, +1};
    static const int dy[4] = {+1, +1, 0, 0};
    for (int i = 0; i < 4; ++i) {
        const int nx = x + dx[i];
        const int ny = y + dy[i];
        if (!world.inBounds(nx, ny)) continue;
        const Mat there = world.at(nx, ny);
        if (!takesMaterial(there, self)) continue;
        const float current = (there == Mat::Air) ? 0.0f : world.mass(nx, ny);
        if (current + amount > 1.0f) continue;
        world.setMass(nx, ny, self, current + amount);
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

    // 1. Gravity (or anti-gravity for gases).
    //
    //    The two kinds of mobile material move by different rules, because they
    //    are different things:
    //
    //    * A LOOSE SOLID is a heap of grains. It moves as whole cells: it drops
    //      one row a tick, it sinks through a liquid and pushes that liquid up
    //      as it goes, and it settles into a slope instead of levelling out.
    //    * A LIQUID is a body of fluid. It moves by transferring a fraction of a
    //      unit, so it can be half full and spread out, and a pool levels
    //      itself.
    //
    //    A liquid never changes into a solid by being poured at one, and a
    //    solid never changes into a liquid by sinking into one. The only place
    //    two materials exchange places is a solid sinking into a liquid, and
    //    that is a straight swap: the solid goes down, the liquid comes up, and
    //    both amounts are carried across unchanged.
    const int gy = y + gDir;
    if (gy >= 0 && gy < world.height()) {
        const Mat along = world.at(x, gy);
        const bool alongIsAir = (along == Mat::Air);
        const bool alongIsLiquid = materialInfo(along).flows;

        if (info.falls) {
            // -- A loose solid drops as a whole cell.
            //
            // It falls into air. It sinks into a liquid, which is displaced
            // upwards into the cell the grain just left - that is what makes a
            // stone dropped in a pond sink instead of floating on it, and it is
            // a straight swap: the grain goes down, the liquid comes up, and
            // both amounts are carried across unchanged.
            //
            // It does NOT sink into another solid. Sand does not quietly work
            // its way down through the dirt it was dropped on; it piles up on
            // top of it, which is what a heap of sand is supposed to do.
            const bool sinksIntoLiquid = alongIsLiquid && self != Mat::PackedDirt;
            if (alongIsAir || sinksIntoLiquid) {
                const float solidMass = world.mass(x, y);
                if (sinksIntoLiquid) {
                    const float liquidMass = world.mass(x, gy);
                    world.setMass(x, gy, self, solidMass);
                    world.setMass(x, y, along, liquidMass);
                } else {
                    world.setMass(x, gy, self, solidMass);
                    world.setMass(x, y, self, 0.0f);
                }
                budget -= solidMass;
                return;
            }
        } else {
            // -- A liquid (or a gas) pours along gDir into air, or into more of
            //    itself. It will not pour into a cell holding a different
            //    material: that would change one material into another.
            if (alongIsAir || along == self) {
                // How full the cell below is: the larger of what it held at the
                // start of the tick and what it holds now, so a cell that has
                // already been filled this tick is never overfilled.
                const float alongMass = std::max(world.massAt(x, gy), world.mass(x, gy));
                const float room = 1.0f - alongMass;
                if (room > 0.0f) {
                    float flow = std::min(myMass, room);
                    // A sliver too small to be a cell of its own goes down whole
                    // rather than being left behind - but never by more than the
                    // cell below can hold.
                    if (myMass - flow < kMassEmpty) flow = std::min(myMass, room);
                    if (flow > kFlowMin) {
                        const float alongNow = alongIsAir ? 0.0f : world.mass(x, gy);
                        world.setMass(x, y, self, world.mass(x, y) - flow);
                        world.setMass(x, gy, alongIsAir ? self : along, alongNow + flow);
                        budget -= flow;
                        return;
                    }
                }
            }
        }
    }

    if (info.falls) {
        // -- A loose solid spreads by moving WHOLE grains.
        //
        // A heap of sand is made of grains, not of fractions of a grain, so a
        // grain never splits: it either moves to the next cell or it stays
        // where it is. It only ever moves into empty air.
        //
        // Whether it is allowed to slide is decided by the slope. `drop` is how
        // many rows lower the ground is in the neighbour's column compared with
        // the ground under this grain. A grain slides diagonally down a slope
        // of at least `need` rows, and `need` comes from the material's
        // friction: sand (110) slides on a one row step and forms a dune, gravel
        // (140) needs two and holds a steeper bank, clay (200) needs three and
        // stands almost upright. That is the angle of repose.
        const int mySupport = surfaceRow(world, x, y + 1);
        const int need = std::max(1, info.friction / 64);
        // Which side a grain tries first is fixed by its position, so a heap
        // settles into a shape instead of shuffling back and forth.
        static const int dx[4] = {-1, +1, -1, +1};
        static const int dy[4] = {+1, +1, 0, 0};
        static const int leftFirst[4] = {0, 1, 2, 3};
        static const int rightFirst[4] = {1, 0, 3, 2};
        const bool preferLeft = (noise(x, y, 0) & 1u) == 0u;
        const int* order = preferLeft ? leftFirst : rightFirst;
        for (int k = 0; k < 4; ++k) {
            const int i = order[k];
            const int nx = x + dx[i];
            const int ny = y + dy[i];
            if (!world.inBounds(nx, ny)) continue;
            if (world.at(nx, ny) != Mat::Air) continue;
            const int theirGround = surfaceRow(world, nx, ny);
            if (theirGround - mySupport < need) continue;
            const float solidMass = world.mass(x, y);
            world.setMass(nx, ny, self, solidMass);
            world.setMass(x, y, self, 0.0f);
            budget -= solidMass;
            return;
        }
        return;
    }

    if (myMass <= kMassEmpty) {
        handOffCrumb(world, x, y, self, world.mass(x, y));
        return;
    }

    // Head: how deep this cell sits under the liquid above it. This is the
    // pressure of the column, and it is what makes a pool spread out to its
    // edges instead of sitting in a hole.
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
        // A cell only takes material of its own kind, or is empty air.
        if (!takesMaterial(there, self)) continue;

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
        if (diff <= bar) continue;

        // A cell holds one unit, no more. A neighbour that is already full is
        // not offered anything: without this a pool squashes itself into fewer
        // and ever fuller cells, so a lake in a stone cup sinks to half its
        // depth instead of staying the depth it was poured at.
        const float room = 1.0f - std::max(other, world.mass(nx, ny));
        if (room <= 0.0f) continue;
        offers[i] = std::min(diff * kSideShare * spreadScale, room);
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
        if (!takesMaterial(there, self)) continue;
        const float other = (there == Mat::Air) ? 0.0f : world.massAt(nx, ny);
        if (other + give < kMassEmpty) continue;
        const float current = (there == Mat::Air) ? 0.0f : world.mass(nx, ny);
        // The neighbour may have been filled by somebody else earlier in this
        // same pass, so the room is checked again here, not just when the
        // offer was worked out.
        const float room = 1.0f - current;
        if (room <= 0.0f) continue;
        const float actual = std::min(give, room);
        world.setMass(nx, ny, self, current + actual);
        budget -= actual;
        given += actual;
    }

    // Whatever is left stays here. If that is a sliver too small to be a real
    // cell, it is handed to a neighbour that will take it - and if no
    // neighbour will, the sliver simply stays. It is never multiplied: the
    // amount that went out is subtracted from what came in, never forgotten.
    float remain = world.mass(x, y) - given;
    if (remain < kMassEmpty && remain > 0.0f) {
        for (int i = 0; i < 4; ++i) {
            const int nx = x + dx[i];
            const int ny = y + dy[i];
            if (!world.inBounds(nx, ny)) continue;
            const Mat there = world.at(nx, ny);
            if (!takesMaterial(there, self)) continue;
            const float current = (there == Mat::Air) ? 0.0f : world.mass(nx, ny);
            if (current + remain > 1.0f) continue;
            world.setMass(nx, ny, self, current + remain);
            remain = 0.0f;
            break;
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

    // Each step starts with a clean sheet: whatever exploded on this step is
    // reported, and yesterday's is forgotten.
    blastEvents_.clear();

    // Reactions first (lava crusts, ice melts, sulfur blows), then fire, then
    // movement.
    applyReactions(world, region, frame_, &blastEvents_);
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
