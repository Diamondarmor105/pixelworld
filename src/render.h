#pragma once

#include "world.h"


// Turns the material grid into pixels on screen.
//
// Two steps, and the split matters: updateImage() writes one colour per cell
// into an image buffer that lives in normal memory, and drawView() uploads
// that buffer to the GPU once and draws it stretched. The world is never drawn
// one pixel at a time.
//
// The buffer holds exactly the part of the world that is on screen. The world
// itself is endless, so there is nothing bigger to hold.
class Renderer {
public:
    Renderer(int width, int height);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Copies the colours of the cells at (worldX, worldY) and around them into
    // the image buffer - one cell per pixel, top-left corner of the view at
    // (worldX, worldY).
    void updateImage(const World& world, int worldX, int worldY);

    // Uploads the image buffer (if needed) and draws it stretched with
    // nearest-neighbour filtering, so cells stay sharp squares.
    void drawView(int scale, int offsetX = 0, int offsetY = 0);

    const Image& image() const { return image_; }

private:
    Image image_{};
    Texture2D texture_{};
    bool textureReady_ = false;
    int width_ = 0;
    int height_ = 0;
};
