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
    // Fire starts bright yellow and fades to deep red as it burns out.
    float nl = static_cast<float>(life) / FIRE_LIFE;
    if (nl > 1.0f) nl = 1.0f;
    // Small spatial flicker for organic look.
    float flicker = static_cast<float>((x * 7 + y * 13 + life * 3) % 100) / 100.0f;
    float temp = nl * 0.85f + flicker * 0.15f;

    Color brightYellow{255, 240, 80, 255};
    Color yellow{255, 180, 30, 255};
    Color orange{255, 100, 10, 255};
    Color redOrange{220, 40, 5, 255};
    Color deepRed{140, 10, 2, 255};

    if (temp > 0.8f) {
        return lerpColor(yellow, brightYellow, (temp - 0.8f) / 0.2f);
    } else if (temp > 0.5f) {
        return lerpColor(orange, yellow, (temp - 0.5f) / 0.3f);
    } else if (temp > 0.2f) {
        return lerpColor(redOrange, orange, (temp - 0.2f) / 0.3f);
    } else {
        return lerpColor(deepRed, redOrange, temp / 0.2f);
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
