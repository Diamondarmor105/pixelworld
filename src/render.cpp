#include "render.h"

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
            pixels[static_cast<size_t>(y) * world.width() + x] = materialInfo(world.at(x, y)).color;
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
