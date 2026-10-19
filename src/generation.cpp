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

// A simple tree: a trunk of wood with bark on its edges, and a rounded canopy of
// foliage. `groundY` is the surface row the trunk stands on.
void buildTree(World& world, int trunkX, int groundY, int trunkHeight, int canopyRadius) {
    const int trunkTop = groundY - trunkHeight;
    for (int y = trunkTop; y < groundY; ++y) {
        for (int dx = 0; dx < 4; ++dx) {
            const bool edge = (dx == 0 || dx == 3);
            world.set(trunkX + dx, y, edge ? Mat::Bark : Mat::Wood);
        }
    }

    const int canopyX = trunkX + 1;
    const int canopyY = trunkTop - canopyRadius / 3;
    for (int dy = -canopyRadius; dy <= canopyRadius / 2; ++dy) {
        for (int dx = -canopyRadius; dx <= canopyRadius; ++dx) {
            const float nx = static_cast<float>(dx) / static_cast<float>(canopyRadius);
            const float ny = static_cast<float>(dy) / (static_cast<float>(canopyRadius) * 0.75f);
            if (nx * nx + ny * ny <= 1.0f) world.set(canopyX + dx, canopyY + dy, Mat::Foliage);
        }
    }
}

// A fallen log lying on the ground, so there is loose wood to set alight.
void buildLog(World& world, int startX, int endX, int groundY) {
    for (int x = startX; x < endX; ++x) {
        for (int dy = 0; dy < 3; ++dy) {
            const bool edge = (dy == 0 || dy == 2);
            world.set(x, groundY - 1 - dy, edge ? Mat::Bark : Mat::Wood);
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

    // An oil slick floating on the left of the pool. Oil is lighter than water,
    // so the simulation sorts the two out and the oil rises to the surface.
    for (int x = poolCenter - 26; x < poolCenter - 6; ++x) {
        const int surface = surfaceRow(x, height);
        for (int y = surface; y < surface + 3; ++y) {
            world.set(x, y, Mat::Oil);
        }
    }

    // Two trees and a fallen log, to burn.
    buildTree(world, width * 42 / 100, surfaceRow(width * 42 / 100, height), 34, 14);
    buildTree(world, width * 90 / 100, surfaceRow(width * 90 / 100, height), 26, 11);
    buildLog(world, width * 84 / 100, width * 84 / 100 + 18, surfaceRow(width * 84 / 100, height));
}

void buildBurnTestScene(World& world) {
    const int width = world.width();
    const int height = world.height();

    world.fill(Mat::Air);

    // Stone floor across the bottom.
    for (int x = 0; x < width; ++x) {
        for (int y = height - 6; y < height; ++y) {
            world.set(x, y, Mat::Stone);
        }
    }

    // A standing block of wood, lit at the bottom centre.
    const int cx = width / 2;
    const int bottom = height - 7;
    for (int y = bottom - 20; y <= bottom; ++y) {
        for (int x = cx - 15; x <= cx + 15; ++x) {
            world.set(x, y, Mat::Wood);
        }
    }
    world.setLife(cx, bottom, materialInfo(Mat::Wood).burnTime);
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
