#pragma once

// Which part of the world is on screen.
//
// The camera is just a top-left corner in world cells. It follows the player
// with a small dead zone, so a bit of shuffling around does not make the whole
// world jitter, and it is clamped so you cannot see past the top or bottom of
// the world. It is deliberately not clamped left or right: the world will be
// endless that way round.
class GameCamera {
public:
    GameCamera(int viewWidth, int viewHeight, float worldX, float worldY);

    // Scrolls to keep `worldX`/`worldY` inside the dead zone around the middle.
    void follow(float worldX, float worldY);

    // Keeps the view inside the world vertically, and anywhere horizontally.
    void clamp(int worldWidth, int worldHeight);

    int viewX() const { return viewX_; }
    int viewY() const { return viewY_; }

private:
    int viewWidth_;
    int viewHeight_;
    float x_;
    float y_;
    int viewX_ = 0;
    int viewY_ = 0;
};
