#include "world.h"

World::World(int width, int height)
    : width_(width), height_(height), cells_(static_cast<size_t>(width) * height, 0) {}

void World::fill(Mat m) {
    cells_.assign(cells_.size(), static_cast<uint8_t>(m));
}

int World::count(Mat m) const {
    const uint8_t target = static_cast<uint8_t>(m);
    int total = 0;
    for (uint8_t cell : cells_) {
        if (cell == target) ++total;
    }
    return total;
}
