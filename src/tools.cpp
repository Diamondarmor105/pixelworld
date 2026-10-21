#include "tools.h"

#include "materials.h"
#include "sim.h"

#include <cmath>

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
            p.vx = dx / len * 1.2f;
            p.vy = dy / len * 1.2f;
            p.life = 60;
            p.tool = Tool::SparkWand;
            cooldown_ = 8;
            break;
        case Tool::Dynamite:
            p.vx = dx / len * 0.6f;
            p.vy = dy / len * 0.6f - 0.3f;
            p.life = 120;
            p.tool = Tool::Dynamite;
            cooldown_ = 20;
            break;
        case Tool::AcidFlask:
            p.vx = dx / len * 0.7f;
            p.vy = dy / len * 0.7f - 0.2f;
            p.life = 120;
            p.tool = Tool::AcidFlask;
            cooldown_ = 20;
            break;
    }
    projectiles_.push_back(p);
}

void Tools::tick(World& world, Simulation& sim) {
    if (cooldown_ > 0) --cooldown_;

    for (auto& p : projectiles_) {
        if (p.done) continue;

        // Gravity for thrown objects.
        if (p.tool != Tool::SparkWand) {
            p.vy += 0.05f;
        }

        p.x += p.vx;
        p.y += p.vy;
        --p.life;

        int ix = static_cast<int>(std::floor(p.x));
        int iy = static_cast<int>(std::floor(p.y));

        if (!world.inBounds(ix, iy)) { p.done = true; continue; }

        Mat here = world.at(ix, iy);

        // Spark wand: stop on solid, spawn fire.
        if (p.tool == Tool::SparkWand) {
            if (here != Mat::Air && !materialInfo(here).flows && !materialInfo(here).rises) {
                // Spawn fire cluster.
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        int fx = ix + dx, fy = iy + dy;
                        if (!world.inBounds(fx, fy)) continue;
                        Mat m = world.at(fx, fy);
                        if (m == Mat::Air || materialInfo(m).flammable) {
                            world.set(fx, fy, Mat::Fire);
                            world.setLife(fx, fy, FIRE_LIFE);
                        }
                    }
                p.done = true;
            }
        } else {
            // Thrown objects trigger on landing (hitting solid or ground).
            bool hit = (here != Mat::Air && !materialInfo(here).flows && !materialInfo(here).rises);
            if (hit || p.life <= 0 || p.y >= world.height() - 2) {
                detonate(world, p);
                p.done = true;
            }
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
        // Explosion: clear cells in radius, spawn fire on rim.
        int radius = 6;
        int r2 = radius * radius;
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int x = cx - radius; x <= cx + radius; ++x) {
                if (!world.inBounds(x, y)) continue;
                int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy > r2) continue;
                Mat m = world.at(x, y);
                if (m == Mat::Bedrock) continue;
                if (materialInfo(m).blastResistance >= 200) continue;
                // Erase: set to air, but fling some material outward.
                world.set(x, y, Mat::Air);
            }
        }
        // Light the rim.
        for (int y = cy - radius - 1; y <= cy + radius + 1; ++y) {
            for (int x = cx - radius - 1; x <= cx + radius + 1; ++x) {
                if (!world.inBounds(x, y)) continue;
                int dx = x - cx, dy = y - cy;
                int d2 = dx * dx + dy * dy;
                if (d2 < r2 || d2 > (radius + 2) * (radius + 2)) continue;
                Mat m = world.at(x, y);
                if (m == Mat::Air || materialInfo(m).flammable) {
                    world.set(x, y, Mat::Fire);
                    world.setLife(x, y, FIRE_LIFE);
                }
            }
        }
    } else if (p.tool == Tool::AcidFlask) {
        // Splash acid in a pool.
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                int x = cx + dx, y = cy + dy;
                if (!world.inBounds(x, y)) continue;
                if (world.at(x, y) == Mat::Air) {
                    world.setMass(x, y, Mat::Acid, 0.8f);
                }
            }
        }
    }
}
