#pragma once

#include <vector>

#include "materials.h"

// Mobile materials are a continuous quantity, not a whole cell. A cell holding
// less than this much counts as empty; the rest of the program only ever needs
// "is there material here", so one threshold keeps everything else unchanged.
//
// This threshold is also the SEED size: a cell is never filled with an amount
// smaller than this. A transfer that would leave a recipient holding between 0
// and `kMassEmpty` is refused instead, and the material stays where it was.
//
// That refusal is what removes the last flicker. Without it, a cell at the edge
// of a pool would receive a sliver, become a material cell for exactly one tick,
// pour the sliver back down, and become air again — an on/off blink measured at
// 25 toggles in 60 ticks on one cell. Refusing the sliver means the recipient
// either gets enough material to be a real cell or gets nothing at all, so the
// visible surface can only change when a whole seed of material moves.
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
    // Mobile materials — liquids and loose solids alike — also get a full mass
    // of 1.0, because both now move by transferring a fraction of a unit. That
    // is what lets every existing "world.set(x, y, Mat::Water/Sand)" call keep
    // working unchanged after movement became continuous.
    void set(int x, int y, Mat m) {
        if (!inBounds(x, y)) return;
        const size_t index = static_cast<size_t>(y) * width_ + x;
        cells_[index] = static_cast<uint8_t>(m);
        life_[index] = 0;
        mass_[index] = isMobile(m) ? 1.0f : 0.0f;
    }

    // How much material is in this cell: 0.0 empty, 1.0 full, above 1.0 compressed.
    // Always 0 for a non-liquid cell.
    float mass(int x, int y) const {
        if (!inBounds(x, y)) return 0.0f;
        return mass_[static_cast<size_t>(y) * width_ + x];
    }

    // -- Double buffering for the movement pass ------------------------------
    //
    // Movement is a scatter: a cell writes mass into its neighbours. If those
    // neighbours are still to be visited in the same pass, they read mass that
    // this very tick created, and push it on again. That is a cascade, and it is
    // what made the surface of a pool flicker: measured on one edge cell, mass
    // arrived from the right and was poured straight down inside a single tick,
    // 25 times in 60 ticks, and the cell blinked between water and air.
    //
    // So the pass reads from a SNAPSHOT taken before it starts and writes to the
    // live grid. A cell then always computes its outflow from the world as it was
    // at the beginning of the tick, never from a neighbour this tick has already
    // changed. Nothing can travel more than one cell per tick, and the order the
    // cells are visited in stops mattering at all.
    //
    // Only `mass_` is buffered. `flowMass` never touches the material or life
    // arrays, so the snapshot copy is a single flat float copy per tick.
    void beginMassSnapshot() { massPrev_ = mass_; }

    // Mass as it stood at the start of the movement pass. This is the only read a
    // flow rule may use. `mass()` would see writes made earlier in the same pass.
    float massAt(int x, int y) const {
        if (!inBounds(x, y)) return 0.0f;
        return massPrev_[static_cast<size_t>(y) * width_ + x];
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
    std::vector<float> massPrev_;   // start-of-pass copy, see beginMassSnapshot
};
