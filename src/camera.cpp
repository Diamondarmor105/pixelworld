#include "camera.h"

#include <algorithm>

namespace {

// How much of the view, as a fraction, the player can move around in before the
// camera starts chasing them.
constexpr float DEAD_ZONE_X = 0.18f;
constexpr float DEAD_ZONE_Y = 0.15f;

}  // namespace

GameCamera::GameCamera(int viewWidth, int viewHeight, float worldX, float worldY)
    : viewWidth_(viewWidth), viewHeight_(viewHeight), x_(worldX - viewWidth * 0.5f),
      y_(worldY - viewHeight * 0.5f) {
    viewX_ = static_cast<int>(x_);
    viewY_ = static_cast<int>(y_);
}

void GameCamera::follow(float worldX, float worldY) {
    const float centreX = x_ + viewWidth_ * 0.5f;
    const float centreY = y_ + viewHeight_ * 0.5f;

    const float dx = worldX - centreX;
    const float dy = worldY - centreY;
    const float deadX = viewWidth_ * DEAD_ZONE_X;
    const float deadY = viewHeight_ * DEAD_ZONE_Y;

    // Move by however much the player overshot the dead zone, not all the way to
    // the middle - so the camera settles instead of chasing every step.
    if (dx < -deadX) x_ += dx + deadX;
    else if (dx > deadX) x_ += dx - deadX;

    if (dy < -deadY) y_ += dy + deadY;
    else if (dy > deadY) y_ += dy - deadY;

    viewX_ = static_cast<int>(x_);
    viewY_ = static_cast<int>(y_);
}

void GameCamera::clamp(int worldWidth, int worldHeight) {
    // Vertical: never look above the sky or below the bottom of the world.
    const float maxY = static_cast<float>(worldHeight - viewHeight_);
    y_ = std::clamp(y_, 0.0f, std::max(0.0f, maxY));

    // Horizontal: only while the level is a fixed size. The endless world in
    // slice 6 drops this, because there is always more world that way.
    const float maxX = static_cast<float>(worldWidth - viewWidth_);
    x_ = std::clamp(x_, 0.0f, std::max(0.0f, maxX));

    viewX_ = static_cast<int>(x_);
    viewY_ = static_cast<int>(y_);
}
