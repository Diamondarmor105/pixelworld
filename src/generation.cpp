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

// Ground: packed dirt at the top, stone underneath, bedrock floor.
void buildGround(World& world) {
    const int width = world.width();
    const int height = world.height();

    for (int x = 0; x < width; ++x) {
        const int surface = surfaceRow(x, height);
        for (int y = surface; y < height; ++y) {
            Mat material = Mat::PackedDirt;
            if (y > surface + 3) material = Mat::Stone;
            if (y >= height - 2) material = Mat::Bedrock;
            world.set(x, y, material);
        }
    }
}

}  // namespace

void buildTestScene(World& world) {
    const int width = world.width();
    const int height = world.height();

    world.fill(Mat::Air);
    buildGround(world);

    // A column of sand resting on the ground. It collapses as soon as the
    // simulation runs, which is the point.
    const int columnCenter = width / 2;
    const int columnBottom = surfaceRow(columnCenter, height) - 1;
    for (int y = columnBottom - 50; y <= columnBottom; ++y) {
        for (int x = columnCenter - 14; x <= columnCenter + 14; ++x) {
            world.set(x, y, Mat::Sand);
        }
    }

    // A gravel heap on the left: also falls and settles into a slope.
    const int heapCenter = width / 6;
    const int heapBase = surfaceRow(heapCenter, height);
    for (int dx = -30; dx <= 30; ++dx) {
        const int rise = 30 - std::abs(dx);
        for (int dy = 0; dy < rise; ++dy) {
            world.set(heapCenter + dx, heapBase - dy, Mat::Gravel);
        }
    }

    // A bank of soil on the right third, to show a looser, darker ground.
    const int bankCenter = width * 3 / 5;
    const int bankBase = surfaceRow(bankCenter, height);
    for (int dx = -25; dx <= 25; ++dx) {
        const int rise = 18 - std::abs(dx) / 2;
        for (int dy = 0; dy < rise; ++dy) {
            world.set(bankCenter + dx, bankBase - dy, Mat::Soil);
        }
    }

    // A pool: hollow out a basin in the ground and fill it halfway with water.
    const int poolCenter = width * 3 / 4;
    const int poolHalf = 42;
    for (int dx = -poolHalf; dx < poolHalf; ++dx) {
        const int x = poolCenter + dx;
        const int surface = surfaceRow(x, height);
        const int depth = 10 + (poolHalf - std::abs(dx)) / 3;
        for (int y = surface; y < surface + depth; ++y) {
            world.set(x, y, Mat::Air);
        }
    }
    for (int dx = -poolHalf; dx < poolHalf; ++dx) {
        const int x = poolCenter + dx;
        const int surface = surfaceRow(x, height);
        for (int y = surface; y < surface + 7; ++y) {
            world.set(x, y, Mat::Water);
        }
    }
}

void buildDropTestScene(World& world) {
    const int width = world.width();
    const int height = world.height();

    world.fill(Mat::Air);

    // Stone floor across the bottom.
    for (int x = 0; x < width; ++x) {
        for (int y = height - 4; y < height; ++y) {
            world.set(x, y, Mat::Stone);
        }
    }

    // A block of sand hanging well above the floor.
    const int cx = width / 2;
    const int bottom = height - 60;
    for (int y = bottom - 24; y <= bottom; ++y) {
        for (int x = cx - 12; x <= cx + 12; ++x) {
            world.set(x, y, Mat::Sand);
        }
    }
}

void buildPoolTestScene(World& world) {
    const int width = world.width();
    const int height = world.height();

    world.fill(Mat::Air);

    const int floorTop = height - 60;

    // Stone floor and two walls, making a wide basin.
    for (int x = 0; x < width; ++x) {
        for (int y = floorTop; y < height; ++y) {
            world.set(x, y, Mat::Stone);
        }
    }
    for (int x = 60; x < 66; ++x) {
        for (int y = floorTop - 40; y < floorTop; ++y) {
            world.set(x, y, Mat::Stone);
        }
    }
    for (int x = width - 66; x < width - 60; ++x) {
        for (int y = floorTop - 40; y < floorTop; ++y) {
            world.set(x, y, Mat::Stone);
        }
    }

    // Water starts as a tall column on the left of the basin, and has to spread
    // out and find its own level.
    for (int x = 80; x < 130; ++x) {
        for (int y = floorTop - 38; y < floorTop; ++y) {
            world.set(x, y, Mat::Water);
        }
    }
}
