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

    // Health, from full down to nothing. At zero the player is dead.
    static constexpr float MAX_HEALTH = 100.0f;

    // `x` is the left edge, `y` the top, in world cells.
    Player(float x, float y);

    // One tick of movement. `moveInput` is -1 (left), 0 or +1 (right); a jump
    // only happens if the player is standing on something. The world is
    // mutable because walking into a loose pile of sand or gravel shoves it
    // out of the way instead of stopping dead.
    void tick(World& world, float moveInput, bool jumpInput);

    float x() const { return x_; }
    float y() const { return y_; }
    bool onGround() const { return onGround_; }

    float health() const { return health_; }
    bool dead() const { return health_ <= 0.0f; }

    // Hurts the player. The health never goes below zero.
    void damage(float amount);

    // Puts the player back at the start of the world, whole again.
    void respawn(float x, float y);

    void draw(float offsetX, float offsetY, int scale) const;

private:
    bool collides(const World& world) const;
    // `free` is set while buried: the player moves without asking the grid
    // anything, because every cell they occupy is already solid.
    void moveAxis(World& world, float amount, bool horizontal, bool free);
    // Walking into a loose grain (sand, gravel, soil, snow) pushes it aside:
    // the cell directly ahead is moved one step further ahead, and further on
    // again if that is loose too, so a small heap can be ploughed through but a
    // deep drift eventually becomes a wall. Returns true if room was found.
    bool pushLoose(World& world, float direction) const;

    float x_;
    float y_;
    float vx_ = 0.0f;
    float vy_ = 0.0f;
    bool onGround_ = false;
    float health_ = MAX_HEALTH;
};
