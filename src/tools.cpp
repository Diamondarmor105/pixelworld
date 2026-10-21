#include "tools.h"

#include "blast.h"
#include "materials.h"
#include "sim.h"

#include <cmath>
#include <tuple>

const char* Tools::name(Tool t) {
    switch (t) {
        case Tool::SparkWand: return "SPARK WAND";
        case Tool::Dynamite:  return "DYNAMITE";
        case Tool::AcidFlask: return "ACID FLASK";
    }
    return "?";
}

void Tools::fire(float sx, float sy, float tx, float ty) {
    if (cooldown_ > 0) return;
    float dx = tx - sx;
    float dy = ty - sy;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    Projectile p;
    p.x = sx;
    p.y = sy;
    p.done = false;

    switch (selected_) {
        case Tool::SparkWand:
            p.vx = dx / len * 2.0f;
            p.vy = dy / len * 2.0f;
            p.life = 60;
            p.tool = Tool::SparkWand;
            cooldown_ = 8;
            break;
        case Tool::Dynamite: {
            float speed = std::min(len * 0.08f, 2.0f);
            if (speed < 0.4f) speed = 0.4f;
            p.vx = dx / len * speed;
            p.vy = dy / len * speed - 0.3f;
            p.life = 150;
            p.tool = Tool::Dynamite;
            cooldown_ = 20;
            break;
        }
        case Tool::AcidFlask: {
            float speed = std::min(len * 0.08f, 1.5f);
            if (speed < 0.4f) speed = 0.4f;
            p.vx = dx / len * speed;
            p.vy = dy / len * speed - 0.2f;
            p.life = 150;
            p.tool = Tool::AcidFlask;
            cooldown_ = 20;
            break;
        }
    }
    projectiles_.push_back(p);
}

void Tools::tick(World& world) {
    if (cooldown_ > 0) --cooldown_;

    for (auto& p : projectiles_) {
        if (p.done) continue;

        // Gravity for thrown objects with air resistance.
        if (p.tool != Tool::SparkWand) {
            p.vy += 0.05f;
            p.vx *= 0.995f;
            p.vy *= 0.995f;
        }

        // Sub-step movement to prevent tunneling through pixels.
        float speed = std::sqrt(p.vx * p.vx + p.vy * p.vy);
        int steps = (int)std::ceil(speed);
        if (steps < 1) steps = 1;
        float sx = p.vx / steps;
        float sy = p.vy / steps;

        bool triggered = false;
        for (int s = 0; s < steps && !triggered; ++s) {
            p.x += sx;
            p.y += sy;

            int ix = static_cast<int>(std::floor(p.x));
            int iy = static_cast<int>(std::floor(p.y));

            if (!world.inBounds(ix, iy)) { p.done = true; triggered = true; break; }

            Mat here = world.at(ix, iy);

            if (p.tool == Tool::SparkWand) {
                if (here != Mat::Air && !materialInfo(here).flows && !materialInfo(here).rises) {
                    int r = 1 + (rand() % 2);
                    for (int dy = -r; dy <= r; ++dy)
                        for (int dx = -r; dx <= r; ++dx) {
                            if (dx * dx + dy * dy > r * r + (rand() % 4)) continue;
                            int fx = ix + dx, fy = iy + dy;
                            if (!world.inBounds(fx, fy)) continue;
                            Mat m = world.at(fx, fy);
                            if (m == Mat::Air || materialInfo(m).flammable) {
                                world.set(fx, fy, Mat::Fire);
                                world.setLife(fx, fy, FIRE_LIFE + (rand() % 4));
                            }
                        }
                    p.done = true;
                    triggered = true;
                }
            } else {
                bool hit = (here != Mat::Air && !materialInfo(here).flows && !materialInfo(here).rises);
                if (hit || p.life <= 0 || p.y >= world.height() - 2) {
                    detonate(world, p);
                    p.done = true;
                    triggered = true;
                }
            }
        }

        --p.life;
        if (p.life <= 0 && !p.done) {
            if (p.tool != Tool::SparkWand) {
                detonate(world, p);
            }
            p.done = true;
        }
    }

    // Remove done projectiles.
    std::vector<Projectile> alive;
    for (auto& p : projectiles_) if (!p.done) alive.push_back(p);
    projectiles_ = alive;
}

void Tools::detonate(World& world, Projectile& p) {
    int cx = static_cast<int>(std::floor(p.x));
    int cy = static_cast<int>(std::floor(p.y));

    if (p.tool == Tool::Dynamite) {
        int radius = 12 + (rand() % 5); // 12-16 (more power)
        int r2 = radius * radius;
        int waterCount = 0, total = 0;
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int x = cx - radius; x <= cx + radius; ++x) {
                if (!world.inBounds(x, y)) continue;
                int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy > r2) continue;
                ++total;
                if (world.at(x, y) == Mat::Water) ++waterCount;
            }
        }
        const bool underwater = (waterCount > total / 3);

        // The same blast rules as every other explosion: the wave breaks the
        // blocks in the line of sight first, then spends what is left of its
        // energy on the next block along the line.
        BlastOptions options;
        options.radius = radius;
        options.power = 1.0f;
        options.underwater = underwater;
        options.flames = !underwater;  // underwater it makes bubbles, not fire
        options.smokePuffs = underwater ? 12 : 24;
        blast(world, cx, cy, options);
    } else if (p.tool == Tool::AcidFlask) {
        // Splash acid in a pool.
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                int x = cx + dx, y = cy + dy;
                if (!world.inBounds(x, y)) continue;
                if (world.at(x, y) == Mat::Air) {
                    world.setMass(x, y, Mat::Acid, 0.6f + (rand() % 40) / 100.0f);
                }
            }
        }
    }
}

