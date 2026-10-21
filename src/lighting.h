#pragma once

#include "world.h"

#include <vector>

class Lighting {
public:
    Lighting(int width, int height);

    void update(const World& world, float playerX, float playerY,
                float time, bool lightning, float explosionX, float explosionY, float explosionTimer);

    float at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0.15f;
        return light_[static_cast<size_t>(y) * width_ + x];
    }

    // RGB tint multipliers (1.0 = no tint). Fire light is warm.
    float tintR(int x, int y) const { return tintR_[idx(x,y)]; }
    float tintG(int x, int y) const { return tintG_[idx(x,y)]; }
    float tintB(int x, int y) const { return tintB_[idx(x,y)]; }

    const float* data() const { return light_.data(); }
    bool isLightning() const { return lightningTimer_ > 0; }
    float explosionFlash() const { return explosionTimer_; }

private:
    int width_, height_;
    std::vector<float> light_;
    std::vector<float> tintR_, tintG_, tintB_;
    float lightningTimer_ = 0.0f;
    float explosionTimer_ = 0.0f;
    float explosionX_ = 0, explosionY_ = 0;

    size_t idx(int x, int y) const { return static_cast<size_t>(y) * width_ + x; }

    static bool lineOfSight(const World& world, int x0, int y0, int x1, int y1);
};
