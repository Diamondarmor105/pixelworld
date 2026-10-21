#include "render.h"

namespace {

// Realistic flame palette: white core -> yellow -> orange -> red -> dark red.
constexpr Color kFireCore{255, 250, 220, 255};
constexpr Color kFireHot{255, 200, 60, 255};
constexpr Color kFireMid{240, 110, 20, 255};
constexpr Color kFireCool{200, 50, 8, 255};
constexpr Color kFireDeep{120, 15, 2, 255};

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
                // Fire flickers with more variation.
                int flicker = (x * 7 + y * 13 + life * 31) % 6;
                if (flicker < 1) color = kFireCore;
                else if (flicker < 2) color = kFireHot;
                else if (flicker < 3) color = kFireMid;
                else if (flicker < 5) color = kFireCool;
                else color = kFireDeep;
            } else {
                color = materialInfo(m).color;
                if (materialInfo(m).flows || materialInfo(m).rises) {
                    const float mass = world.mass(x, y);
                    const float full = mass > 1.0f ? 1.0f : (mass < 0.0f ? 0.0f : mass);
                    const Color air = materialInfo(Mat::Air).color;
                    const float mix = 0.45f + 0.55f * full;
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
