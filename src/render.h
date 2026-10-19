#pragma once

#include "raylib.h"

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

    // Copies the colours of every cell into the image buffer.
    void updateImage(const World& world);

    // Uploads the image buffer (if needed) and draws it at `scale`
    // with nearest-neighbour filtering, so cells stay sharp squares.
    void drawScaled(int scale, int offsetX = 0, int offsetY = 0);

    const Image& image() const { return image_; }

private:
    Image image_{};
    Texture2D texture_{};
    bool textureReady_ = false;
};
