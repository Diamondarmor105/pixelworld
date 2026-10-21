#include "materials.h"
#include "render.h"
#include "world.h"

#include <algorithm>
#include <cmath>

namespace {

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
                // Fire color: bright yellow center -> orange -> red edges.
                // Use life and position for flickering.
                int t = (x * 3 + y * 7 + life * 11) % 7;
                if (t == 0) color = Color{255, 240, 100, 255};       // bright yellow
                else if (t <= 2) color = Color{255, 180, 40, 255};   // yellow-orange
                else if (t <= 4) color = Color{255, 100, 15, 255};   // orange
                else if (t <= 5) color = Color{220, 50, 5, 255};     // red-orange
                else color = Color{160, 20, 2, 255};                 // deep red
            } else {
                color = materialInfo(m).color;
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
