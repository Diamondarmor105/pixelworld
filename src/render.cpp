#include "render.h"

namespace {

// Two flame colours, used in a flicker so fire looks alive rather than a flat
// block. A burning cell or a flame cycles between them as its life ticks down.
constexpr Color kFireHot{255, 216, 120, 255};
constexpr Color kFireCool{242, 122, 38, 255};

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
                color = ((x + y + life) & 1) ? kFireHot : kFireCool;
            } else {
                color = materialInfo(m).color;
            }
            pixels[static_cast<size_t>(y) * world.width() + x] = color;
        }
    }
}

void Renderer::drawScaled(int scale, int offsetX, int offsetY) {
    if (!textureReady_) {
        texture_ = LoadTextureFromImage(image_);
        SetTextureFilter(texture_, TEXTURE_FILTER_POINT);
        textureReady_ = true;
    } else {
        UpdateTexture(texture_, image_.data);
    }
    DrawTextureEx(texture_, Vector2{static_cast<float>(offsetX), static_cast<float>(offsetY)},
                  0.0f, static_cast<float>(scale), WHITE);
}
