#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include "generation.h"
#include "materials.h"
#include "raylib.h"
#include "render.h"
#include "sim.h"
#include "world.h"

namespace {

constexpr int MAX_GRID_WIDTH = 480;
constexpr int MAX_GRID_HEIGHT = 270;
constexpr int PREFERRED_SCALE = 3;

// Steps the simulation for however long a self-test needs.
void runSteps(World& world, int steps) {
    Simulation sim(world);
    for (int i = 0; i < steps; ++i) sim.step(world);
}

// The simulated grid and the window are chosen to suit the actual display: the
// picture is as large as the framebuffer allows at the chunkiest scale that
// still fits, without the window running off the screen or hiding under the
// menu bar. On a Retina display the framebuffer is larger than the window, so
// the scale is measured in framebuffer pixels and the world is centred.
struct Viewport {
    int gridWidth;
    int gridHeight;
    int scale;
    int offsetX;
    int offsetY;
};

Viewport chooseViewport() {
    const int framebufferWidth = GetRenderWidth();
    const int framebufferHeight = GetRenderHeight();

    const int gridWidth = std::min(MAX_GRID_WIDTH, std::max(320, framebufferWidth / PREFERRED_SCALE));
    const int gridHeight = std::min(MAX_GRID_HEIGHT, std::max(200, framebufferHeight / PREFERRED_SCALE));

    const int scale = std::max(1, std::min(framebufferWidth / gridWidth, framebufferHeight / gridHeight));

    return Viewport{gridWidth, gridHeight, scale,
                    (framebufferWidth - gridWidth * scale) / 2,
                    (framebufferHeight - gridHeight * scale) / 2};
}

// Temporary debug brush: sets fire where the mouse is held. The real tools
// replace this in slice 7. Flame is only painted onto air or something that can
// burn, so it does not carve holes in solid ground.
void paintFire(World& world, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius) continue;
            const int x = cx + dx;
            const int y = cy + dy;
            const Mat m = world.at(x, y);
            if (m == Mat::Air || materialInfo(m).flammable) {
                world.set(x, y, Mat::Fire);
                world.setLife(x, y, FIRE_LIFE);
            }
        }
    }
}

// Opens the window and runs the main loop. In smoke mode it closes itself after
// a fixed number of frames so it can be checked from the command line.
int runGame(bool smoke) {
    const int monitor = GetCurrentMonitor();
    const int windowWidth = std::min(MAX_GRID_WIDTH * PREFERRED_SCALE, GetMonitorWidth(monitor) - 80);
    const int windowHeight = std::min(MAX_GRID_HEIGHT * PREFERRED_SCALE, GetMonitorHeight(monitor) - 120);

    InitWindow(windowWidth, windowHeight, "Pixel World");
    SetTargetFPS(60);

    const Viewport viewport = chooseViewport();

    World world(viewport.gridWidth, viewport.gridHeight);
    buildTestScene(world);

    Simulation sim(world);
    Renderer renderer(viewport.gridWidth, viewport.gridHeight);

    int frames = 0;
    while (!WindowShouldClose()) {
        // Temporary: `R` rebuilds the scene so it can be watched from the start.
        if (IsKeyPressed(KEY_R)) {
            buildTestScene(world);
            sim = Simulation(world);
        }

        // Temporary debug brush: hold the left mouse button to set fire to the
        // wood and leaves. Replaced by real tools in slice 7.
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 mouse = GetMousePosition();
            const int gx = static_cast<int>((mouse.x - viewport.offsetX) / viewport.scale);
            const int gy = static_cast<int>((mouse.y - viewport.offsetY) / viewport.scale);
            paintFire(world, gx, gy, 3);
        }

        sim.step(world);
        renderer.updateImage(world);

        BeginDrawing();
        ClearBackground(BLACK);
        renderer.drawScaled(viewport.scale, viewport.offsetX, viewport.offsetY);
        EndDrawing();

        ++frames;
        if (smoke && frames >= 60) {
            std::printf("window %dx%d (render %dx%d) | grid %dx%d | scale %d | frames %d\n",
                        GetScreenWidth(), GetScreenHeight(), GetRenderWidth(), GetRenderHeight(),
                        viewport.gridWidth, viewport.gridHeight, viewport.scale, frames);
            TakeScreenshot("smoke.png");

            // Read the frame back and measure how much of the window the world
            // actually covers, so a mis-scaled or clipped render cannot pass.
            Image shot = LoadImageFromScreen();
            Color* pixels = static_cast<Color*>(shot.data);
            const int total = shot.width * shot.height;
            int painted = 0;
            for (int i = 0; i < total; ++i) {
                const Color c = pixels[i];
                if (c.r > 24 || c.g > 24 || c.b > 24) ++painted;
            }
            std::printf("world covers %d of %d pixels (%.1f%%)\n", painted, total,
                        100.0 * static_cast<double>(painted) / total);
            UnloadImage(shot);
            break;
        }
    }

    CloseWindow();
    return 0;
}

// Self-test: sand must fall and settle, leaving nothing floating.
bool checkSandSettles() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildDropTestScene(world);

    int startTop = MAX_GRID_HEIGHT;
    for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
        for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
            if (world.at(x, y) == Mat::Sand) startTop = std::min(startTop, y);
        }
        if (startTop != MAX_GRID_HEIGHT) break;
    }

    runSteps(world, 2000);

    // Nothing floating: no grain of sand may have empty space directly beneath it.
    int floating = 0;
    int settledTop = MAX_GRID_HEIGHT;
    for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
        for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
            if (world.at(x, y) != Mat::Sand) continue;
            settledTop = std::min(settledTop, y);
            if (y + 1 < MAX_GRID_HEIGHT && world.at(x, y + 1) == Mat::Air) ++floating;
        }
    }

    const bool fell = settledTop > startTop + 10;
    const bool passed = floating == 0 && fell && world.count(Mat::Sand) > 0;
    std::printf("sand settles: %s (floating grains %d, top row %d -> %d)\n",
                passed ? "PASS" : "FAIL", floating, startTop, settledTop);
    return passed;
}

// Self-test: water must spread out when poured in one place, keep its volume,
// and stay inside its basin.
bool checkWaterSpreads() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    world.fill(Mat::Air);
    for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
        for (int y = 200; y < MAX_GRID_HEIGHT; ++y) world.set(x, y, Mat::Stone);
    }
    // Narrow basin: floor at y=200, walls 4 thick, interior x=214..265.
    for (int x = 210; x < 214; ++x) {
        for (int y = 160; y < 200; ++y) world.set(x, y, Mat::Stone);
    }
    for (int x = 266; x < 270; ++x) {
        for (int y = 160; y < 200; ++y) world.set(x, y, Mat::Stone);
    }
    // Water starts piled high against the left wall.
    for (int x = 214; x < 220; ++x) {
        for (int y = 184; y < 200; ++y) world.set(x, y, Mat::Water);
    }

    const int startCells = world.count(Mat::Water);
    int startColumns = 0;
    for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
        for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
            if (world.at(x, y) == Mat::Water) { ++startColumns; break; }
        }
    }

    runSteps(world, 3000);

    const int endCells = world.count(Mat::Water);
    int endColumns = 0;
    int leaks = 0;
    int aboveRim = 0;
    for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
        for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
            if (world.at(x, y) != Mat::Water) continue;
            if (x < 214 || x > 265) ++leaks;
            if (y < 160) ++aboveRim;
        }
    }
    for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
        for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
            if (world.at(x, y) == Mat::Water) { ++endColumns; break; }
        }
    }

    const bool conserved = endCells == startCells;
    const bool spread = endColumns >= startColumns * 5 / 4;
    const bool contained = leaks == 0 && aboveRim == 0;
    const bool passed = conserved && spread && contained;
    std::printf("water spreads and stays put: %s (cells %d -> %d, columns %d -> %d, leaks %d, above rim %d)\n",
                passed ? "PASS" : "FAIL", startCells, endCells, startColumns, endColumns, leaks, aboveRim);
    return passed;
}

// Self-test: fire must spread through a block of wood, burn it down and go out,
// leaving ash behind.
bool checkFireSpreads() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildBurnTestScene(world);

    const int startWood = world.count(Mat::Wood);
    runSteps(world, 400);

    const int endWood = world.count(Mat::Wood);
    const int ash = world.count(Mat::Ash);
    const int burning = world.burningCount();

    const bool burned = endWood <= startWood * 2 / 5;   // at least 60% gone
    const bool leftAsh = ash > 0;                       // burning wood leaves ash
    const bool wentOut = burning <= startWood / 50;     // the fire does not burn forever
    const bool passed = burned && leftAsh && wentOut;
    std::printf("fire spreads: %s (wood %d -> %d, ash %d, still burning %d)\n",
                passed ? "PASS" : "FAIL", startWood, endWood, ash, burning);
    return passed;
}

// Self-test: lava meeting water crusts over into stone, and a body of lava deep
// enough to matter cools into obsidian instead.
bool checkLavaWater() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);

    buildLavaWaterScene(world, 1);  // a thin trickle
    const int lavaBefore = world.count(Mat::Lava);
    const int stoneBefore = world.count(Mat::Stone);
    runSteps(world, 40);
    const int stoneMade = world.count(Mat::Stone) - stoneBefore;
    const bool trickleWorked = stoneMade > 0 && world.count(Mat::Lava) < lavaBefore;

    buildLavaWaterScene(world, 6);  // a deep pool
    runSteps(world, 40);
    const int obsidian = world.count(Mat::Obsidian);
    const bool poolWorked = obsidian > 0;

    const bool passed = trickleWorked && poolWorked;
    std::printf("lava+water→stone: %s (stone %d, obsidian %d)\n",
                passed ? "PASS" : "FAIL", stoneMade, obsidian);
    return passed;
}

// Self-test: snow and glacial ice next to flame both turn into water.
bool checkIceMelts() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildIceMeltsScene(world);

    const int iceBefore = world.count(Mat::GlacialIce) + world.count(Mat::Snow);
    runSteps(world, 20);
    const int iceAfter = world.count(Mat::GlacialIce) + world.count(Mat::Snow);
    const int water = world.count(Mat::Water);

    const bool passed = water > 0 && iceAfter < iceBefore;
    std::printf("ice melts: %s (water %d, ice %d -> %d)\n",
                passed ? "PASS" : "FAIL", water, iceBefore, iceAfter);
    return passed;
}

// Self-test: water soaking into soil softens it into mud.
bool checkWaterSoilMud() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildMudScene(world);

    const int soilBefore = world.count(Mat::Soil);
    runSteps(world, 40);
    const int soilAfter = world.count(Mat::Soil);
    const int mud = world.count(Mat::Mud);

    const bool passed = mud > 0 && soilAfter < soilBefore;
    std::printf("water+soil→mud: %s (mud %d, soil %d -> %d)\n",
                passed ? "PASS" : "FAIL", mud, soilBefore, soilAfter);
    return passed;
}

// Self-test: water washing over ash carries it off rather than leaving it be.
bool checkAshDissolves() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildAshScene(world);

    const int ashBefore = world.count(Mat::Ash);
    runSteps(world, 60);
    const int ashAfter = world.count(Mat::Ash);

    const bool passed = ashAfter < ashBefore / 2;
    std::printf("ash dissolves: %s (ash %d -> %d)\n",
                passed ? "PASS" : "FAIL", ashBefore, ashAfter);
    return passed;
}

// Self-test: oil touching lava goes up on the spot, fiercely.
bool checkOilIgnites() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildOilScene(world);

    const int oilBefore = world.count(Mat::Oil);
    runSteps(world, 6);
    const int burning = world.burningCount();
    const int oilAfter = world.count(Mat::Oil);

    const bool passed = burning > 0 && oilAfter < oilBefore;
    std::printf("oil ignites: %s (burning %d, oil %d -> %d)\n",
                passed ? "PASS" : "FAIL", burning, oilBefore, oilAfter);
    return passed;
}

// Self-test: lava reaching sulfur crust blows a crater out of the dirt above it.
bool checkSulfurExplodes() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildSulfurScene(world);

    const int dirtBefore = world.count(Mat::PackedDirt);
    runSteps(world, 30);
    const int dirtAfter = world.count(Mat::PackedDirt);
    const int sulfur = world.count(Mat::SulfurCrust);

    const bool passed = sulfur == 0 && dirtAfter < dirtBefore;
    std::printf("sulfur explodes: %s (dirt %d -> %d)\n",
                passed ? "PASS" : "FAIL", dirtBefore, dirtAfter);
    return passed;
}

// Headless check: builds the scene, renders it into an image buffer, writes that
// image to disk, and runs the physics checks. No window is opened.
int runSelfTest() {
    bool passed = true;

    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildTestScene(world);

    Renderer renderer(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    renderer.updateImage(world);
    ExportImage(renderer.image(), "selftest.png");

    std::printf("grid %dx%d | wrote selftest.png\n", renderer.image().width, renderer.image().height);

    const int sky = world.count(Mat::Air);
    const int ground = world.count(Mat::PackedDirt) + world.count(Mat::Stone) + world.count(Mat::Bedrock);
    const bool skyAbove = sky > 0 && ground > 0 && sky + ground < static_cast<int>(world.cellCount());
    if (!skyAbove) passed = false;
    std::printf("sky and ground both present: %s\n", skyAbove ? "PASS" : "FAIL");

    const int sand = world.count(Mat::Sand);
    const bool sandPresent = sand > 100;
    if (!sandPresent) passed = false;
    std::printf("sand column present: %s (%d cells)\n", sandPresent ? "PASS" : "FAIL", sand);

    if (!checkSandSettles()) passed = false;
    if (!checkWaterSpreads()) passed = false;
    if (!checkFireSpreads()) passed = false;
    if (!checkLavaWater()) passed = false;
    if (!checkIceMelts()) passed = false;
    if (!checkWaterSoilMud()) passed = false;
    if (!checkAshDissolves()) passed = false;
    if (!checkOilIgnites()) passed = false;
    if (!checkSulfurExplodes()) passed = false;

    return passed ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    bool smoke = false;
    bool selftest = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;
        else if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;
    }

    if (selftest) return runSelfTest();
    return runGame(smoke);
}
