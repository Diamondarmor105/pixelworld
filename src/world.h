#pragma once

#include <vector>

#include "materials.h"

// Liquids are a continuous quantity, not a whole cell. A cell holding less than
// this much liquid counts as empty; the rest of the program only ever needs
// "is there liquid here", so one threshold keeps everything else unchanged.
constexpr float kMassEmpty = 0.02f;

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

    // Placing a material clears the cell's life counter; the simulation sets
    // life again for cells that are on fire.
    //
    // Liquid materials also get a full mass of 1.0. That is what lets every
    // existing "world.set(x, y, Mat::Water)" call keep working unchanged after
    // liquids became a continuous quantity rather than a whole cell.
    void set(int x, int y, Mat m) {
        if (!inBounds(x, y)) return;
        const size_t index = static_cast<size_t>(y) * width_ + x;
        cells_[index] = static_cast<uint8_t>(m);
        life_[index] = 0;
        mass_[index] = materialInfo(m).flows ? 1.0f : 0.0f;
    }

    // How much liquid is in this cell: 0.0 empty, 1.0 full, above 1.0 compressed.
    // Always 0 for a non-liquid cell.
    float mass(int x, int y) const {
        if (!inBounds(x, y)) return 0.0f;
        return mass_[static_cast<size_t>(y) * width_ + x];
    }

    // Sets the liquid in a cell directly, from the flow rules. `material` is the
    // liquid being moved in. Life is left alone, so a flowing cell never disturbs
    // burning.
    //
    // Mass is never destroyed here, which makes conservation a property of the
    // storage layer rather than of every caller. A positive amount below the
    // empty threshold is kept, not rounded away: the cell stays a liquid cell
    // holding a sliver, and the sliver is available to move on. Only a genuinely
    // empty amount clears the cell.
    void setMass(int x, int y, Mat material, float amount) {
        if (!inBounds(x, y)) return;
        const size_t index = static_cast<size_t>(y) * width_ + x;
        if (amount <= 0.0f) {
            cells_[index] = static_cast<uint8_t>(Mat::Air);
            mass_[index] = 0.0f;
        } else {
            cells_[index] = static_cast<uint8_t>(material);
            mass_[index] = amount;
        }
    }

    // Per-cell counter used to time how long fire and burning material last.
    // 0 means "not burning"; a positive value counts down each tick.
    uint8_t life(int x, int y) const {
        if (!inBounds(x, y)) return 0;
        return life_[static_cast<size_t>(y) * width_ + x];
    }

    void setLife(int x, int y, uint8_t value) {
        if (!inBounds(x, y)) return;
        life_[static_cast<size_t>(y) * width_ + x] = value;
    }

    void fill(Mat m);

    size_t cellCount() const { return cells_.size(); }

    // Counts how many cells currently hold the given material.
    int count(Mat m) const;

    // Total liquid mass of a material across the grid. This is the quantity the
    // mass-based flow conserves, so it is what tests must compare before and
    // after: the number of *cells* a liquid occupies legitimately falls as a
    // pool levels, because the same liquid is then spread thinner.
    float totalMass(Mat m) const;

    // Counts cells of a material that hold at least `atLeast` mass - used to ask
    // "where is there visible liquid", ignoring the vanishing sliver left behind
    // by a flow that rounded out.
    int cellsWithMass(Mat m, float atLeast) const;

    // Counts how many cells are currently on fire (flames plus burning fuel).
    int burningCount() const;

private:
    int width_;
    int height_;
    std::vector<uint8_t> cells_;
    std::vector<uint8_t> life_;
    std::vector<float> mass_;
};
