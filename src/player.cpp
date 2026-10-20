#include "player.h"

#include <algorithm>
#include <cmath>

#include "raylib.h"

namespace {

// All of these are in cells per tick, and the game ticks 60 times a second.
// Working per tick rather than per second keeps the movement identical on a
// slow frame and a fast one, which is what lets the self-test assert on exact
// positions.
constexpr float WALK_SPEED = 1.2f;
constexpr float JUMP_SPEED = 5.0f;
constexpr float GRAVITY = 0.45f;
constexpr float MAX_FALL = 8.0f;

// How many cells the player can simply step up onto while walking. Enough to
// walk over the bumps in the ground and the edges of small piles without
// noticing; far less than a wall, a cliff or the side of a pit, which still
// stop you dead.
constexpr int MAX_STEP_UP = 2;

// How deep a heap of loose grain the player can plough through. Each cell of
// push displaces one column of grain, so this is roughly "how many columns of
// sand you can shove aside at once" before it becomes an immovable wall.
constexpr int MAX_PUSH = 3;

// What the player cannot walk through. Air and fire are passable, and so are
// leaves - you push through foliage rather than being stopped by it. Liquids
// are something you wade through and sink into rather than stand on. Everything
// else stops you - unless it is loose enough to shove (see pushLoose).
bool blocksPlayer(Mat m) {
    if (m == Mat::Air || m == Mat::Fire || m == Mat::Foliage) return false;
    return !materialInfo(m).flows;
}

// Loose grains the player can push, as opposed to a solid wall of stone.
bool isLoose(Mat m) {
    return materialInfo(m).falls && !materialInfo(m).flows;
}

}  // namespace

Player::Player(float x, float y) : x_(x), y_(y) {}

void Player::tick(World& world, float moveInput, bool jumpInput) {
    vx_ = moveInput * WALK_SPEED;
    if (jumpInput && onGround_) vy_ = -JUMP_SPEED;

    vy_ += GRAVITY;
    if (vy_ > MAX_FALL) vy_ = MAX_FALL;

    // Buried: material has fallen onto them. Falling any further would only
    // push them deeper, so gravity is off and they climb steadily out instead,
    // in whatever direction is being held.
    if (collides(world)) {
        onGround_ = true;
        vy_ = 0.0f;
        moveAxis(world, vx_, true, true);
        moveAxis(world, -1.0f, false, true);
        return;
    }

    onGround_ = false;
    moveAxis(world, vx_, true, false);
    moveAxis(world, vy_, false, false);

    // Never fall out of the bottom of the world, and never above the sky or off
    // either end of the level.
    if (y_ < 0.0f) {
        y_ = 0.0f;
        vy_ = 0.0f;
    }
    const float floorY = static_cast<float>(world.height() - HEIGHT);
    if (y_ > floorY) {
        y_ = floorY;
        vy_ = 0.0f;
        onGround_ = true;
    }
    const float maxX = static_cast<float>(world.width() - WIDTH);
    if (x_ < 0.0f) {
        x_ = 0.0f;
        vx_ = 0.0f;
    } else if (x_ > maxX) {
        x_ = maxX;
        vx_ = 0.0f;
    }
}

void Player::moveAxis(World& world, float amount, bool horizontal, bool free) {
    if (free) {
        if (horizontal) x_ += amount;
        else y_ += amount;
        return;
    }

    float remaining = amount;
    while (remaining != 0.0f) {
        // At most one cell at a time, so a fast fall cannot tunnel through the
        // floor in a single tick.
        const float step = (remaining > 0.0f) ? std::min(1.0f, remaining)
                                              : std::max(-1.0f, remaining);
        if (horizontal) x_ += step;
        else y_ += step;
        remaining -= step;

        if (!collides(world)) continue;

        if (horizontal) {
            // Put them back, then try to shovel the loose grain ahead of them
            // out of the way before giving up.
            x_ -= step;
            if (pushLoose(world, step)) {
                x_ += step;
                if (!collides(world)) continue;
                x_ -= step;
            }
            // Still stuck: maybe the thing in the way is small enough to step
            // onto instead of stopping for.
            const float groundY = y_;
            for (int rise = 1; rise <= MAX_STEP_UP; ++rise) {
                y_ = groundY - static_cast<float>(rise);
                if (collides(world)) continue;
                x_ += step;
                if (!collides(world)) return;  // stepped up over it
                x_ -= step;
            }
            y_ = groundY;
            vx_ = 0.0f;
            return;
        }

        // Put the player back where they were and stop them dead that way.
        y_ -= step;
        if (step > 0.0f) onGround_ = true;  // the floor caught them
        vy_ = 0.0f;
        return;
    }
}

bool Player::pushLoose(World& world, float direction) const {
    const int dir = (direction > 0.0f) ? 1 : -1;
    const int front = (dir > 0) ? static_cast<int>(std::floor(x_)) + WIDTH
                                : static_cast<int>(std::floor(x_)) - 1;
    const int top = static_cast<int>(std::floor(y_));

    // Every cell in the player's front column must be loose (or air), and the
    // column has to end at open space within MAX_PUSH cells, otherwise this is
    // a wall, not a heap.
    for (int cy = top; cy < top + HEIGHT; ++cy) {
        const Mat m = world.at(front, cy);
        if (m == Mat::Air) continue;
        if (!isLoose(m)) return false;
    }

    int reach = 1;
    for (;; ++reach) {
        const int px = front + dir * reach;
        if (!world.inBounds(px, top)) return false;
        bool empty = true;
        for (int cy = top; cy < top + HEIGHT; ++cy) {
            if (world.at(px, cy) != Mat::Air) { empty = false; break; }
        }
        if (empty) break;
        if (reach > MAX_PUSH) return false;
    }

    // Shift each column forward, from the far end back toward the player, so no
    // grain is overwritten on the way.
    for (int r = reach; r >= 1; --r) {
        const int from = front + dir * (r - 1);
        const int to = front + dir * r;
        for (int cy = top; cy < top + HEIGHT; ++cy) {
            world.set(to, cy, world.at(from, cy));
            world.set(from, cy, Mat::Air);
        }
    }
    return true;
}

bool Player::collides(const World& world) const {
    const int left = static_cast<int>(std::floor(x_));
    const int top = static_cast<int>(std::floor(y_));
    for (int cy = top; cy < top + HEIGHT; ++cy) {
        for (int cx = left; cx < left + WIDTH; ++cx) {
            if (blocksPlayer(world.at(cx, cy))) return true;
        }
    }
    return false;
}

void Player::draw(float offsetX, float offsetY, int scale) const {
    // The sprite is placed from the same whole-cell origin as `collides`, not
    // from the raw float position: `collides` tests the cells the player
    // actually occupies, which start at floor(x_). Rounding the float instead
    // draws the body up to a whole cell to the right of its own collision box.
    const float px = offsetX + std::floor(x_) * scale;
    const float py = offsetY + std::floor(y_) * scale;

    // A head and a body, so the block reads as a little person.
    DrawRectangle(static_cast<int>(px), static_cast<int>(py),
                  WIDTH * scale, 3 * scale, Color{248, 210, 170, 255});
    DrawRectangle(static_cast<int>(px), static_cast<int>(py) + 3 * scale,
                  WIDTH * scale, (HEIGHT - 3) * scale, Color{226, 88, 74, 255});
}
