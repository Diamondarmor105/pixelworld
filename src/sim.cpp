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

// A liquid that cannot fall or roll shoves a lighter grain aside instead of
// being stopped by it. This is how melt-water works its way out of a snow bank,
// and how a wave forces sand and gravel out of the way: the pile looks solid,
// but the heavier thing pushes through.
bool shoveGrain(World& world, std::vector<uint8_t>& moved, int x, int y, int dir) {
    const int tx = x + dir;
    if (!world.inBounds(tx, y)) return false;
    const Mat target = world.at(tx, y);
    if (target == Mat::Air) return false;                          // handled by the sideways move
    if (materialInfo(target).flows) return false;                  // liquids sort out by density
    if (!canDisplace(world.at(x, y), target)) return false;
    return trySwap(world, moved, x, y, tx, y);
}

// Move one mobile cell according to its material's rules. Loose solids and
// liquids share the same steps:
//
//   Falling solids:  down, then down-diagonal.
//   Liquids:         down, then down-diagonal, then sideways.
//
// `dir` picks which side is tried first; `spreadOnly` limits the call to the
// sideways step, so the second sweep of a tick cannot undo the falling the
// first sweep just did. Returns true if the cell moved.
bool updateCell(World& world, std::vector<uint8_t>& moved, int x, int y, int dir, bool spreadOnly) {
    const Mat self = world.at(x, y);
    const MaterialInfo& info = materialInfo(self);
    if (!info.falls && !info.flows) return false;
    if (isMoved(world, moved, x, y)) return false;

    const int first = dir;
    const int second = -dir;

    if (!spreadOnly) {
        // 1. Straight down, into anything this cell can displace.
        if (trySwap(world, moved, x, y, x, y + 1)) return true;

        // 2. Down-diagonal, so a heap slides into a slope instead of standing in
        //    a tower. A loose solid only takes this step through an open side
        //    cell, so grains cannot squeeze through a diagonal crack in a wall; a
        //    liquid may also displace a lighter neighbour on the way past.
        if (info.flows) {
            if (trySwap(world, moved, x, y, x + first, y + 1)) return true;
            if (trySwap(world, moved, x, y, x + second, y + 1)) return true;
        } else {
            if (isOpen(world, x + first, y) && trySwap(world, moved, x, y, x + first, y + 1)) return true;
            if (isOpen(world, x + second, y) && trySwap(world, moved, x, y, x + second, y + 1)) return true;
        }
    }

    // A loose solid has no sideways step at all: it falls and rolls, and that is
    // it. Only a liquid spreads along its row to find a level. This guard has to
    // live outside the `spreadOnly` block above, because the second sweep skips
    // that block - without this, a falling solid would reach the sideways step in
    // the second sweep and shuffle one cell left or right on every tick, which is
    // the surface grain that jitters in place.
    if (!info.flows) return false;

    // A liquid surface cell may walk sideways into liquid when the column it
    // steps toward has a lower surface.
    //
    // This is the one move the plain rules lack, and it is what makes a slope
    // cascade. On a slope a surface cell's own-row neighbours are liquid, so
    // the sideways-into-air step below is unavailable, and without this the
    // slope stands forever: measured on a stepped basin, exactly 3 cells of 200
    // ever left the high terrace, all by tick 1000, and then nothing moved for
    // the next 99,000 ticks while the far terraces stayed bone dry.
    //
    // Two guards keep it from becoming a churn engine, both found by measuring
    // a settled pool rather than guessing:
    //
    //   * Only the cell at the TOP of its own column takes the step. Applying
    //     it to every liquid cell had buried cells walking sideways out of a
    //     pool and tearing holes in it.
    //   * Only a drop of at least two rows counts. A settled pool's surface
    //     ruffles by a single row (measured: y=188..189 across the whole
    //     width), and treating that as "lower" made cells hop between
    //     neighbouring columns forever - surface movement went from 8 columns
    //     of 480 to 320.
    //
    // With both guards, a slope levels and a settled pool does not move at all.
    if (world.at(x, y + 1) != Mat::Air && materialInfo(world.at(x, y + 1)).flows) {
        // Topmost non-air cell of a column, and only if it is liquid.
        auto surfaceRow = [&](int cx) -> int {
            if (cx < 0 || cx >= world.width()) return 1 << 30;
            for (int cy = 0; cy < world.height(); ++cy) {
                const Mat m = world.at(cx, cy);
                if (m == Mat::Air) continue;
                return materialInfo(m).flows ? cy : (1 << 30);
            }
            return 1 << 30;
        };

        const int mySurface = surfaceRow(x);
        if (y == mySurface) {
            constexpr int kReach = 8;
            constexpr int kMinDrop = 2;
            int bestGap = 0, bestDir = 0;
            for (int d = 1; d <= kReach; ++d) {
                const int lt = surfaceRow(x - d);
                const int rt = surfaceRow(x + d);
                if (rt < (1 << 30) && (rt - mySurface) > bestGap) { bestGap = rt - mySurface; bestDir = +1; }
                if (lt < (1 << 30) && (lt - mySurface) > bestGap) { bestGap = lt - mySurface; bestDir = -1; }
            }
            if (bestDir != 0 && bestGap >= kMinDrop) {
                if (trySwap(world, moved, x, y, x + bestDir, y)) return true;
            }
        }
    }

    // A cell on the surface of a settled pool has nothing left to do.
    //
    // It cannot fall and it cannot roll, so the only move the rules leave it is
    // the sideways shuffle - and on a flat surface that shuffle achieves
    // nothing. Neighbouring surface cells simply trade places, every tick,
    // forever. Measured on a wide shallow pool, this was the whole remaining
    // source of a moving surface: of the cells that still moved, 25 of 34 could
    // not fall or roll at all, and 40 of 68 moves crossed into a cell whose
    // direction preference pointed the other way.
    //
    // The test is deliberately narrow, so that real spreading is untouched. The
    // cell must be sitting ON liquid - part of a pool's surface, not on the
    // floor - and it must have no downward escape on either side. A cell on the
    // floor is left alone even when boxed in, because it may still be creeping
    // outward to reach lower ground further along, and stopping it there was
    // measured to hold a pool back from spreading (41 columns of spread fell to
    // 24, and the shallow-heap check failed).
    //
    // The cheapest test is first: most cells can fall straight down, and are
    // gone on one comparison.
    if (world.at(x, y + 1) != Mat::Air && materialInfo(world.at(x, y + 1)).flows) {
        const bool leftOpen = world.at(x - 1, y) == Mat::Air && world.at(x - 1, y + 1) == Mat::Air;
        if (!leftOpen) {
            const bool rightOpen = world.at(x + 1, y) == Mat::Air && world.at(x + 1, y + 1) == Mat::Air;
            if (!rightOpen) return false;
        }
    }

    // 3. Sideways at the same row, so a pool spreads out instead of piling up.
    //
    //    A plain step into empty space, or shoving a lighter grain aside. The
    //    two sweeps in `Simulation::step` then fill the holes this leaves
    //    behind, which is what levels a pool out to a flat surface.
    if (trySwap(world, moved, x, y, x + first, y)) return true;
    if (trySwap(world, moved, x, y, x + second, y)) return true;
    if (shoveGrain(world, moved, x, y, first)) return true;
    if (shoveGrain(world, moved, x, y, second)) return true;
    return false;
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

    // Movement runs as two sweeps over the same tick. The first, bottom-up, is
    // the one that does the falling: a cell lands before the cell above it
    // moves. The second, top-down and in the opposite column order, only asks
    // each cell to spread sideways - it is what fills the holes the first sweep
    // leaves behind, so a raised pile spreads out instead of freezing as a
    // staircase. Both sweeps share the same `moved` marks, so no cell moves
    // twice in one tick.
    //
    // The second sweep only revisits the rows the first sweep actually moved
    // something in. A still pool or an empty sky has nothing to spread, and
    // walking every cell again would double the tick cost for no effect; on a
    // settled scene the second sweep is now nearly free.
    //
    // Which side each cell tries first is a separate matter.
    //
    // This used to be one value for the whole grid, flipped every tick. That
    // kept the left/right bias balanced overall, but it did so in lockstep, and
    // the flip had a second effect that was worse: because the sweep walks in
    // the same direction it prefers, reversing it every tick made a pool edge
    // settle into a *different* shape on alternate ticks. Measured on the edge
    // of a wide shallow pool, the falling sweep alone changed
    //
    //     .....WWWWWW      on one tick, to
    //     ....W..WWWW      on the next, and back again, forever.
    //
    // Both shapes are genuinely settled for the scan order that produced them;
    // they simply disagree. On screen the edge of the pool therefore pulses
    // between two outlines, which the eye blends into a flickering,
    // semi-transparent surface.
    //
    // So the scan order is now fixed, and the left/right bias is balanced the
    // other way: each cell has a standing preference of its own, taken from its
    // own coordinates. Neighbouring cells lean different ways, the preference
    // does not change from tick to tick, and the grid as a whole stays balanced
    // because the hash is even-handed. A cell that leans right still tries left
    // when its first choice is blocked, so nothing is unreachable.
    //
    // A preference that *did* change every tick is not a fix: the cell then
    // flip-flops with a different phase to its neighbour, which measured worse -
    // ping-ponging fell from 90 cells to 60, but cells that wandered off and
    // came back tripled, to 276. Stable is the whole point.
    if (dirtyRows_.size() != static_cast<size_t>(height)) dirtyRows_.assign(height, 0);
    std::fill(dirtyRows_.begin(), dirtyRows_.end(), 0);

    // A cell's standing preference: stable per cell, even-handed across the
    // grid, and independent of the tick so it does not reverse in sympathy with
    // its neighbours.
    auto lean = [&](int x, int y) {
        return (noise(x, y, 0) & 1u) == 0u ? 1 : -1;
    };

    // Bottom-up, and always left-to-right, so a cell lands before the cell above
    // it is asked and the result does not depend on which tick it is.
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            if (!inRect(region, x, y)) continue;
            if (updateCell(world, moved_, x, y, lean(x, y), false)) dirtyRows_[y] = 1;
        }
    }
    // Top-down and right-to-left, the opposite way, so the sideways pass fills
    // the holes the falling pass just made instead of chasing them.
    for (int y = 0; y < height; ++y) {
        if (!dirtyRows_[y]) continue;
        for (int x = width - 1; x >= 0; --x) {
            if (!inRect(region, x, y)) continue;
            updateCell(world, moved_, x, y, -lean(x, y), true);
        }
    }

    ++frame_;
}
