#include "tools.h"

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
            p.vx = dx / len * 1.2f;
            p.vy = dy / len * 1.2f;
            p.life = 60;
            p.tool = Tool::SparkWand;
            cooldown_ = 8;
            break;
        case Tool::Dynamite: {
            float speed = std::min(len * 0.05f, 1.2f);
            if (speed < 0.3f) speed = 0.3f;
            p.vx = dx / len * speed;
            p.vy = dy / len * speed - 0.3f;
            p.life = 120;
            p.tool = Tool::Dynamite;
            cooldown_ = 20;
            break;
        }
        case Tool::AcidFlask: {
            float speed = std::min(len * 0.05f, 1.0f);
            if (speed < 0.3f) speed = 0.3f;
            p.vx = dx / len * speed;
            p.vy = dy / len * speed - 0.2f;
            p.life = 120;
            p.tool = Tool::AcidFlask;
            cooldown_ = 20;
            break;
        }
    }
    projectiles_.push_back(p);
}

void Tools::tick(World& world, Simulation& sim) {
    if (cooldown_ > 0) --cooldown_;

    for (auto& p : projectiles_) {
        if (p.done) continue;

        // Gravity for thrown objects with air resistance.
        if (p.tool != Tool::SparkWand) {
            p.vy += 0.05f;
            p.vx *= 0.995f;
            p.vy *= 0.995f;
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
                // Spawn fire cluster with random size/shape.
                int r = 1 + (rand() % 3);
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
        bool underwater = (waterCount > total / 3);
        int effectiveRadius = underwater ? radius / 2 : radius;

        // Line of sight helper (Bresenham).
        auto hasLOS = [&](int x0, int y0, int x1, int y1) -> bool {
            int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
            int sx = (x0 < x1) ? 1 : -1;
            int sy = (y0 < y1) ? 1 : -1;
            int err = dx - dy;
            int x = x0, y = y0;
            while (true) {
                if (x == x1 && y == y1) return true;
                if (!(x == x0 && y == y0)) {
                    Mat m = world.at(x, y);
                    if (m != Mat::Air && !materialInfo(m).flows && !materialInfo(m).rises && m != Mat::Fire) {
                        return false;
                    }
                }
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x += sx; }
                if (e2 < dx) { err += dx; y += sy; }
                if (x < 0 || y < 0 || x >= world.width() || y >= world.height()) return false;
            }
        };

        // First pass: collect liquid moves.
        struct LiquidMove { int x, y, tx, ty; Mat m; float mass; };
        std::vector<LiquidMove> liquidMoves;
        for (int y = cy - effectiveRadius; y <= cy + effectiveRadius; ++y) {
            for (int x = cx - effectiveRadius; x <= cx + effectiveRadius; ++x) {
                if (!world.inBounds(x, y)) continue;
                int dx = x - cx, dy = y - cy;
                float dist = std::sqrt((float)(dx*dx + dy*dy));
                float angle = std::atan2((float)dy, (float)dx);
                float jitter = 0.85f + 0.3f * std::sin(angle * 3.0f + (float)(rand() % 100) / 100.0f * 6.28f);
                float effR = effectiveRadius * jitter;
                if (dist > effR) continue;
                if (!hasLOS(cx, cy, x, y)) continue; // blocked by wall
                Mat m = world.at(x, y);
                if (m == Mat::Bedrock) continue;
                bool isLiquid = materialInfo(m).flows || materialInfo(m).rises;
                if (!isLiquid) continue;
                float nx = (dist < 0.1f) ? 0 : (float)dx / dist;
                float ny = (dist < 0.1f) ? -1 : (float)dy / dist;
                // Find first air cell along outward ray beyond blast.
                int tx = x, ty = y;
                for (int step = 1; step <= effectiveRadius + 4; ++step) {
                    int nx2 = cx + (int)(nx * (dist + step));
                    int ny2 = cy + (int)(ny * (dist + step));
                    if (!world.inBounds(nx2, ny2)) break;
                    if (world.at(nx2, ny2) == Mat::Air) { tx = nx2; ty = ny2; break; }
                }
                if (tx == x && ty == y) continue; // no place to go
                float mass = world.mass(x, y);
                if (mass <= 0) mass = 1.0f;
                liquidMoves.push_back({x, y, tx, ty, m, mass});
            }
        }
        // Apply liquid moves.
        for (auto& lm : liquidMoves) {
            Mat targetMat = world.at(lm.tx, lm.ty);
            if (targetMat == Mat::Air) {
                world.setMass(lm.tx, lm.ty, lm.m, lm.mass);
            } else if (materialInfo(targetMat).flows || materialInfo(targetMat).rises) {
                world.setMass(lm.tx, lm.ty, targetMat, world.mass(lm.tx, lm.ty) + lm.mass);
            } else {
                continue; // blocked, don't destroy
            }
            world.set(lm.x, lm.y, Mat::Air);
        }
        // Destroy solids based on energy (with line of sight).
        for (int y = cy - effectiveRadius; y <= cy + effectiveRadius; ++y) {
            for (int x = cx - effectiveRadius; x <= cx + effectiveRadius; ++x) {
                if (!world.inBounds(x, y)) continue;
                int dx = x - cx, dy = y - cy;
                float dist = std::sqrt((float)(dx*dx + dy*dy));
                // Randomize shape: per-cell radius jitter.
                float angle = std::atan2((float)dy, (float)dx);
                float jitter = 0.85f + 0.3f * std::sin(angle * 3.0f + (float)(rand() % 100) / 100.0f * 6.28f);
                float effR = effectiveRadius * jitter;
                if (dist > effR) continue;
                if (!hasLOS(cx, cy, x, y)) continue; // blocked by wall
                Mat m = world.at(x, y);
                if (m == Mat::Bedrock) continue;
                if (materialInfo(m).flows || materialInfo(m).rises) continue; // liquids handled
                float energy = 1.0f - (dist / effR);
                if (energy < 0) energy = 0;
                int resist = materialInfo(m).blastResistance;
                if ((int)(energy * 255.0f) > resist) {
                    world.set(x, y, Mat::Air);
                }
            }
        }
        // Light the rim AND center (fire doesn't spread well underwater).
        if (!underwater) {
            // Center fire.
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    int x = cx + dx, y = cy + dy;
                    if (!world.inBounds(x, y)) continue;
                    Mat m = world.at(x, y);
                    if (m == Mat::Air || materialInfo(m).flammable) {
                        world.set(x, y, Mat::Fire);
                        world.setLife(x, y, FIRE_LIFE + (rand() % 4));
                    }
                }
            }
            // Rim fire.
            for (int y = cy - effectiveRadius - 1; y <= cy + effectiveRadius + 1; ++y) {
                for (int x = cx - effectiveRadius - 1; x <= cx + effectiveRadius + 1; ++x) {
                    if (!world.inBounds(x, y)) continue;
                    int dx = x - cx, dy = y - cy;
                    int d2 = dx * dx + dy * dy;
                    if (d2 < (effectiveRadius-2)*(effectiveRadius-2) || d2 > (effectiveRadius + 2) * (effectiveRadius + 2)) continue;
                    if (!hasLOS(cx, cy, x, y)) continue;
                    Mat m = world.at(x, y);
                    if (m == Mat::Air || materialInfo(m).flammable) {
                        world.set(x, y, Mat::Fire);
                        world.setLife(x, y, FIRE_LIFE + (rand() % 4));
                    }
                }
            }
            // Spawn smoke at the blast site.
            for (int i = 0; i < 20; ++i) {
                int rx = cx + (rand() % 9) - 4;
                int ry = cy - (rand() % 6);
                if (world.inBounds(rx, ry) && world.at(rx, ry) == Mat::Air) {
                    world.setMass(rx, ry, Mat::Smoke, 0.5f + (rand() % 50) / 100.0f);
                }
            }
        } else {
            // Underwater: spawn bubbles (smoke rising).
            for (int i = 0; i < 12; ++i) {
                int rx = cx + (rand() % 7) - 3;
                int ry = cy - (rand() % 5);
                if (world.inBounds(rx, ry) && world.at(rx, ry) == Mat::Air) {
                    world.setMass(rx, ry, Mat::Smoke, 0.4f + (rand() % 40) / 100.0f);
                }
            }
        }
    } else if (p.tool == Tool::AcidFlask) {    } else if (p.tool == Tool::AcidFlask) {
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

