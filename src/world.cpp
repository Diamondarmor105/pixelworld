#include "world.h"

World::World(int width, int height)
    : width_(width),
      height_(height),
      cells_(static_cast<size_t>(width) * height, 0),
      life_(static_cast<size_t>(width) * height, 0),
      mass_(static_cast<size_t>(width) * height, 0.0f) {}

void World::fill(Mat m) {
    cells_.assign(cells_.size(), static_cast<uint8_t>(m));
    life_.assign(life_.size(), 0);
    mass_.assign(mass_.size(), materialInfo(m).flows ? 1.0f : 0.0f);
}

int World::count(Mat m) const {
    const uint8_t target = static_cast<uint8_t>(m);
    int total = 0;
    for (uint8_t cell : cells_) {
        if (cell == target) ++total;
    }
    return total;
}

float World::totalMass(Mat m) const {
    const uint8_t target = static_cast<uint8_t>(m);
    float total = 0.0f;
    for (size_t i = 0; i < cells_.size(); ++i) {
        if (cells_[i] == target) total += mass_[i];
    }
    return total;
}

int World::cellsWithMass(Mat m, float atLeast) const {
    const uint8_t target = static_cast<uint8_t>(m);
    int total = 0;
    for (size_t i = 0; i < cells_.size(); ++i) {
        if (cells_[i] == target && mass_[i] >= atLeast) ++total;
    }
    return total;
}

int World::burningCount() const {    int total = 0;
    for (size_t i = 0; i < cells_.size(); ++i) {
        if (isBurning(static_cast<Mat>(cells_[i]), life_[i])) ++total;
    }
    return total;
}
