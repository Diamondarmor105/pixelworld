#pragma once

#include "world.h"

// The little figure you move around.
//
// The player is a 6x10 block of empty space as far as the grid is concerned:
// it is not a material, it does not take part in any simulation rule, and it is
// simply moved by this class and drawn on top. That keeps the player small and
// separate, so it can be tested on its own without a window.
class Player {
public:
    static constexpr int WIDTH = 6;
    static constexpr int HEIGHT = 10;

    // `x` is the left edge, `y` the top, in world cells.
    Player(float x, float y);

    // One tick of movement. `moveInput` is -1 (left), 0 or +1 (right); a jump
    // only happens if the player is standing on something.
    void tick(const World& world, float moveInput, bool jumpInput);

    float x() const { return x_; }
    float y() const { return y_; }
    bool onGround() const { return onGround_; }

    void draw(float offsetX, float offsetY, int scale) const;

private:
    bool collides(const World& world) const;
    // `free` is set while buried: the player moves without asking the grid
    // anything, because every cell they occupy is already solid.
    void moveAxis(const World& world, float amount, bool horizontal, bool free);

    float x_;
    float y_;
    float vx_ = 0.0f;
    float vy_ = 0.0f;
    bool onGround_ = false;
};
