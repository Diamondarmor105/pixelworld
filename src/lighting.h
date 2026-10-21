#pragma once

#include "world.h"

#include <vector>

// Computes a per-cell light level (0..1) for the world. Light comes from:
//   - the sun (day/night cycle), only above ground
//   - fire cells
//   - the player (small radius)
//   - lightning flashes
// Underground ambient is dim; surface ambient follows the day/night cycle.
class Lighting {
public:
    Lighting(int width, int height);

    // Recompute light for the whole world. `time` is in seconds.
    void update(const World& world, float playerX, float playerY,
                float time, bool lightning);

    float at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0.15f;
        return light_[static_cast<size_t>(y) * width_ + x];
    }

    const float* data() const { return light_.data(); }

private:
    int width_, height_;
    std::vector<float> light_;
    float lightningTimer_ = 0.0f;
};
