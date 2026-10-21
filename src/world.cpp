#include "world.h"

#include <algorithm>
#include <climits>

World::World(int height) : height_(height) {}

int World::chunkIndexOf(int worldX) const {
    // Floor division by 256, which is a power of two, so this is a shift
    // rather than a division. The shift rounds towards negative infinity,
    // which is exactly what is wanted: column -1 belongs to the chunk to the
    // left of zero, not to chunk 0.
    //
    // (C++20 requires a right shift of a negative number to round this way.
    // C++17 leaves it to the implementation, and every compiler this project
    // is built with does the same thing.)
    return worldX >> 8;
}

const Chunk* World::chunkAt(int x) const {
    const int index = chunkIndexOf(x);
    if (index == cachedIndex_) return cachedChunk_;
    const auto it = chunks_.find(index);
    const Chunk* found = (it == chunks_.end()) ? nullptr : &it->second;
    cachedIndex_ = index;
    cachedChunk_ = found;
    cachedLeft_ = index * Chunk::WIDTH;
    return found;
}

Chunk* World::chunkAt(int x) {
    return const_cast<Chunk*>(static_cast<const World*>(this)->chunkAt(x));
}

bool World::hasChunk(int index) const {
    return chunks_.find(index) != chunks_.end();
}

Chunk& World::chunk(int index) {
    auto it = chunks_.find(index);
    if (it == chunks_.end()) {
        it = chunks_.try_emplace(index, height_).first;
    }
    // Point the cache at it, so the next lookup is free.
    cachedIndex_ = index;
    cachedLeft_ = index * Chunk::WIDTH;
    cachedChunk_ = &it->second;
    return it->second;
}

void World::clearChunks() {
    chunks_.clear();
    cachedIndex_ = INT_MIN;
    cachedChunk_ = nullptr;
    cachedLeft_ = 0;
}

Mat World::at(int x, int y) const {
    if (y < 0 || y >= height_) return Mat::Air;
    const Chunk* c = chunkAt(x);
    if (!c) return Mat::Air;
    return static_cast<Mat>(c->cells[c->index(x - cachedLeft_, y)]);
}

void World::set(int x, int y, Mat m) {
    if (y < 0 || y >= height_) return;
    Chunk* c = chunkAt(x);
    if (!c) {
        // The world grows to wherever you write. A scene can be drawn without
        // any set-up, and a tool that reaches past the edge of what has been
        // generated does not fall off the map.
        c = &chunk(chunkIndexOf(x));
    }
    const size_t cell = c->index(x - cachedLeft_, y);
    c->cells[cell] = static_cast<uint8_t>(m);
    c->life[cell] = 0;
    c->mass[cell] = isMobile(m) ? 1.0f : 0.0f;
}

float World::mass(int x, int y) const {
    if (y < 0 || y >= height_) return 0.0f;
    const Chunk* c = chunkAt(x);
    if (!c) return 0.0f;
    return c->mass[c->index(x - cachedLeft_, y)];
}

void World::setMass(int x, int y, Mat material, float amount) {
    if (y < 0 || y >= height_) return;
    Chunk* c = chunkAt(x);
    if (!c) {
        // The world grows to wherever you write, so a liquid that reaches the
        // edge of what has been generated keeps its mass instead of vanishing.
        c = &chunk(chunkIndexOf(x));
    }
    const size_t cell = c->index(x - cachedLeft_, y);
    if (amount <= 0.0f) {
        c->cells[cell] = static_cast<uint8_t>(Mat::Air);
        c->mass[cell] = 0.0f;
    } else {
        c->cells[cell] = static_cast<uint8_t>(material);
        c->mass[cell] = amount;
    }
}

uint8_t World::life(int x, int y) const {
    if (y < 0 || y >= height_) return 0;
    const Chunk* c = chunkAt(x);
    if (!c) return 0;
    return c->life[c->index(x - cachedLeft_, y)];
}

void World::setLife(int x, int y, uint8_t value) {
    if (y < 0 || y >= height_) return;
    Chunk* c = chunkAt(x);
    if (!c) return;
    c->life[c->index(x - cachedLeft_, y)] = value;
}

void World::beginMassSnapshot(const SimRect& region) {
    // Two cells of slack on every side, so a cell on the edge of the region can
    // still read the mass of the neighbours it is about to compare itself with.
    const int x0 = region.x0 - 2;
    const int y0 = region.y0 - 2;
    const int x1 = region.x1 + 2;
    const int y1 = region.y1 + 2;

    snapX0_ = x0;
    snapY0_ = y0;
    snapW_ = x1 - x0 + 1;
    snapH_ = y1 - y0 + 1;
    if (snapW_ <= 0 || snapH_ <= 0) {
        snapMass_.clear();
        return;
    }
    snapMass_.assign(static_cast<size_t>(snapW_) * static_cast<size_t>(snapH_), 0.0f);

    // Copy the mass of the region in, one chunk row at a time, so the copy is a
    // handful of straight memory moves rather than a hundred thousand lookups.
    for (int y = y0; y <= y1; ++y) {
        if (y < 0 || y >= height_) continue;
        float* row = &snapMass_[static_cast<size_t>(y - y0) * snapW_];
        int x = x0;
        while (x <= x1) {
            const int index = chunkIndexOf(x);
            const Chunk* c = chunkAt(x);
            const int localX = x - index * Chunk::WIDTH;
            const int segmentEnd = std::min(x1, (index + 1) * Chunk::WIDTH - 1);
            const int count = segmentEnd - x + 1;
            if (c) {
                const float* source = &c->mass[static_cast<size_t>(y) * Chunk::WIDTH + localX];
                std::copy(source, source + count, row + (x - x0));
            }
            x = segmentEnd + 1;
        }
    }
}

float World::massAt(int x, int y) const {
    if (x < snapX0_ || x >= snapX0_ + snapW_ || y < snapY0_ || y >= snapY0_ + snapH_) {
        return 0.0f;
    }
    return snapMass_[static_cast<size_t>(y - snapY0_) * snapW_ + (x - snapX0_)];
}

void World::fill(int x0, int x1, Mat m) {
    if (x1 < x0) return;
    const uint8_t value = static_cast<uint8_t>(m);
    const float massValue = isMobile(m) ? 1.0f : 0.0f;

    const int first = chunkIndexOf(x0);
    const int last = chunkIndexOf(x1);
    for (int index = first; index <= last; ++index) {
        Chunk& c = chunk(index);
        const int from = (index == first) ? (x0 - index * Chunk::WIDTH) : 0;
        const int to = (index == last) ? (x1 - index * Chunk::WIDTH) : (Chunk::WIDTH - 1);
        for (int y = 0; y < height_; ++y) {
            for (int lx = from; lx <= to; ++lx) {
                const size_t cell = c.index(lx, y);
                c.cells[cell] = value;
                c.life[cell] = 0;
                c.mass[cell] = massValue;
            }
        }
    }
}

size_t World::cellCount() const {
    return chunks_.size() * static_cast<size_t>(Chunk::WIDTH) * static_cast<size_t>(height_);
}

int World::count(Mat m) const {
    const uint8_t target = static_cast<uint8_t>(m);
    int total = 0;
    for (const auto& entry : chunks_) {
        for (uint8_t cell : entry.second.cells) {
            if (cell == target) ++total;
        }
    }
    return total;
}

float World::totalMass(Mat m) const {
    const uint8_t target = static_cast<uint8_t>(m);
    float total = 0.0f;
    for (const auto& entry : chunks_) {
        const Chunk& c = entry.second;
        for (size_t i = 0; i < c.cells.size(); ++i) {
            if (c.cells[i] == target) total += c.mass[i];
        }
    }
    return total;
}

int World::cellsWithMass(Mat m, float atLeast) const {
    const uint8_t target = static_cast<uint8_t>(m);
    int total = 0;
    for (const auto& entry : chunks_) {
        const Chunk& c = entry.second;
        for (size_t i = 0; i < c.cells.size(); ++i) {
            if (c.cells[i] == target && c.mass[i] >= atLeast) ++total;
        }
    }
    return total;
}

int World::burningCount() const {
    int total = 0;
    for (const auto& entry : chunks_) {
        const Chunk& c = entry.second;
        for (size_t i = 0; i < c.cells.size(); ++i) {
            if (isBurning(static_cast<Mat>(c.cells[i]), c.life[i])) ++total;
        }
    }
    return total;
}

SimRect World::bounds() const {
    if (chunks_.empty()) return SimRect{0, 0, -1, -1};
    int lowest = INT_MAX;
    int highest = INT_MIN;
    for (const auto& entry : chunks_) {
        lowest = std::min(lowest, entry.first);
        highest = std::max(highest, entry.first);
    }
    return SimRect{lowest * Chunk::WIDTH, 0,
                   (highest + 1) * Chunk::WIDTH - 1, height_ - 1};
}
