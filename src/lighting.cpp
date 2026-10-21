#include "lighting.h"

#include "materials.h"

#include <cmath>

Lighting::Lighting(int width, int height)
    : width_(width), height_(height),
      light_(static_cast<size_t>(width) * height, 0.15f) {}

void Lighting::update(const World& world, float playerX, float playerY,
                      float time, bool lightning) {
    // Day/night: sun light level on surface.
    // Cycle length ~ 120 seconds (60 day + 60 night).
    float cycle = std::fmod(time, 120.0f);
    float sun = 0.3f + 0.7f * (0.5f + 0.5f * std::cos((cycle / 120.0f) * 6.2831853f - 3.14159265f));
    // sun ranges ~0.3 (night) to 1.0 (day)

    // Lightning timer.
    if (lightning) lightningTimer_ = 0.5f;
    if (lightningTimer_ > 0) lightningTimer_ -= 1.0f / 60.0f;

    float lightFlash = (lightningTimer_ > 0) ? 1.0f : 0.0f;

    // Reset light grid.
    for (size_t i = 0; i < light_.size(); ++i) light_[i] = 0.12f; // ambient underground

    // Sun: light up sky (cells above ground) and surface.
    for (int x = 0; x < width_; ++x) {
        // Find surface.
        int surf = height_;
        for (int y = 0; y < height_; ++y) {
            if (world.at(x, y) != Mat::Air) { surf = y; break; }
        }
        // Cells above surface (sky) get sun light.
        for (int y = 0; y < surf && y < height_; ++y) {
            light_[static_cast<size_t>(y) * width_ + x] = sun;
        }
        // Surface cell itself.
        if (surf < height_) {
            light_[static_cast<size_t>(surf) * width_ + x] = std::max(light_[static_cast<size_t>(surf) * width_ + x], sun * 0.8f);
        }
    }

    // Lightning flash: light up sky.
    if (lightFlash > 0) {
        for (int x = 0; x < width_; ++x) {
            int surf = height_;
            for (int y = 0; y < height_; ++y) {
                if (world.at(x, y) != Mat::Air) { surf = y; break; }
            }
            for (int y = 0; y <= surf && y < height_; ++y) {
                light_[static_cast<size_t>(y) * width_ + x] = std::max(light_[static_cast<size_t>(y) * width_ + x], lightFlash);
            }
        }
    }

    // Fire light: radius 8.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            Mat m = world.at(x, y);
            uint8_t life = world.life(x, y);
            bool isFire = (m == Mat::Fire) || (life > 0 && materialInfo(m).flammable);
            if (!isFire) continue;
            // Emit light.
            for (int dy = -8; dy <= 8; ++dy) {
                for (int dx = -8; dx <= 8; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
                    float dist = std::sqrt((float)(dx*dx + dy*dy));
                    if (dist > 8.0f) continue;
                    float contrib = (1.0f - dist / 8.0f) * 0.9f;
                    size_t idx = static_cast<size_t>(ny) * width_ + nx;
                    if (contrib > light_[idx]) light_[idx] = contrib;
                }
            }
        }
    }

    // Player light: radius 6.
    {
        int px = static_cast<int>(playerX);
        int py = static_cast<int>(playerY);
        for (int dy = -6; dy <= 6; ++dy) {
            for (int dx = -6; dx <= 6; ++dx) {
                int nx = px + dx, ny = py + dy;
                if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
                float dist = std::sqrt((float)(dx*dx + dy*dy));
                if (dist > 6.0f) continue;
                float contrib = (1.0f - dist / 6.0f) * 0.6f;
                size_t idx = static_cast<size_t>(ny) * width_ + nx;
                if (contrib > light_[idx]) light_[idx] = contrib;
            }
        }
    }
}
