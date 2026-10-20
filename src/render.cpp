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
                // A liquid cell holds a fraction of a unit of liquid, so draw a
                // partly filled cell lighter. A shallow film then reads as thin
                // water rather than as a solid block, which is what makes the
                // surface of a pool look crisp.
                if (materialInfo(m).flows) {
                    const float mass = world.mass(x, y);
                    const float full = mass > 1.0f ? 1.0f : mass;
                    color.a = static_cast<unsigned char>(60 + 195 * full);
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
