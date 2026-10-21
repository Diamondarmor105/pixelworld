#include "materials.h"
#include "render.h"
#include "world.h"

#include <algorithm>
#include <cmath>

namespace {

// Realistic flame palette based on temperature.
constexpr Color kFireCore{255, 255, 240, 255};
constexpr Color kFireHot{255, 220, 80, 255};
constexpr Color kFireMid{255, 140, 20, 255};
constexpr Color kFireCool{230, 60, 8, 255};
constexpr Color kFireDeep{160, 20, 3, 255};

static Color lerpColor(Color c1, Color c2, float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return Color{
        static_cast<unsigned char>(c1.r + (c2.r - c1.r) * t),
        static_cast<unsigned char>(c1.g + (c2.g - c1.g) * t),
        static_cast<unsigned char>(c1.b + (c2.b - c1.b) * t),
        static_cast<unsigned char>(c1.a + (c2.a - c1.a) * t)
    };
}

static Color getFireColor(uint8_t life, int x, int y) {
    float flicker = static_cast<float>((x * 7 + y * 13 + life * 3) % 100) / 100.0f;
    float normalizedLife = static_cast<float>(life) / 255.0f;
    float temp = normalizedLife * 0.7f + flicker * 0.3f;

    if (temp > 0.8f) {
        return lerpColor(kFireHot, kFireCore, (temp - 0.8f) / 0.2f);
    } else if (temp > 0.5f) {
        return lerpColor(kFireMid, kFireHot, (temp - 0.5f) / 0.3f);
    } else if (temp > 0.2f) {
        return lerpColor(kFireCool, kFireMid, (temp - 0.2f) / 0.3f);
    } else {
        return lerpColor(kFireDeep, kFireCool, temp / 0.2f);
    }
}

}  // namespace

Renderer::Renderer(int width, int height) {
    image_ = GenImageColor(width, height, BLACK);
}

Renderer::~Renderer() {
    if (textureReady_) UnloadTexture(texture_);
    UnloadImage(image_);
}

void Renderer::updateImage(const World& world) {
    Color* pixels = static_cast<Color*>(image_.data);
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            const Mat m = world.at(x, y);
            const uint8_t life = world.life(x, y);

            Color color;
            if (isBurning(m, life)) {
                color = getFireColor(life, x, y);
            } else {
                color = materialInfo(m).color;
                // Add subtle noise to solids.
                if (!materialInfo(m).flows && !materialInfo(m).rises && m != Mat::Air) {
                    int noise = static_cast<int>((x * 12.9898f + y * 78.233f) * 43758.5453f);
                    int shift = (noise % 15) - 7;
                    color.r = static_cast<unsigned char>(std::clamp((int)color.r + shift, 0, 255));
                    color.g = static_cast<unsigned char>(std::clamp((int)color.g + shift, 0, 255));
                    color.b = static_cast<unsigned char>(std::clamp((int)color.b + shift, 0, 255));
                }
                // Liquid/gas volumetric blending with ease-out.
                if (materialInfo(m).flows || materialInfo(m).rises) {
                    const float mass = world.mass(x, y);
                    const float full = mass > 1.0f ? 1.0f : (mass < 0.0f ? 0.0f : mass);
                    const Color air = materialInfo(Mat::Air).color;
                    float mix = full * full;
                    color.r = static_cast<unsigned char>(air.r + (color.r - air.r) * mix);
                    color.g = static_cast<unsigned char>(air.g + (color.g - air.g) * mix);
                    color.b = static_cast<unsigned char>(air.b + (color.b - air.b) * mix);
                    color.a = 255;
                }
            }

            pixels[static_cast<size_t>(y) * world.width() + x] = color;
        }
    }
}

void Renderer::drawView(int worldX, int worldY, int viewWidth, int viewHeight,
                        int scale, int offsetX, int offsetY) {
    if (!textureReady_) {
        texture_ = LoadTextureFromImage(image_);
        SetTextureFilter(texture_, TEXTURE_FILTER_POINT);
        textureReady_ = true;
    } else {
        UpdateTexture(texture_, image_.data);
    }

    const Rectangle source{static_cast<float>(worldX), static_cast<float>(worldY),
                           static_cast<float>(viewWidth), static_cast<float>(viewHeight)};
    const Rectangle dest{static_cast<float>(offsetX), static_cast<float>(offsetY),
                         static_cast<float>(viewWidth * scale),
                         static_cast<float>(viewHeight * scale)};
    DrawTexturePro(texture_, source, dest, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
}
