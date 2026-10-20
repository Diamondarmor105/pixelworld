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
// it. This is how melt-water works its way out of a snow bank, and how a wave
// of water or a walking player forces sand and gravel out of the way: the pile
// looks solid, but the heavier thing pushes through.
bool shoveGrain(World& world, std::vector<uint8_t>& moved, int x, int y, int dir) {
    const int tx = x + dir;
    if (!world.inBounds(tx, y)) return false;
    const Mat target = world.at(tx, y);
    if (target == Mat::Air) return false;                          // handled by flowTowardEmpty
    if (materialInfo(target).flows) return false;                  // liquids sort out by density
    if (!canDisplace(world.at(x, y), target)) return false;
    return trySwap(world, moved, x, y, tx, y);
}

// How much of `liquid` sits in column x below row y. This is the cell's own
// "head of water": how deep the pool is under it. Unlike the top row of the
// column, this does not change when the cell itself hops away, which is what
// makes it safe to compare. `limit` bounds the scan: the caller only ever needs
// to know whether a neighbour is deeper than this cell, so it never has to count
// the whole column to the bottom of the world.
int liquidBelow(const World& world, Mat liquid, int x, int y, int limit) {
    int depth = 0;
    const int stop = std::min(world.height(), y + 1 + limit);
    for (int i = y + 1; i < stop; ++i) {
        if (world.at(x, i) == liquid) ++depth;
    }
    return depth;
}

// A surface cell - nothing stacked on top, so no pressure. It looks sideways
// along its own row for a column whose liquid sits deeper than its own, and
// steps one cell toward the nearest one it finds. That is what lets a poured
// pool spread out to a flat surface rather than settling as a 45-degree wedge
// (the diagonal roll below only ever moves downhill, so on its own it stops as
// a staircase).
//
// Two details matter. First, an empty neighbour column counts as deeper (it
// reports no liquid at all), so a pool spreads out across dry ground too.
// Second, `reach` lets the cell look several columns ahead, exactly like the
// pressured path does: with a reach of one the pool can only even out by one
// column per tick, which takes thousands of ticks to level a tank and then
// stalls a few rows short.
//
// The comparison is depth below the cell, not the neighbour column's top row.
// Comparing tops looks right but is not: a stray cell riding one row above a
// flat pool sees the column it just left as "lower" (its top is now the row
// below), so it hops right, then the column it left looks lower again, and it
// hops back - a single pixel blinking every tick. Depth below the row does not
// move when the cell leaves, so a pool that is already level is a genuine tie
// and the cell stays put.
bool levelHop(World& world, std::vector<uint8_t>& moved, int x, int y, int dir, int reach) {
    const Mat self = world.at(x, y);
    // This cell's own depth is counted in full (once), because the neighbour
    // scan below is priced against it: a neighbour can only be deeper than us if
    // it has more than this many cells, so we never have to look past that.
    const int myDepth = liquidBelow(world, self, x, y, world.height());

    for (int distance = 1; distance <= reach; ++distance) {
        const int tx = x + dir * distance;
        if (!world.inBounds(tx, y)) return false;
        if (world.at(tx, y) != Mat::Air) return false;  // blocked: cannot pass through other cells
        if (isMoved(world, moved, tx, y)) return false;
        // `myDepth + 1` rows is enough to prove the neighbour is deeper (it
        // would have to have at least myDepth + 1 cells to beat us) or not.
        const int theirDepth = liquidBelow(world, self, tx, y, myDepth + 1);
        if (theirDepth > myDepth) {
            // A deeper column is in reach: step one cell toward it.
            return trySwap(world, moved, x, y, x + dir, y);
        }
    }
    return false;
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
    //   2. Pressured liquid reaches out sideways for the nearest empty cell,
    //      shoving lighter material out of the way - a deep pond levelling out,
    //      or a wave bursting through a pile of sand.
    //   3. A surface cell steps sideways toward the nearest strictly-lower
    //      column within reach, so a poured pool spreads to a flat surface
    //      instead of a diagonal wedge.
    const int side = (((x * 73856093) ^ (y * 19349663) ^ (frame * 83492791)) & 1) ? 1 : -1;
    const int reach = std::max(1, static_cast<int>(info.flowSlack));

    if (trySwap(world, moved, x, y, x + side, y + 1)) return;
    if (trySwap(world, moved, x, y, x - side, y + 1)) return;

    // How much liquid is stacked directly on top of this cell? That is its
    // pressure: none means it is a surface cell, some means it is buried and
    // moving because the pool above is pushing down on it.
    int pressure = 0;
    while (pressure < info.flowSlack && materialInfo(world.at(x, y - 1 - pressure)).flows) ++pressure;

    if (pressure > 0) {
        if (flowTowardEmpty(world, moved, x, y, side, pressure)) return;
        if (flowTowardEmpty(world, moved, x, y, -side, pressure)) return;
        if (shoveGrain(world, moved, x, y, side)) return;
        if (shoveGrain(world, moved, x, y, -side)) return;
    } else if (materialInfo(world.at(x, y - 1)).falls) {
        // A liquid buried under a lighter grain - melt-water inside a snow bank,
        // say - is squeezed but has no liquid above it, so `pressure` is zero
        // and it would otherwise sit there as a pile forever. Treat the grain
        // above as weight and push the grain beside it aside, which is how the
        // water gets out and then behaves like water.
        if (shoveGrain(world, moved, x, y, side)) return;
        if (shoveGrain(world, moved, x, y, -side)) return;
    }

    // Levelling is a SURFACE job. A buried cell sits under liquid and should only
    // ever move because of pressure (above), never because of the surface
    // comparison - and that comparison is the expensive one, because it measures
    // how deep the pool is beneath the cell. Running it for every buried cell of
    // a large settled lake handed back the same answer ("nothing to do") millions
    // of times a second, and cost more than a whole frame on its own. A pool's
    // interior is levelled by the periodic pass instead, which does the whole
    // body in one step.
    if (pressure > 0) return;

    if (levelHop(world, moved, x, y, side, reach)) return;
    levelHop(world, moved, x, y, -side, reach);
}

// Every so often, a body of liquid that has gone quiet is tidied up to a
// genuinely level surface. The local rules above are enough to move water
// around and look right, but on their own they level a pool only approximately
// and very slowly: a tall column collapsing into a wide tank takes thousands of
// ticks to even out and then stalls a few rows short. This pass measures each
// connected body's columns and volume, works out the one surface row that holds
// that volume, and rewrites the body to it - exactly conserving the cell count,
// in a single step. It is a no-op on a body that is already level, so it costs
// nothing visually and never makes a settled pool shimmer. It runs every
// LEVEL_INTERVAL ticks rather than every tick so the local rules (splashing,
// rolling, water bursting through sand) keep doing the visible work.
constexpr int LEVEL_INTERVAL = 20;

// Level every body of `liquid` that overlaps the region. The flood-fill is
// clipped to the region, so a body stretching off-screen is only tidied where
// it is visible and the frozen part is left exactly as it is.
void levelLiquid(World& world, Mat liquid, const SimRect& region) {
    const int width = world.width();
    const int height = world.height();
    std::vector<uint8_t> seen(world.cellCount(), 0);

    for (int sy = region.y0; sy <= region.y1; ++sy) {
        for (int sx = region.x0; sx <= region.x1; ++sx) {
            if (world.at(sx, sy) != liquid) continue;
            const size_t start = static_cast<size_t>(sy) * width + sx;
            if (seen[start]) continue;

            // 1. Collect one body, 8-connected, so a diagonal staircase counts
            // as one body rather than a string of separate single cells.
            std::vector<int> stack{static_cast<int>(start)};
            std::vector<int> body;
            seen[start] = 1;
            int minX = sx, maxX = sx;
            while (!stack.empty()) {
                const int i = stack.back();
                stack.pop_back();
                body.push_back(i);
                const int x = i % width;
                const int y = i / width;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int px = x + dx;
                        const int py = y + dy;
                        if (px < 0 || py < 0 || px >= width || py >= height) continue;
                        if (world.at(px, py) != liquid) continue;
                        const size_t k = static_cast<size_t>(py) * width + px;
                        if (seen[k]) continue;
                        seen[k] = 1;
                        stack.push_back(static_cast<int>(k));
                    }
                }
            }
            const int volume = static_cast<int>(body.size());
            const int cols = maxX - minX + 1;

            // 2. The row each column's liquid sits on: one below the body's
            // lowest cell in that column.
            std::vector<int> support(cols, -1);
            for (int i : body) {
                const int x = i % width;
                const int y = i / width;
                support[x - minX] = std::max(support[x - minX], y + 1);
            }

            // 3. Find the surface row that holds exactly this volume. Capacity
            // is how many cells would fit above each column's floor if the
            // surface were at row s, and it only shrinks as s rises, so the
            // first s that fits is the level surface.
            auto capacity = [&](int s) {
                int cap = 0;
                for (int c = 0; c < cols; ++c) {
                    if (support[c] >= 0) cap += std::max(0, support[c] - s);
                }
                return cap;
            };
            int surface = 0;
            for (int s = 0; s <= height; ++s) {
                if (capacity(s) <= volume) {
                    surface = s;
                    break;
                }
            }

            // 4. Rewrite the body: clear it, then pour it back row by row from
            // the surface downward so the volume spreads evenly instead of
            // stacking in one column.
            for (int i : body) world.set(i % width, i / width, Mat::Air);
            int remaining = volume;
            for (int y = surface; y < height && remaining > 0; ++y) {
                for (int c = 0; c < cols && remaining > 0; ++c) {
                    const int px = minX + c;
                    if (support[c] < 0 || y >= support[c]) continue;
                    if (world.at(px, y) != Mat::Air) continue;
                    world.set(px, y, liquid);
                    --remaining;
                }
            }
            // Whatever could not go under the surface (a column walled off
            // above its floor) stacks up on top instead, so volume is never
            // lost.
            for (int y = surface - 1; y >= 0 && remaining > 0; --y) {
                for (int c = 0; c < cols && remaining > 0; ++c) {
                    const int px = minX + c;
                    if (world.at(px, y) != Mat::Air) continue;
                    world.set(px, y, liquid);
                    --remaining;
                }
            }
        }
    }
}

// Lava is thick and crusts over rather than flowing, so the leveling pass skips
// it; water, oil and acid are the ones that should find their level.
bool levelsOut(Mat m) {
    if (!materialInfo(m).flows) return false;
    return m != Mat::Lava;
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

    // Every so often, tidy any liquid body that has gone quiet to a genuinely
    // level surface. Runs before movement so the settled result is what the
    // movement rules then act on.
    if (frame_ % LEVEL_INTERVAL == 0) {
        for (int m = 0; m < static_cast<int>(Mat::COUNT); ++m) {
            if (levelsOut(static_cast<Mat>(m))) levelLiquid(world, static_cast<Mat>(m), region);
        }
    }

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
