#include "generation.h"

#include <cmath>

namespace {

// Ground height for a given column, in cells from the top. A gentle hill so the
// scene is not a flat line.
int surfaceRow(int x, int worldHeight) {
    const int base = static_cast<int>(worldHeight * 0.72f);
    const int wave = static_cast<int>(7.0f * std::sin(static_cast<float>(x) * 0.02f));
    const int hill = static_cast<int>(4.0f * std::sin(static_cast<float>(x) * 0.005f));
    return base - wave - hill;
}

}  // namespace

void buildTestScene(World& world) {
    const int width = world.width();
    const int height = world.height();

    world.fill(Mat::Air);

    // Sky, then ground: packed dirt at the top, stone underneath, bedrock floor.
    for (int x = 0; x < width; ++x) {
        const int surface = surfaceRow(x, height);
        for (int y = surface; y < height; ++y) {
            Mat material = Mat::PackedDirt;
            if (y > surface + 3) material = Mat::Stone;
            if (y >= height - 2) material = Mat::Bedrock;
            world.set(x, y, material);
        }
    }

    // A column of sand resting on the ground, to show off the pixel scale.
    const int columnCenter = width / 2;
    const int columnBottom = surfaceRow(columnCenter, height) - 1;
    for (int y = columnBottom - 55; y <= columnBottom; ++y) {
        for (int x = columnCenter - 15; x <= columnCenter + 15; ++x) {
            world.set(x, y, Mat::Sand);
        }
    }

    // A small stone mound to break up the horizon.
    const int moundCenter = width / 5;
    const int moundBase = surfaceRow(moundCenter, height);
    for (int dx = -40; dx <= 40; ++dx) {
        const int rise = 40 - std::abs(dx);
        if (rise <= 0) continue;
        for (int dy = 0; dy < rise; ++dy) {
            world.set(moundCenter + dx, moundBase - dy, Mat::Stone);
        }
    }
}
