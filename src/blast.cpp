#include "blast.h"

#include "materials.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

// The blast's outline is cut into this many wedges. Each wedge gets its own
// reach, so the hole is a lumpy shape rather than a perfect circle, and the
// flames are cut to exactly the same wedges so the fire always sits inside the
// hole it burned.
constexpr int kWedges = 24;

// Mixes two numbers into one, so the same input always gives the same output.
uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t h = a * 2654435761u ^ b * 2246822519u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

// How far the blast reaches in this direction, as a fraction of its radius.
//
// The wedge gives the lumpy outline. The small per-cell crumble roughens the
// edge so it does not look machine-cut. Both come out of a hash of the cell
// and the seed of this blast, which is why the hole and the fire that lights
// it always have the same shape: they ask the same question and get the same
// answer.
float outlineScale(int dx, int dy, uint32_t seed) {
    const float angle = std::atan2(static_cast<float>(dy), static_cast<float>(dx));
    int wedge = static_cast<int>((angle + 3.14159265f) / 6.28318531f * kWedges);
    if (wedge < 0) wedge = 0;
    if (wedge >= kWedges) wedge = kWedges - 1;

    const uint32_t w = mix(static_cast<uint32_t>(wedge), seed);
    const float wedgeReach = 0.75f + (w & 0xFFFFu) / 65535.0f * 0.40f;  // 0.75 .. 1.15

    const uint32_t c = mix(mix(static_cast<uint32_t>(dx + 512), static_cast<uint32_t>(dy + 512)), seed);
    const float crumble = (c & 0xFFFFu) / 65535.0f - 0.5f;               // -0.5 .. 0.5

    return wedgeReach + crumble * 0.10f;
}

// A liquid or a gas: the wave washes straight through it instead of being
// stopped by it.
bool isFluid(Mat m) {
    const MaterialInfo& info = materialInfo(m);
    return info.flows || info.rises;
}

// Bedrock is the floor of the world. Nothing breaks it, whatever the blast
// carries, so it is checked by name and not by its resistance.
bool isBedrock(Mat m) { return m == Mat::Bedrock; }

}  // namespace

void blast(World& world, int cx, int cy, const BlastOptions& options) {
    const int radius = options.underwater ? options.radius / 2 : options.radius;
    if (radius <= 0) return;

    // The blast carries this much energy, measured in the same units as a
    // block's blast resistance. Breaking a block costs that block's resistance,
    // so the energy says how much the blast can chew through.
    const float energy = options.power * 380.0f;
    const uint32_t seed = static_cast<uint32_t>(rand()) * 2654435761u;

    // The area the blast can possibly reach, as a square around the centre.
    const int span = radius + 2;
    const int side = span * 2 + 1;
    std::vector<uint8_t> willBreak(static_cast<size_t>(side) * side, 0);

    struct Broken {
        int x, y;
    };
    struct Shove {
        int x, y, tx, ty;
        Mat m;
        float mass;
    };
    struct Scorched {
        int x, y;
        float heat;
    };

    std::vector<Broken> broken;
    std::vector<Shove> shoves;
    std::vector<Scorched> scorched;

    // -- Step one: work out what the blast would do, without changing anything
    // yet. Every line of sight is measured against the world as it stands now,
    // so a wall this blast is going to break still costs the blast the energy
    // to break it. What is behind that wall is left with only the remainder.
    for (int dy = -span; dy <= span; ++dy) {
        for (int dx = -span; dx <= span; ++dx) {
            const int x = cx + dx;
            const int y = cy + dy;
            if (!world.inBounds(x, y)) continue;

            const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            const float reach = static_cast<float>(radius) * outlineScale(dx, dy, seed);
            if (dist > reach) continue;  // outside this blast's lumpy outline

            // Walk the line of sight from the centre out to this cell, breaking
            // the blocks in the way and spending their resistance as it goes.
            float spent = 0.0f;
            bool blocked = false;
            int px = cx;
            int py = cy;
            const int adx = std::abs(x - cx);
            const int ady = std::abs(y - cy);
            const int sx = (cx < x) ? 1 : -1;
            const int sy = (cy < y) ? 1 : -1;
            int err = adx - ady;
            while (true) {
                const int e2 = 2 * err;
                if (e2 > -ady) { err -= ady; px += sx; }
                if (e2 < adx) { err += adx; py += sy; }
                if (px == x && py == y) break;  // arrived at the target

                const Mat blocker = world.at(px, py);
                if (blocker == Mat::Air || blocker == Mat::Fire) continue;  // open space
                if (isFluid(blocker)) continue;  // the wave washes through water and gas

                const MaterialInfo& binfo = materialInfo(blocker);
                const float have = energy - spent;

                if (isBedrock(blocker)) {
                    // Bedrock never breaks, so the wave stops here.
                    blocked = true;
                    break;
                }

                if (have > static_cast<float>(binfo.blastResistance)) {
                    // Strong enough to break it: pay for it and carry on.
                    spent += static_cast<float>(binfo.blastResistance);
                    if (!willBreak[static_cast<size_t>(py - (cy - span)) * side + (px - (cx - span))]) {
                        willBreak[static_cast<size_t>(py - (cy - span)) * side + (px - (cx - span))] = 1;
                        broken.push_back({px, py});
                    }
                } else {
                    // Too tough. The wave stops here and everything behind it
                    // is shielded.
                    blocked = true;
                    break;
                }
            }
            if (blocked) continue;  // the wave never got here

            const float have = energy - spent;
            if (have <= 0.0f) continue;  // it got here with nothing left

            const Mat m = world.at(x, y);
            if (isFluid(m)) {
                // Liquids are not blown away, they are shoved aside. The mass
                // goes with them, so the amount of water does not change.
                if (options.pushLiquids) {
                    float nx = (dist < 0.1f) ? 0.0f : static_cast<float>(dx) / dist;
                    float ny = (dist < 0.1f) ? -1.0f : static_cast<float>(dy) / dist;
                    int tx = x;
                    int ty = y;
                    for (int step = 1; step <= radius + 4; ++step) {
                        const int nx2 = cx + static_cast<int>(nx * (dist + step));
                        const int ny2 = cy + static_cast<int>(ny * (dist + step));
                        if (!world.inBounds(nx2, ny2)) break;
                        const Mat m2 = world.at(nx2, ny2);
                        if (m2 == Mat::Air) { tx = nx2; ty = ny2; break; }
                        // A block in the way stops the shove, unless this blast is
                        // about to blow it out of the way anyway.
                        const size_t index =
                            static_cast<size_t>(ny2 - (cy - span)) * side + (nx2 - (cx - span));
                        if (!isFluid(m2) && m2 != Mat::Fire && index < willBreak.size() &&
                            !willBreak[index]) {
                            break;
                        }
                    }
                    if (tx != x || ty != y) {
                        float mass = world.mass(x, y);
                        if (mass <= 0.0f) mass = 1.0f;
                        shoves.push_back({x, y, tx, ty, m, mass});
                    }
                }
            } else if (m != Mat::Air) {
                const MaterialInfo& info = materialInfo(m);
                if (!isBedrock(m) && have > static_cast<float>(info.blastResistance)) {
                    const size_t index =
                        static_cast<size_t>(y - (cy - span)) * side + (x - (cx - span));
                    if (!willBreak[index]) {
                        willBreak[index] = 1;
                        broken.push_back({x, y});
                    }
                }
            }

            // The flame is hottest at the centre and fades towards the rim, and
            // it is fainter where the blast had to spend its energy on the way.
            float heat = (1.0f - dist / reach) * (have / energy);
            if (heat < 0.0f) heat = 0.0f;
            scorched.push_back({x, y, heat});
        }
    }

    // -- Step two: put it into effect.
    //
    // Liquids move first, so a shove lands on the world as it was before the
    // blast punched any holes in it.
    for (const Shove& s : shoves) {
        const Mat targetMat = world.at(s.tx, s.ty);
        if (targetMat == Mat::Air) {
            world.setMass(s.tx, s.ty, s.m, s.mass);
        } else if (isFluid(targetMat)) {
            world.setMass(s.tx, s.ty, targetMat, world.mass(s.tx, s.ty) + s.mass);
        } else {
            continue;  // blocked, so leave the liquid where it is
        }
        world.setMass(s.x, s.y, Mat::Air, 0.0f);
    }

    for (const Broken& b : broken) {
        world.set(b.x, b.y, Mat::Air);
    }

    // -- Step three: light it up. The flame is hottest at the centre and fades
    // towards the rim, and it does not reach past a wall the blast could not
    // break through.
    if (options.flames) {
        for (const Scorched& s : scorched) {
            const Mat m = world.at(s.x, s.y);
            if (m != Mat::Air && !materialInfo(m).flammable) continue;
            float strength = s.heat;
            if (strength < 0.1f) strength = 0.1f;
            world.set(s.x, s.y, Mat::Fire);
            world.setLife(s.x, s.y, static_cast<uint8_t>(FIRE_LIFE * strength * 2.0f));
        }
    }

    // -- Step four: a puff of smoke above the blast.
    const int spread = std::max(2, radius / 3);
    for (int i = 0; i < options.smokePuffs; ++i) {
        const int rx = cx + (rand() % (spread * 2 + 1)) - spread;
        const int ry = cy - (rand() % (spread + 2));
        if (world.inBounds(rx, ry) && world.at(rx, ry) == Mat::Air) {
            world.setMass(rx, ry, Mat::Smoke, 0.4f + (rand() % 60) / 100.0f);
        }
    }
}
