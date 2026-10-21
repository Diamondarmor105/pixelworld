#include "tools.h"

#include "blast.h"
#include "materials.h"
#include "sim.h"

#include <cmath>
#include <tuple>

namespace {

// Gravity on a thrown tool, in cells per tick squared.
constexpr float THROW_GRAVITY = 0.05f;

// How much of the speed the air takes off, every tick.
constexpr float THROW_DRAG = 0.995f;

// How a thrown tool is aimed.
//
// The player clicks where they want the tool to land, so the throw has to
// arrive there - otherwise clicking on a tree drops the dynamite halfway to
// it. The flight time grows with the distance (a short toss is quick, a long
// one lobs), and the starting speed is worked out from that flight time, so
// the tool lands where it was aimed.
//
// Two things bend the flight: gravity pulls the tool down the whole way, and
// air resistance takes a little off the speed every tick. Both are worked out
// here, in advance, so the answer is where the player pointed.
void aimThrow(float sx, float sy, float tx, float ty,
              float& vx, float& vy,
              float cellsPerTick, float minFlight, float maxFlight) {
    const float dx = tx - sx;
    const float dy = ty - sy;
    const float len = std::sqrt(dx * dx + dy * dy);

    // How long the tool is in the air.
    float flight = len / cellsPerTick;
    if (flight < minFlight) flight = minFlight;
    if (flight > maxFlight) flight = maxFlight;

    // How far a speed of one travels in that time, once air resistance has
    // had its say. Sideways that is simply the sum of the shrinking speeds.
    const float reach = (1.0f - std::pow(THROW_DRAG, flight)) / (1.0f - THROW_DRAG);

    // Gravity on its own would pull the tool down at this speed by the end of
    // the flight, so the throw only has to make up the difference. Gravity is
    // applied before the air gets its say, so the sideways sum has one more
    // factor of drag in it than the upward one.
    const float fall = THROW_GRAVITY * THROW_DRAG / (1.0f - THROW_DRAG);

    vx = dx / reach;
    vy = fall + (dy - fall * flight) / (THROW_DRAG * reach);
}

}  // namespace

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
            aimThrow(sx, sy, tx, ty, p.vx, p.vy, 5.0f, 18.0f, 70.0f);
            p.life = 150;
            p.tool = Tool::Dynamite;
            cooldown_ = 20;
            break;
        }
        case Tool::AcidFlask: {
            aimThrow(sx, sy, tx, ty, p.vx, p.vy, 4.0f, 20.0f, 80.0f);
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

    // Each tick starts with a clean sheet: whatever exploded on this tick is
    // reported, and yesterday's is forgotten.
    blastEvents_.clear();

    for (auto& p : projectiles_) {
        if (p.done) continue;

        // Gravity for thrown objects with air resistance.
        if (p.tool != Tool::SparkWand) {
            p.vy += THROW_GRAVITY;
            p.vx *= THROW_DRAG;
            p.vy *= THROW_DRAG;
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
        blastEvents_.push_back(BlastEvent{cx, cy, radius});
    } else if (p.tool == Tool::AcidFlask) {
        // The flask shatters. Its acid ends up in the air around the impact -
        // the visible splash - and in the cell it actually struck, so that it
        // can work its way into the material around it. The acid is used up as
        // it eats, so a splash carves a hole and then is gone.
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -5; dx <= 5; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (!world.inBounds(x, y)) continue;
                const Mat here = world.at(x, y);
                const bool isImpact = (dx == 0 && dy == 0);
                if (here != Mat::Air) {
                    // Only the cell the flask struck is broken into. Everything
                    // else has to be air for the acid to splash into, so that
                    // a splash never eats a hole on its own.
                    if (!isImpact) continue;
                    if (materialInfo(here).flows) continue;
                }
                world.setMass(x, y, Mat::Acid, 0.5f + (rand() % 50) / 100.0f);
            }
        }
    }
}

