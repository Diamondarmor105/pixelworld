#pragma once

#include <cstdint>
#include <climits>
#include <unordered_map>
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

// The part of the world the simulation works on, in cells, inclusive at both
// ends. Only the pixels near the player are simulated; the rest of the world
// is held still until it comes into view.
struct SimRect {
    int x0;
    int y0;
    int x1;
    int y1;
};

// One block of the world: 256 cells wide, and as tall as the whole world.
//
// The world is endless sideways, so it is stored as a row of these. A chunk is
// built the first time it is asked for and is never thrown away again: that is
// what makes your destruction stick. Walk a long way away, come back, and the
// hole you blew is still there.
//
// Three arrays, one entry per cell: the material, how long the fire on it has
// left, and how much material it holds.
struct Chunk {
    static constexpr int WIDTH = 256;

    explicit Chunk(int height)
        : cells(static_cast<size_t>(WIDTH) * static_cast<size_t>(height), 0),
          life(static_cast<size_t>(WIDTH) * static_cast<size_t>(height), 0),
          mass(static_cast<size_t>(WIDTH) * static_cast<size_t>(height), 0.0f) {}

    std::vector<uint8_t> cells;
    std::vector<uint8_t> life;
    std::vector<float> mass;

    // Where a cell lives inside the chunk. `lx` is the column inside the
    // chunk, 0 to 255; `y` is the row in the world.
    size_t index(int lx, int y) const {
        return static_cast<size_t>(y) * WIDTH + static_cast<size_t>(lx);
    }
};

// The grid of material cells that makes up the world.
//
// The world has a fixed height and no ends sideways: it goes on for ever in
// both directions. It is kept as a map of chunks, so only the parts of the
// world you have actually reached are held in memory — and the ones you have
// reached are never let go.
class World {
public:
    static constexpr int CHUNK_WIDTH = Chunk::WIDTH;

    // How tall the world is, in cells. The endless part is sideways.
    static constexpr int DEFAULT_HEIGHT = 512;

    explicit World(int height = DEFAULT_HEIGHT);

    int height() const { return height_; }

    // Is this cell inside the world? Horizontally, every column is inside:
    // the world is endless that way. Vertically, there is a sky and a floor.
    bool inBounds(int /*x*/, int y) const { return y >= 0 && y < height_; }

    // Reading or writing outside the grid is safe: reads return Air, writes
    // are ignored. Writing to a column whose chunk does not exist yet grows
    // the world to there, so a scene can be drawn without any set-up.
    Mat at(int x, int y) const;
    void set(int x, int y, Mat m);

    // How much material is in this cell: 0.0 empty, 1.0 full, above 1.0 compressed.
    // Always 0 for a non-liquid cell.
    float mass(int x, int y) const;

    // Sets the material in a cell directly, from the flow rules. `material` is
    // the material being moved in. Life is left alone, so a flowing cell never
    // disturbs burning.
    //
    // Mass is never destroyed here, which makes conservation a property of the
    // storage layer rather than of every caller. A positive amount below the
    // empty threshold is kept, not rounded away: the cell stays a material cell
    // holding a sliver, and the sliver is available to move on. Only a genuinely
    // empty amount clears the cell.
    void setMass(int x, int y, Mat material, float amount);

    // Per-cell counter used to time how long fire and burning material last.
    // 0 means "not burning"; a positive value counts down each tick.
    uint8_t life(int x, int y) const;
    void setLife(int x, int y, uint8_t value);

    // -- Chunks -------------------------------------------------------------

    // Which chunk a world column belongs to. Negative columns count backwards
    // from zero, so the world really does go on for ever to the left.
    int chunkIndexOf(int worldX) const;

    bool hasChunk(int index) const;

    // The chunk with this index, built empty if it is not there yet.
    Chunk& chunk(int index);

    // Throws the whole world away. Used by the debug key that starts again.
    void clearChunks();

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
    // live world. A cell then always computes its outflow from the world as it
    // was at the beginning of the tick, never from a neighbour this tick has
    // already changed. Nothing can travel more than one cell per tick, and the
    // order the cells are visited in stops mattering at all.
    //
    // The snapshot covers the region being simulated (plus a couple of cells
    // of slack, so a cell on its edge can still read its neighbours).
    void beginMassSnapshot(const SimRect& region);

    // Mass as it stood at the start of the movement pass. This is the only read a
    // flow rule may use. `mass()` would see writes made earlier in the same pass.
    float massAt(int x, int y) const;

    // -- Odds and ends ------------------------------------------------------

    // Fills a range of world columns with a material. The world is endless, so
    // the range has to be said out loud.
    void fill(int x0, int x1, Mat m);

    // How many cells the world holds, over every chunk that exists.
    size_t cellCount() const;

    // Counts how many cells currently hold the given material.
    int count(Mat m) const;

    // Total liquid mass of a material across the world. This is the quantity
    // the mass-based flow conserves, so it is what tests must compare before and
    // after: the number of *cells* a liquid occupies legitimately falls as a
    // pool levels, because the same liquid is then spread thinner.
    float totalMass(Mat m) const;

    // Counts cells of a material that hold at least `atLeast` mass - used to ask
    // "where is there visible liquid", ignoring the vanishing sliver left behind
    // by a flow that rounded out.
    int cellsWithMass(Mat m, float atLeast) const;

    // Counts how many cells are currently on fire (flames plus burning fuel).
    int burningCount() const;

    // The area the existing chunks cover. Used by the self-tests, which
    // simulate everything at once.
    SimRect bounds() const;

private:
    // The chunk that holds a world column, or null if there is none there.
    // The last one looked up is remembered: the simulation walks the world row
    // by row, so nearly every call lands in the same chunk as the call before,
    // and this turns the lookup into a pair of comparisons.
    const Chunk* chunkAt(int x) const;
    Chunk* chunkAt(int x);

    int height_;

    // The chunks that exist, by index. Never unloaded.
    std::unordered_map<int, Chunk> chunks_;

    // The cache described above: which chunk, which one it is, and the world
    // column it starts at.
    mutable int cachedIndex_ = INT_MIN;
    mutable const Chunk* cachedChunk_ = nullptr;
    mutable int cachedLeft_ = 0;

    // The start-of-pass copy of the mass, see beginMassSnapshot.
    std::vector<float> snapMass_;
    int snapX0_ = 0;
    int snapY0_ = 0;
    int snapW_ = 0;
    int snapH_ = 0;
};
