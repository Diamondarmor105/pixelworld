#include "sim.h"

#include <algorithm>

namespace {

// Can material `from` move into a cell holding `into`?
// Empty space always yes; a lighter liquid is pushed aside, and a liquid also
// pushes through a lighter grain (so melt-water can percolate through snow);
// but loose solids give way only to liquids. Grains stack in landing order -
// falling sand settles on top of dirt instead of flipping the dirt up through
// the pile - and anything solid blocks.
bool canDisplace(Mat from, Mat into) {
    if (into == Mat::Air) return true;
    const MaterialInfo& target = materialInfo(into);
    if (!target.falls && !target.flows) return false;
    if (materialInfo(from).density <= target.density) return false;
    return target.flows || materialInfo(from).flows;
}

bool isOpen(const World& world, int x, int y) {
    return world.at(x, y) == Mat::Air;
}

bool inRect(const SimRect& r, int x, int y) {
    return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1;
}

bool isMoved(const World& world, const std::vector<uint8_t>& moved, int x, int y) {
    return moved[static_cast<size_t>(y) * world.width() + x] != 0;
}

void markMoved(const World& world, std::vector<uint8_t>& moved, int x, int y) {
    moved[static_cast<size_t>(y) * world.width() + x] = 1;
}

// Swaps two cells if it is legal and neither has moved this tick.
bool trySwap(World& world, std::vector<uint8_t>& moved, int x, int y, int tx, int ty) {
    if (!world.inBounds(tx, ty)) return false;
    if (isMoved(world, moved, tx, ty)) return false;

    const Mat from = world.at(x, y);
    const Mat into = world.at(tx, ty);
    if (!canDisplace(from, into)) return false;

    // `life` travels with the material: burning oil keeps burning as it flows.
    const uint8_t lifeFrom = world.life(x, y);
    const uint8_t lifeInto = world.life(tx, ty);

    world.set(x, y, into);
    world.set(tx, ty, from);
    world.setLife(x, y, lifeInto);
    world.setLife(tx, ty, lifeFrom);

    // Only the destination is flagged. The cell that moved cannot move again,
    // but the gap it left is free to be filled straight away - by the cell
    // above falling into it, in this same tick. Flagging the gap too is what
    // once let holes survive a whole tick, shuttle around inside a pool of
    // water and make it shimmer instead of settling.
    markMoved(world, moved, tx, ty);
    return true;
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

// Flames reach the four neighbours, and for materials that flow (oil, and later
// lava) the diagonals too, so fire creeps further through a liquid.
void spreadFire(World& world, int x, int y, int frame, bool includeDiagonals) {
    tryIgnite(world, x + 1, y, frame);
    tryIgnite(world, x - 1, y, frame);
    tryIgnite(world, x, y + 1, frame);
    tryIgnite(world, x, y - 1, frame);
    if (includeDiagonals) {
        tryIgnite(world, x + 1, y + 1, frame);
        tryIgnite(world, x - 1, y + 1, frame);
        tryIgnite(world, x + 1, y - 1, frame);
        tryIgnite(world, x - 1, y - 1, frame);
    }
}

// Fire and burning fuel age by one tick, spread to whatever is next to them, and
// - when their time runs out - turn into their `burnsInto` (ash for wood and
// leaves, nothing for oil).
void updateFire(World& world, const SimRect& region, int frame) {
    const int width = world.width();
    const int height = world.height();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!inRect(region, x, y)) continue;
            const Mat m = world.at(x, y);
            const uint8_t life = world.life(x, y);

            if (m == Mat::Fire) {
                spreadFire(world, x, y, frame, false);
                if (life <= 1) world.set(x, y, Mat::Air);
                else world.setLife(x, y, life - 1);
                continue;
            }

            const MaterialInfo& info = materialInfo(m);
            if (!info.flammable || life == 0) continue;

            spreadFire(world, x, y, frame, info.flows);
            if (life <= 1) world.set(x, y, info.burnsInto);
            else world.setLife(x, y, life - 1);
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

// A liquid that has liquid stacked on top of it is under pressure. It scans up
// to `reach` cells sideways for an empty cell and takes one step toward the
// nearest one. Pressure grows with depth, so a deep pool pushes out to find its
// level while a shallow surface layer stays where it is.
bool flowTowardEmpty(World& world, std::vector<uint8_t>& moved, int x, int y, int dir, int reach) {
    const Mat self = world.at(x, y);
    for (int distance = 1; distance <= reach; ++distance) {
        const int px = x + dir * distance;
        if (!world.inBounds(px, y)) return false;
        if (!canDisplace(self, world.at(px, y))) return false;  // a wall, stop looking
        if (isOpen(world, px, y)) {
            return trySwap(world, moved, x, y, x + dir, y);
        }
    }
    return false;
}

// Pressured liquid shoves a lighter grain sideways instead of being stopped by
// it. This is how melt-water works its way out of a snow bank: the pile looks
// solid, but the water is heavier and pushes through.
bool shoveGrain(World& world, std::vector<uint8_t>& moved, int x, int y, int dir) {
    const int tx = x + dir;
    if (!world.inBounds(tx, y)) return false;
    const Mat target = world.at(tx, y);
    if (target == Mat::Air) return false;                          // handled by flowTowardEmpty
    if (materialInfo(target).flows) return false;                  // liquids sort out by density
    if (!canDisplace(world.at(x, y), target)) return false;
    return trySwap(world, moved, x, y, tx, y);
}

// A surface cell - nothing stacked on top, so no pressure. It may shuffle one
// step sideways into an empty cell, but only if its own floor stays put: the
// cell below it must hold liquid, and the cell below the destination must be
// empty. In other words it can spill over an edge into air, and never hover a
// cell higher afterwards. Strictly one-way, so a settled pool is perfectly
// still - an earlier "fill the notch beside me" version let cells hop back and
// forth one row up and down forever, which is exactly the shimmer this
// replaced, and every pool rim shimmered.
bool spillOverEdge(World& world, std::vector<uint8_t>& moved, int x, int y, int dir) {
    const int tx = x + dir;
    if (!world.inBounds(tx, y)) return false;
    if (world.at(tx, y) != Mat::Air) return false;
    if (!materialInfo(world.at(x, y + 1)).flows) return false;  // no floor of my own to step off
    if (materialInfo(world.at(tx, y + 1)).flows) return false;  // over a notch, not an edge
    return trySwap(world, moved, x, y, tx, y);
}

void updateCell(World& world, std::vector<uint8_t>& moved, int x, int y, int dir, int frame) {
    const Mat self = world.at(x, y);
    const MaterialInfo& info = materialInfo(self);
    if (!info.falls && !info.flows) return;
    if (isMoved(world, moved, x, y)) return;

    // Straight down.
    if (trySwap(world, moved, x, y, x, y + 1)) return;

    if (info.falls && !info.flows) {
        // Loose solids slide down a slope, but only through an open side cell, so
        // grains cannot squeeze through a diagonal crack in a wall.
        if (isOpen(world, x + dir, y) && trySwap(world, moved, x, y, x + dir, y + 1)) return;
        if (isOpen(world, x - dir, y) && trySwap(world, moved, x, y, x - dir, y + 1)) return;
        return;
    }

    // Liquids.
    //   1. Roll down a slope diagonally.
    //   2. Spread sideways. With liquid stacked on top, the pressure pushes as
    //      far as the weight allows, even shoving a lighter grain aside on the
    //      way. With nothing stacked on top there is no pressure, so the cell
    //      only spills over a genuine edge into air beside it - always one row
    //      downward, never back up - so a pool levels off and then goes
    //      completely still instead of shimmering at its rim.
    const int side = (((x * 73856093) ^ (y * 19349663) ^ (frame * 83492791)) & 1) ? 1 : -1;

    if (trySwap(world, moved, x, y, x + side, y + 1)) return;
    if (trySwap(world, moved, x, y, x - side, y + 1)) return;

    int pressure = 0;
    while (pressure < info.flowSlack && materialInfo(world.at(x, y - 1 - pressure)).flows) ++pressure;

    if (pressure > 0) {
        if (flowTowardEmpty(world, moved, x, y, side, pressure)) return;
        if (flowTowardEmpty(world, moved, x, y, -side, pressure)) return;
        if (spillOverEdge(world, moved, x, y, side)) return;
        if (spillOverEdge(world, moved, x, y, -side)) return;
        if (shoveGrain(world, moved, x, y, side)) return;
        shoveGrain(world, moved, x, y, -side);
        return;
    }

    if (spillOverEdge(world, moved, x, y, side)) return;
    spillOverEdge(world, moved, x, y, -side);
}

}  // namespace

Simulation::Simulation(const World& world) {
    moved_.assign(world.cellCount(), 0);
}

void Simulation::step(World& world) {
    const SimRect all{0, 0, world.width() - 1, world.height() - 1};
    step(world, all);
}

void Simulation::step(World& world, const SimRect& region) {
    const int width = world.width();
    const int height = world.height();

    if (moved_.size() != world.cellCount()) moved_.assign(world.cellCount(), 0);
    std::fill(moved_.begin(), moved_.end(), 0);

    // Reactions first (lava crusts, ice melts, sulfur blows), then fire, then
    // movement.
    applyReactions(world, region, frame_);
    updateFire(world, region, frame_);

    // Bottom row first, so a falling cell lands before the cell above it moves.
    const int dir = (frame_ % 2 == 0) ? 1 : -1;
    for (int y = height - 1; y >= 0; --y) {
        if (dir > 0) {
            for (int x = 0; x < width; ++x) {
                if (inRect(region, x, y)) updateCell(world, moved_, x, y, dir, frame_);
            }
        } else {
            for (int x = width - 1; x >= 0; --x) {
                if (inRect(region, x, y)) updateCell(world, moved_, x, y, dir, frame_);
            }
        }
    }

    ++frame_;
}
