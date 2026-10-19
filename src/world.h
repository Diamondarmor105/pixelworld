#pragma once

#include <vector>

#include "materials.h"

// The grid of material cells that makes up the world.
//
// For now the world is exactly the size of the visible area (480 x 270). When
// the endless world is built (slice 6) this grows to a chunked world, but the
// read/write interface below is what the rest of the program uses either way.
class World {
public:
    World(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    bool inBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < width_ && y < height_;
    }

    // Reading or writing outside the grid is safe: reads return Air, writes are ignored.
    Mat at(int x, int y) const {
        if (!inBounds(x, y)) return Mat::Air;
        return static_cast<Mat>(cells_[static_cast<size_t>(y) * width_ + x]);
    }

    void set(int x, int y, Mat m) {
        if (!inBounds(x, y)) return;
        cells_[static_cast<size_t>(y) * width_ + x] = static_cast<uint8_t>(m);
    }

    void fill(Mat m);

    size_t cellCount() const { return cells_.size(); }

    // Counts how many cells currently hold the given material.
    int count(Mat m) const;

private:
    int width_;
    int height_;
    std::vector<uint8_t> cells_;
};
