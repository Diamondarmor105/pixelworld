#pragma once

#include "sim.h"

#include <vector>

// The three tools the player can hold. 1/2/3 select them.
enum class Tool { SparkWand = 0, Dynamite = 1, AcidFlask = 2 };

// A projectile in flight. Spark wand shots travel straight; dynamite and acid
// flasks are thrown with gravity and trigger on landing.
struct Projectile {
    float x, y;       // position in world cells
    float vx, vy;     // velocity in cells per tick
    Tool tool;
    int life;         // ticks remaining before forced detonation
    bool done;        // true once it has triggered
};

// Manages the active projectiles and applies their effects to the world.
class Tools {
public:
    Tools() = default;

    void setTool(Tool t) { selected_ = t; }
    Tool selected() const { return selected_; }

    // Fire the selected tool from (sx, sy) toward (tx, ty).
    void fire(float sx, float sy, float tx, float ty);

    // Advance all projectiles one tick, applying effects on impact.
    void tick(World& world, Simulation& sim);

    const std::vector<Projectile>& projectiles() const { return projectiles_; }

    static const char* name(Tool t);

    float lastExplosionX() const { return lastExplosionX_; }
    float lastExplosionY() const { return lastExplosionY_; }
    float lastExplosionTimer() const { return lastExplosionTimer_; }
    void clearExplosion() { lastExplosionTimer_ = 0; }

private:
    void detonate(World& world, Projectile& p);
    Tool selected_ = Tool::SparkWand;
    std::vector<Projectile> projectiles_;
    int cooldown_ = 0;
    float lastExplosionX_ = 0, lastExplosionY_ = 0, lastExplosionTimer_ = 0;
};
