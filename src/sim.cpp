#include "sim.h"

#include <algorithm>

namespace {

// Can material `from` move into a cell holding `into`?
// Empty space always yes; a lighter loose solid or liquid is pushed aside;
// anything solid blocks.
bool canDisplace(Mat from, Mat into) {
    if (into == Mat::Air) return true;
    const MaterialInfo& target = materialInfo(into);
    if (!target.falls && !target.flows) return false;
    return materialInfo(from).density > target.density;
}

bool isOpen(const World& world, int x, int y) {
    return world.at(x, y) == Mat::Air;
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

    markMoved(world, moved, x, y);
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
void updateFire(World& world, int frame) {
    const int width = world.width();
    const int height = world.height();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
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
    //   2. Spread sideways, but only as far as the weight of liquid stacked on
    //      top of this cell allows. A surface cell has nothing pressing on it
    //      and stays put, so a pool goes still once it is level; a deep cell
    //      reaches further and pushes the water out to find its level.
    const int side = (((x * 73856093) ^ (y * 19349663) ^ (frame * 83492791)) & 1) ? 1 : -1;

    if (trySwap(world, moved, x, y, x + side, y + 1)) return;
    if (trySwap(world, moved, x, y, x - side, y + 1)) return;

    int pressure = 0;
    while (pressure < info.flowSlack && materialInfo(world.at(x, y - 1 - pressure)).flows) ++pressure;

    if (pressure > 0) {
        if (flowTowardEmpty(world, moved, x, y, side, pressure)) return;
        if (flowTowardEmpty(world, moved, x, y, -side, pressure)) return;
    }
}

}  // namespace

Simulation::Simulation(const World& world) {
    moved_.assign(world.cellCount(), 0);
}

void Simulation::step(World& world) {
    const int width = world.width();
    const int height = world.height();

    if (moved_.size() != world.cellCount()) moved_.assign(world.cellCount(), 0);
    std::fill(moved_.begin(), moved_.end(), 0);

    // Reactions first (fire spreads and burns out), then movement.
    updateFire(world, frame_);

    // Bottom row first, so a falling cell lands before the cell above it moves.
    const int dir = (frame_ % 2 == 0) ? 1 : -1;
    for (int y = height - 1; y >= 0; --y) {
        if (dir > 0) {
            for (int x = 0; x < width; ++x) updateCell(world, moved_, x, y, dir, frame_);
        } else {
            for (int x = width - 1; x >= 0; --x) updateCell(world, moved_, x, y, dir, frame_);
        }
    }

    ++frame_;
}
