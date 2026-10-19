#include "world.h"

World::World(int width, int height)
    : width_(width),
      height_(height),
      cells_(static_cast<size_t>(width) * height, 0),
      life_(static_cast<size_t>(width) * height, 0) {}

void World::fill(Mat m) {
    cells_.assign(cells_.size(), static_cast<uint8_t>(m));
    life_.assign(life_.size(), 0);
}

int World::count(Mat m) const {
    const uint8_t target = static_cast<uint8_t>(m);
    int total = 0;
    for (uint8_t cell : cells_) {
        if (cell == target) ++total;
    }
    return total;
}

int World::burningCount() const {
    int total = 0;
    for (size_t i = 0; i < cells_.size(); ++i) {
        if (isBurning(static_cast<Mat>(cells_[i]), life_[i])) ++total;
    }
    return total;
}
