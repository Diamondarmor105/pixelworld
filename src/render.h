#pragma once

#include "world.h"


// Turns the material grid into pixels on screen.
//
// Two steps, and the split matters: updateImage() writes one colour per cell
// into an image buffer that lives in normal memory, and drawScaled() uploads
// that buffer to the GPU once and draws it stretched. The world is never drawn
// one pixel at a time.
class Renderer {
public:
    Renderer(int width, int height);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Copies the colours of every cell into the image buffer, modulated by
    // per-cell light (0..1). `light` may be null to skip lighting.
    void updateImage(const World& world);

    // Uploads the image buffer (if needed) and draws the part of the world at
    // (worldX, worldY), `viewWidth` by `viewHeight` cells, stretched with
    // nearest-neighbour filtering so cells stay sharp squares.
    void drawView(int worldX, int worldY, int viewWidth, int viewHeight,
                  int scale, int offsetX = 0, int offsetY = 0);

    const Image& image() const { return image_; }

private:
    Image image_{};
    Texture2D texture_{};
    bool textureReady_ = false;
};
