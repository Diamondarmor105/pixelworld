#pragma once

#include "blast.h"
#include "player.h"
#include "world.h"

#include <vector>

// How much health the player loses, and from what.
//
// Two things hurt. The first is the material the player is standing in: lava
// is worst, then acid, then fire. The second is an explosion that went off
// nearby - the closer you are to the middle of it, the more it takes off.
//
// The rates are in points of health per second, so a single tick takes a
// sixtieth of each. That way the damage does not depend on how often the
// game happens to be stepping.
namespace Damage {

// Per second.
constexpr float LAVA = 45.0f;
constexpr float ACID = 30.0f;
constexpr float FIRE = 25.0f;

// The most an explosion can take off, right at its centre. This one is a
// single hit, not a rate.
constexpr float BLAST = 60.0f;

}  // namespace Damage

// The health the player loses this tick.
float damageAt(const World& world, const Player& player,
               const std::vector<BlastEvent>& blasts);
