#include "damage.h"

#include "materials.h"

#include <cmath>

namespace {

// The game steps sixty times a second.
constexpr float TICKS_PER_SECOND = 60.0f;

}  // namespace

float damageAt(const World& world, const Player& player,
               const std::vector<BlastEvent>& blasts) {
    // The material the player is standing in. Only the worst thing they are
    // touching counts: standing in a big fire is not six times worse than
    // standing in a small one.
    float worstRate = 0.0f;
    const int left = static_cast<int>(player.x());
    const int right = left + Player::WIDTH - 1;
    const int top = static_cast<int>(player.y());
    const int bottom = top + Player::HEIGHT - 1;

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (!world.inBounds(x, y)) continue;
            const Mat m = world.at(x, y);
            float rate = 0.0f;
            if (m == Mat::Lava) rate = Damage::LAVA;
            else if (m == Mat::Acid) rate = Damage::ACID;
            else if (m == Mat::Fire) rate = Damage::FIRE;
            if (rate > worstRate) worstRate = rate;
        }
    }

    float damage = worstRate / TICKS_PER_SECOND;

    // Explosions. The closer the player is to the middle of one, the more it
    // hurts, and it hurts nothing at all from the far edge of its reach.
    const float px = player.x() + Player::WIDTH * 0.5f;
    const float py = player.y() + Player::HEIGHT * 0.5f;
    for (const BlastEvent& b : blasts) {
        const float dx = px - static_cast<float>(b.x);
        const float dy = py - static_cast<float>(b.y);
        const float distance = std::sqrt(dx * dx + dy * dy);
        const float reach = static_cast<float>(b.radius);
        if (distance >= reach) continue;
        const float closeness = 1.0f - distance / reach;
        const float hit = Damage::BLAST * closeness * closeness;
        if (hit > damage) damage = hit;
    }

    return damage;
}
