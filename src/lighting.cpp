#include "lighting.h"

#include "materials.h"

#include <cmath>

Lighting::Lighting(int width, int height)
    : width_(width), height_(height),
      light_(static_cast<size_t>(width) * height, 0.15f),
      tintR_(static_cast<size_t>(width) * height, 1.0f),
      tintG_(static_cast<size_t>(width) * height, 1.0f),
      tintB_(static_cast<size_t>(width) * height, 1.0f) {}

bool Lighting::lineOfSight(const World& world, int x0, int y0, int x1, int y1) {
    // Bresenham line; blocked if a solid (non-air, non-flow, non-rise) cell
    // lies between source and target.
    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;
    while (true) {
        if (x == x1 && y == y1) return true;
        // Check blocking (skip source cell).
        if (!(x == x0 && y == y0)) {
            Mat m = world.at(x, y);
            if (m != Mat::Air && !materialInfo(m).flows && !materialInfo(m).rises && m != Mat::Fire) {
                return false;
            }
        }
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
        if (x < 0 || y < 0) return false;
    }
}

void Lighting::update(const World& world, float playerX, float playerY,
                      float time, bool lightning, float explosionX, float explosionY, float explosionTimer) {
    if (lightning) lightningTimer_ = 0.4f;
    if (lightningTimer_ > 0) lightningTimer_ -= 1.0f / 60.0f;
    if (explosionTimer > 0) { explosionTimer_ = explosionTimer; explosionX_ = explosionX; explosionY_ = explosionY; }
    if (explosionTimer_ > 0) explosionTimer_ -= 1.0f / 60.0f;

    float lightFlash = (lightningTimer_ > 0) ? 1.0f : 0.0f;
    float cycle = std::fmod(time, 120.0f);
    float sun = 0.3f + 0.7f * (0.5f + 0.5f * std::cos((cycle / 120.0f) * 6.2831853f - 3.14159265f));

    for (size_t i = 0; i < light_.size(); ++i) {
        light_[i] = 0.12f;
        tintR_[i] = 1.0f; tintG_[i] = 1.0f; tintB_[i] = 1.0f;
    }

    // Sun: light up sky.
    for (int x = 0; x < width_; ++x) {
        int surf = height_;
        for (int y = 0; y < height_; ++y) {
            if (world.at(x, y) != Mat::Air) { surf = y; break; }
        }
        for (int y = 0; y < surf && y < height_; ++y) {
            light_[idx(x, y)] = sun;
        }
        if (surf < height_) {
            size_t i = idx(x, surf);
            if (sun * 0.8f > light_[i]) light_[i] = sun * 0.8f;
        }
    }

    // Lightning flash.
    if (lightFlash > 0) {
        for (int x = 0; x < width_; ++x) {
            int surf = height_;
            for (int y = 0; y < height_; ++y) {
                if (world.at(x, y) != Mat::Air) { surf = y; break; }
            }
            for (int y = 0; y <= surf && y < height_; ++y) {
                size_t i = idx(x, y);
                if (lightFlash > light_[i]) light_[i] = lightFlash;
            }
        }
    }

    // Explosion flash.
    if (explosionTimer_ > 0) {
        float flash = explosionTimer_ * 4.0f; // bright then fade
        if (flash > 1.0f) flash = 1.0f;
        int cx = (int)explosionX_, cy = (int)explosionY_;
        for (int dy = -12; dy <= 12; ++dy) {
            for (int dx = -12; dx <= 12; ++dx) {
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
                float dist = std::sqrt((float)(dx*dx + dy*dy));
                if (dist > 12.0f) continue;
                float contrib = flash * (1.0f - dist / 12.0f);
                size_t i = idx(nx, ny);
                if (contrib > light_[i]) light_[i] = contrib;
            }
        }
    }

    // Fire light: radius 10, with occlusion and flicker.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            Mat m = world.at(x, y);
            uint8_t life = world.life(x, y);
            bool isFire = (m == Mat::Fire) || (life > 0 && materialInfo(m).flammable);
            if (!isFire) continue;

            // Flicker: varies by position and time.
            float flicker = 0.85f + 0.15f * std::sin(time * 12.0f + x * 0.7f + y * 1.3f);

            for (int dy = -10; dy <= 10; ++dy) {
                for (int dx = -10; dx <= 10; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
                    float dist = std::sqrt((float)(dx*dx + dy*dy));
                    if (dist > 10.0f) continue;
                    // Occlusion check.
                    if (!lineOfSight(world, x, y, nx, ny)) continue;
                    float contrib = (1.0f - dist / 10.0f) * 0.9f * flicker;
                    size_t i = idx(nx, ny);
                    if (contrib > light_[i]) {
                        light_[i] = contrib;
                        // Warm tint for fire light.
                        tintR_[i] = 1.0f;
                        tintG_[i] = 0.7f;
                        tintB_[i] = 0.4f;
                    }
                }
            }
        }
    }

    // Player light: radius 7, white.
    {
        int px = static_cast<int>(playerX);
        int py = static_cast<int>(playerY);
        for (int dy = -7; dy <= 7; ++dy) {
            for (int dx = -7; dx <= 7; ++dx) {
                int nx = px + dx, ny = py + dy;
                if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
                float dist = std::sqrt((float)(dx*dx + dy*dy));
                if (dist > 7.0f) continue;
                if (!lineOfSight(world, px, py, nx, ny)) continue;
                float contrib = (1.0f - dist / 7.0f) * 0.5f;
                size_t i = idx(nx, ny);
                if (contrib > light_[i]) {
                    light_[i] = contrib;
                    tintR_[i] = 1.0f; tintG_[i] = 1.0f; tintB_[i] = 1.0f;
                }
            }
        }
    }
}
