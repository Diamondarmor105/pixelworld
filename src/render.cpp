#include "materials.h"
#include "render.h"
#include "world.h"

#include <algorithm>
#include <cmath>

namespace {

static Color lerpColor(Color c1, Color c2, float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return Color{
        static_cast<unsigned char>(c1.r + (c2.r - c1.r) * t),
        static_cast<unsigned char>(c1.g + (c2.g - c1.g) * t),
        static_cast<unsigned char>(c1.b + (c2.b - c1.b) * t),
        255
    };
}

static Color getFireColor(uint8_t life, int x, int y) {
    float nl = static_cast<float>(life) / FIRE_LIFE;
    if (nl > 1.0f) nl = 1.0f;
    float flicker = static_cast<float>((x * 7 + y * 13 + life * 3) % 100) / 100.0f;
    float t = nl * 0.8f + flicker * 0.2f;

    Color white{255, 255, 240, 255};
    Color orange{255, 120, 10, 255};

    return lerpColor(orange, white, t);
}

}  // namespace

Renderer::Renderer(int width, int height)
    : image_(GenImageColor(width, height, BLACK)), width_(width), height_(height) {}

Renderer::~Renderer() {
    if (textureReady_) UnloadTexture(texture_);
    UnloadImage(image_);
}

void Renderer::updateImage(const World& world, int worldX, int worldY) {
    Color* pixels = static_cast<Color*>(image_.data);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const int cellX = worldX + x;
            const int cellY = worldY + y;
            const Mat m = world.at(cellX, cellY);
            const uint8_t life = world.life(cellX, cellY);

            Color color;
            if (isBurning(m, life)) {
                color = getFireColor(life, cellX, cellY);
            } else {
                color = materialInfo(m).color;
                // Liquid/gas volumetric blending with ease-out.
                if (materialInfo(m).flows || materialInfo(m).rises) {
                    const float mass = world.mass(cellX, cellY);
                    const float full = mass > 1.0f ? 1.0f : (mass < 0.0f ? 0.0f : mass);
                    const Color air = materialInfo(Mat::Air).color;
                    // Linear blend with a minimum visibility floor so small
                    // amounts of liquid are still visible.
                    float mix = 0.3f + 0.7f * full;
                    if (mix < 0.3f) mix = 0.3f;
                    color.r = static_cast<unsigned char>(air.r + (color.r - air.r) * mix);
                    color.g = static_cast<unsigned char>(air.g + (color.g - air.g) * mix);
                    color.b = static_cast<unsigned char>(air.b + (color.b - air.b) * mix);
                    color.a = 255;
                }
            }

            pixels[static_cast<size_t>(y) * width_ + x] = color;
        }
    }
}

void Renderer::drawView(int scale, int offsetX, int offsetY) {
    if (!textureReady_) {
        texture_ = LoadTextureFromImage(image_);
        SetTextureFilter(texture_, TEXTURE_FILTER_POINT);
        textureReady_ = true;
    } else {
        UpdateTexture(texture_, image_.data);
    }

    const Rectangle source{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)};
    const Rectangle dest{static_cast<float>(offsetX), static_cast<float>(offsetY),
                         static_cast<float>(width_ * scale),
                         static_cast<float>(height_ * scale)};
    DrawTexturePro(texture_, source, dest, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
}
