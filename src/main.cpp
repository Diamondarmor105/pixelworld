#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "blast.h"
#include "camera.h"
#include "generation.h"
#include "materials.h"
#include "player.h"
#include "raylib.h"
#include "render.h"
#include "sim.h"
#include "tools.h"
#include "world.h"

namespace {

constexpr int MAX_GRID_WIDTH = 480;
constexpr int MAX_GRID_HEIGHT = 270;
constexpr int PREFERRED_SCALE = 3;

// The world is larger than the screen; the view scrolls over it.
constexpr int WORLD_WIDTH_SCREENS = 3;
constexpr int WORLD_HEIGHT_MARGIN = 48;

// How far beyond the edge of the screen the simulation still runs, so material
// arriving from off-screen has already settled by the time you see it.
constexpr int SIM_MARGIN = 16;

// Where the player starts: on the ground, near the left end of the level.
constexpr float PLAYER_START_X = 100.0f;

// Steps the simulation for however long a self-test needs.
void runSteps(World& world, int steps) {
    Simulation sim(world);
    for (int i = 0; i < steps; ++i) sim.step(world);
}

// Drops the player onto whatever is underneath them at `x`, so they start
// standing on the ground rather than falling out of the sky.
float groundBelow(const World& world, int x) {
    for (int y = 0; y < world.height(); ++y) {
        const Mat m = world.at(x, y);
        if (m == Mat::Air || m == Mat::Fire || m == Mat::Foliage ||
            materialInfo(m).flows) continue;
        return static_cast<float>(y - Player::HEIGHT);
    }
    return 0.0f;
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

// Opens the window and runs the main loop. In smoke mode it closes itself after
// a fixed number of frames so it can be checked from the command line.
int runGame(bool smoke) {
    const int monitor = GetCurrentMonitor();
    const int windowWidth = std::min(MAX_GRID_WIDTH * PREFERRED_SCALE, GetMonitorWidth(monitor) - 80);
    const int windowHeight = std::min(MAX_GRID_HEIGHT * PREFERRED_SCALE, GetMonitorHeight(monitor) - 120);

    InitWindow(windowWidth, windowHeight, "Pixel World");
    SetTargetFPS(60);

    const Viewport viewport = chooseViewport();

    // The world is bigger than the screen: the view is a window into it. It is
    // three screens wide, and a little taller than the view so there is
    // somewhere to descend to and the camera has something to clamp against.
    const int worldWidth = viewport.gridWidth * WORLD_WIDTH_SCREENS;
    const int worldHeight = viewport.gridHeight + WORLD_HEIGHT_MARGIN;

    World world(worldWidth, worldHeight);
    buildTestScene(world);

    Simulation sim(world);
    Renderer renderer(worldWidth, worldHeight);

    GameCamera camera(viewport.gridWidth, viewport.gridHeight, PLAYER_START_X, 0.0f);
    Player player(PLAYER_START_X, groundBelow(world, static_cast<int>(PLAYER_START_X)));
    camera.follow(player.x() + Player::WIDTH * 0.5f, player.y() + Player::HEIGHT * 0.5f);
    camera.clamp(world.width(), world.height());

    Tools tools;

    int frames = 0;
    while (!WindowShouldClose()) {
        // Temporary: `R` rebuilds the scene and puts the player back, so it can
        // be watched from the start.
        if (IsKeyPressed(KEY_R)) {
            buildTestScene(world);
            sim = Simulation(world);
            player = Player(PLAYER_START_X, groundBelow(world, static_cast<int>(PLAYER_START_X)));
        }

        // Tool selection.
        if (IsKeyPressed(KEY_ONE)) tools.setTool(Tool::SparkWand);
        if (IsKeyPressed(KEY_TWO)) tools.setTool(Tool::Dynamite);
        if (IsKeyPressed(KEY_THREE)) tools.setTool(Tool::AcidFlask);

        float moveInput = 0.0f;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) moveInput += 1.0f;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) moveInput -= 1.0f;
        const bool jump = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP);

        player.tick(world, moveInput, jump);
        camera.follow(player.x() + Player::WIDTH * 0.5f, player.y() + Player::HEIGHT * 0.5f);
        camera.clamp(world.width(), world.height());

        // Fire tool on left click.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const Vector2 mouse = GetMousePosition();
            const float gx = camera.viewX() +
                           static_cast<float>((mouse.x - viewport.offsetX) / viewport.scale);
            const float gy = camera.viewY() +
                           static_cast<float>((mouse.y - viewport.offsetY) / viewport.scale);
            tools.fire(player.x() + Player::WIDTH * 0.5f, player.y() + Player::HEIGHT * 0.5f, gx, gy);
        }

        tools.tick(world);

        // Only the pixels near the player get simulated. Everything else waits,
        // exactly as it was, until it comes into view.
        const SimRect region{
            std::max(0, camera.viewX() - SIM_MARGIN),
            std::max(0, camera.viewY() - SIM_MARGIN),
            std::min(world.width() - 1, camera.viewX() + viewport.gridWidth - 1 + SIM_MARGIN),
            std::min(world.height() - 1, camera.viewY() + viewport.gridHeight - 1 + SIM_MARGIN)};
        sim.step(world, region);

        renderer.updateImage(world);

        const float offsetX = static_cast<float>(viewport.offsetX) -
                              static_cast<float>(camera.viewX() * viewport.scale);
        const float offsetY = static_cast<float>(viewport.offsetY) -
                              static_cast<float>(camera.viewY() * viewport.scale);

        BeginDrawing();
        ClearBackground(BLACK);
        renderer.drawView(camera.viewX(), camera.viewY(), viewport.gridWidth, viewport.gridHeight,
                          viewport.scale, viewport.offsetX, viewport.offsetY);
        player.draw(offsetX, offsetY, viewport.scale);
        // Draw projectiles.
        for (const auto& p : tools.projectiles()) {
            const float sx = (p.x - camera.viewX()) * viewport.scale + viewport.offsetX;
            const float sy = (p.y - camera.viewY()) * viewport.scale + viewport.offsetY;
            Color c = (p.tool == Tool::SparkWand) ? YELLOW :
                      (p.tool == Tool::Dynamite) ? RED : GREEN;
            DrawRectangle((int)sx - 2, (int)sy - 2, 4, 4, c);
        }
        // HUD: tool name.
        DrawText(Tools::name(tools.selected()), 10, 10, 20, WHITE);
        EndDrawing();

        ++frames;
        if (smoke && frames >= 60) {
            std::printf("window %dx%d (render %dx%d) | view %dx%d | world %dx%d | scale %d | frames %d\n",
                        GetScreenWidth(), GetScreenHeight(), GetRenderWidth(), GetRenderHeight(),
                        viewport.gridWidth, viewport.gridHeight, worldWidth, worldHeight,
                        viewport.scale, frames);
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

    const float startMass = world.totalMass(Mat::Water);
    const int startCells = world.count(Mat::Water);
    int startColumns = 0;
    for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
        for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
            if (world.mass(x, y) >= 0.5f) { ++startColumns; break; }
        }
    }

    runSteps(world, 3000);

    const float endMass = world.totalMass(Mat::Water);
    const int endCells = world.cellsWithMass(Mat::Water, 0.5f);
    int endColumns = 0;
    int leaks = 0;
    int aboveRim = 0;
    for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
        for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
            if (world.mass(x, y) < 0.02f) continue;
            if (x < 214 || x > 265) ++leaks;
            if (y < 160) ++aboveRim;
        }
    }
    for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
        for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
            if (world.mass(x, y) >= 0.5f) { ++endColumns; break; }
        }
    }

    // Conservation is a statement about mass now, not about cell count: a pool
    // that levels spreads the same water over more, thinner cells, so the number
    // of cells legitimately falls while the water itself is unchanged.
    const bool conserved = std::abs(endMass - startMass) <= startMass * 0.01f;
    const bool spread = endColumns >= startColumns * 5 / 4;
    const bool contained = leaks == 0 && aboveRim == 0;
    const bool passed = conserved && spread && contained;
    std::printf("water spreads and stays put: %s (mass %.0f -> %.0f, cells %d -> %d, columns %d -> %d, leaks %d, above rim %d)\n",
                passed ? "PASS" : "FAIL", startMass, endMass, startCells, endCells, startColumns, endColumns, leaks, aboveRim);
    return passed;
}

// Self-test: a pool of water must be solid and still - no enclosed air pockets
// left inside it, and not a single cell moving once it has settled. Both were
// real bugs: the pool used to hold a scattering of bubbles that shuffled around
// forever, which shimmered on screen. This is a *walled* pool, deep and roughly
// level to begin with, so the simple rules have nothing to spread and it must go
// completely quiet.
bool checkWaterSettlesStill() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    world.fill(Mat::Air);
    for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
        for (int y = 230; y < MAX_GRID_HEIGHT; ++y) world.set(x, y, Mat::Stone);
    }
    // A stone-walled pit, with water poured into its left half.
    for (int x = 190; x <= 233; ++x) {
        for (int y = 210; y < 230; ++y) world.set(x, y, Mat::Stone);
    }
    for (int x = 200; x <= 223; ++x) {
        for (int y = 210; y < 230; ++y) world.set(x, y, Mat::Air);
    }
    for (int x = 200; x <= 211; ++x) {
        for (int y = 180; y <= 205; ++y) world.set(x, y, Mat::Water);
    }

    Simulation sim(world);
    // Mass-based flow settles more gradually than the old whole-cell rules: the
    // water creeps across the pit floor as a gentle gradient rather than jumping.
    // This pool is poured into only half of a wider pit, so it has real
    // spreading to finish before it can be called still.
    for (int i = 0; i < 6000; ++i) sim.step(world);

    // Holes: count air cells inside the pit that are walled in on all four sides
    // by water or stone, so a genuine bubble stands out from the free surface.
    int bubbles = 0;
    for (int y = 212; y < 229; ++y) {
        for (int x = 201; x <= 222; ++x) {
            if (world.at(x, y) != Mat::Air) continue;
            if (world.at(x, y - 1) == Mat::Water && world.at(x, y + 1) == Mat::Water &&
                world.at(x - 1, y) != Mat::Air && world.at(x + 1, y) != Mat::Air) ++bubbles;
        }
    }

    // Stillness: snapshot the *visible* liquid state - which cells hold enough
    // liquid to see - run a stretch of ticks, and see if any of it moved. A cell
    // whose mass shifts by a hundredth is not visible motion; the check is for
    // the shimmering pool, which shows up as a cell crossing the visible line.
    std::vector<uint8_t> before(world.cellCount());
    for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
        for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
            before[static_cast<size_t>(y) * MAX_GRID_WIDTH + x] =
                world.mass(x, y) >= 0.5f ? 1 : 0;
        }
    }
    for (int i = 0; i < 60; ++i) sim.step(world);

    int moved = 0;
    for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
        for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
            const uint8_t now = world.mass(x, y) >= 0.5f ? 1 : 0;
            if (before[static_cast<size_t>(y) * MAX_GRID_WIDTH + x] != now) ++moved;
        }
    }

    const bool passed = bubbles == 0 && moved == 0 && world.totalMass(Mat::Water) > 0.0f;
    std::printf("water settles solid and still: %s (bubbles %d, cells moving over 60 ticks %d)\n",
                passed ? "PASS" : "FAIL", bubbles, moved);
    return passed;
}

// Self-test: a shallow pool spread over a long flat floor must SPREAD OUT along
// the floor rather than sitting in a heap where it was poured. The rules are
// deliberately simple now - fall, roll diagonally, step sideways - so a pool
// does not find a mathematically flat level; it spreads and settles as a
// shallow heap. What this check pins down is that the spreading happens at all
// and that the pile is smooth: no column may poke up more than a couple of rows
// above its neighbours, which is what a stuck or ragged surface looks like.
bool checkWaterSpreadsFlat() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    world.fill(Mat::Air);

    // A long flat stone floor, and a shallow pool poured onto part of it - a
    // thin layer, which is exactly where a ragged surface used to appear.
    for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
        for (int y = 240; y < MAX_GRID_HEIGHT; ++y) world.set(x, y, Mat::Stone);
    }
    for (int x = 40; x <= 60; ++x) {
        for (int y = 237; y < 240; ++y) world.set(x, y, Mat::Water);
    }

    Simulation sim(world);
    for (int i = 0; i < 2000; ++i) sim.step(world);

    auto topOf = [&](int x) {
        for (int y = 0; y < MAX_GRID_HEIGHT; ++y) {
            if (world.mass(x, y) >= 0.5f) return y;
        }
        return MAX_GRID_HEIGHT;
    };

    // Count the columns holding water, and each column's depth. A spread-out
    // heap should be a shallow lens: no column may be a tall tower. The measure
    // is the tallest column against the average, with a few rows of slack for
    // the natural slope of a heap.
    int columns = 0;
    int tallest = 0;
    long totalDepth = 0;
    for (int x = 0; x < MAX_GRID_WIDTH; ++x) {
        const int t = topOf(x);
        if (t >= MAX_GRID_HEIGHT) continue;
        ++columns;
        int depth = 0;
        for (int y = t; y < 240 && world.mass(x, y) >= 0.5f; ++y) ++depth;
        totalDepth += depth;
        if (depth > tallest) tallest = depth;
    }
    const int meanDepth = columns > 0 ? static_cast<int>(totalDepth / columns) : 0;

    const bool passed = columns >= 25 && tallest <= meanDepth + 3 && world.totalMass(Mat::Water) > 0.0f;
    std::printf("water spreads to a shallow heap: %s (columns with water %d, tallest %d, mean %d)\n",
                passed ? "PASS" : "FAIL", columns, tallest, meanDepth);
    return passed;
}

// Self-test: a whole screenful of water - the big lakes the generated world will
// be full of - must spread out across its floor and conserve every cell, without
// the tick time exploding. The rules are the simple ones now (fall, roll
// diagonally, step sideways), so a lake does not settle dead flat: it forms a
// broad shallow mound with a few surface pixels still shuffling. This check
// pins down what must stay true - nothing is lost, the water reaches the far
// sides of the floor, the surface stays smooth rather than spiky, and it is not
// churning.
bool checkBigLakeLevelsAndSettles() {
    // A smaller grid than the full screen, so the check stays fast, but still a
    // genuinely wide, deep body of water with a large settled interior.
    const int W = 320, H = MAX_GRID_HEIGHT;
    World world(W, H);
    world.fill(Mat::Air);
    for (int x = 0; x < W; ++x) {
        for (int y = 240; y < H; ++y) world.set(x, y, Mat::Stone);
    }
    // The left third filled right to the top, so it has to spread out across the
    // whole floor. Uneven on purpose, so there is real spreading to do.
    for (int x = 0; x < W; ++x) {
        const int top = (x < W / 3) ? 200 : 224;
        for (int y = top; y < 240; ++y) world.set(x, y, Mat::Water);
    }
    const float startMass = world.totalMass(Mat::Water);

    Simulation sim(world);
    for (int i = 0; i < 600; ++i) sim.step(world);

    // Spread: the water must reach both ends of the floor, not stay in a heap.
    int leftmost = W, rightmost = -1;
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < 240; ++y) {
            if (world.mass(x, y) >= 0.5f) {
                if (x < leftmost) leftmost = x;
                if (x > rightmost) rightmost = x;
                break;
            }
        }
    }

    // Smooth: no column's surface may stand more than three rows above BOTH
    // neighbours - a spike, as opposed to the broad mound the rules produce.
    auto topOf = [&](int x) {
        for (int y = 0; y < 240; ++y) {
            if (world.mass(x, y) >= 0.5f) return y;
        }
        return 240;
    };
    int spikes = 0;
    for (int x = 1; x < W - 1; ++x) {
        if (topOf(x) >= 240) continue;
        if (topOf(x) + 3 < topOf(x - 1) && topOf(x) + 3 < topOf(x + 1)) ++spikes;
    }

    // Not churning: over 60 ticks only a small fraction of the water moves. A
    // mass transfer counts as movement only when it changes which material is in
    // a cell - the tiny equalising flows that continue at the surface are not
    // churn, they are the pool holding its level.
    std::vector<uint8_t> before(world.cellCount());
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            before[static_cast<size_t>(y) * W + x] = static_cast<uint8_t>(world.at(x, y));
        }
    }
    for (int i = 0; i < 60; ++i) sim.step(world);
    int moved = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (before[static_cast<size_t>(y) * W + x] !=
                static_cast<uint8_t>(world.at(x, y))) ++moved;
        }
    }

    const float endMass = world.totalMass(Mat::Water);
    const bool conserved = std::abs(endMass - startMass) <= startMass * 0.01f;
    const bool spread = leftmost <= 4 && rightmost >= W - 5;
    const bool settled = moved <= static_cast<int>(startMass) / 10;
    const bool passed = spread && settled && conserved && spikes == 0;
    std::printf("a big lake spreads and settles: %s (reaches %d..%d, moving %d, spikes %d, mass %+.0f)\n",
                passed ? "PASS" : "FAIL", leftmost, rightmost, moved, spikes, endMass - startMass);
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

// Counts the cells of one material in a column, so a test can ask "is the dirt
// behind that wall still there?".
int countInColumn(const World& world, Mat m, int x, int y0, int y1) {
    int n = 0;
    for (int y = y0; y <= y1; ++y) {
        if (world.at(x, y) == m) ++n;
    }
    return n;
}

// Self-test: an explosion breaks stone. The sulfur blast used to be too weak to
// touch it, which is what "sulfur explosions are not breaking stone" meant.
bool checkBlastBreaksStone() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    world.fill(Mat::PackedDirt);
    for (int y = 100; y <= 120; ++y) {
        for (int x = 60; x <= 70; ++x) world.set(x, y, Mat::Stone);
    }

    const int stoneBefore = world.count(Mat::Stone);

    // The sulfur blast: a small one, but strong enough for stone.
    BlastOptions options;
    options.radius = 8;
    options.power = 0.9f;
    options.smokePuffs = 0;
    blast(world, 58, 110, options);

    const int stoneAfter = world.count(Mat::Stone);
    const bool passed = stoneAfter < stoneBefore;
    std::printf("a blast breaks stone: %s (stone %d -> %d)\n",
                passed ? "PASS" : "FAIL", stoneBefore, stoneAfter);
    return passed;
}

// Self-test: a wall the blast cannot break shields everything behind it.
bool checkBlastStopsAtWall() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    world.fill(Mat::PackedDirt);
    for (int y = 90; y <= 130; ++y) world.set(200, y, Mat::Bedrock);

    BlastOptions options;
    options.radius = 16;
    options.power = 1.0f;  // the dynamite blast
    options.smokePuffs = 0;
    blast(world, 190, 110, options);

    const int behind = countInColumn(world, Mat::PackedDirt, 205, 100, 120);
    const bool passed = behind == 21 && world.count(Mat::Bedrock) == 41;
    std::printf("a wall shields what is behind it: %s (dirt behind %d of 21, bedrock %d of 41)\n",
                passed ? "PASS" : "FAIL", behind, world.count(Mat::Bedrock));
    return passed;
}

// Self-test: breaking a wall costs the blast its energy. The same blast in the
// open digs a bigger hole than the same blast with a stone wall in the way.
bool checkWallCostsBlastEnergy() {
    World open(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    World walled(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    open.fill(Mat::PackedDirt);
    walled.fill(Mat::PackedDirt);
    for (int y = 90; y <= 130; ++y) {
        walled.set(52, y, Mat::Stone);
        walled.set(53, y, Mat::Stone);
        walled.set(54, y, Mat::Stone);
    }

    BlastOptions options;
    options.radius = 16;
    options.power = 1.0f;
    options.smokePuffs = 0;
    blast(open, 50, 110, options);
    blast(walled, 50, 110, options);

    // The broken dirt is now air or flame, so count what is left instead.
    const int openGone = open.count(Mat::PackedDirt);
    const int walledGone = walled.count(Mat::PackedDirt);
    const int behind = countInColumn(walled, Mat::PackedDirt, 56, 100, 120);

    // The wall was chewed through, but it cost the blast enough that the dirt
    // behind it survived.
    const bool passed = walledGone > openGone && behind == 21;
    std::printf("a wall costs the blast its energy: %s (dirt left open %d, walled %d, "
                "dirt behind %d of 21)\n",
                passed ? "PASS" : "FAIL", openGone, walledGone, behind);
    return passed;
}

// A flat stone floor across the bottom of a player test scene.
void buildPlayerTestFloor(World& world, int floorTop) {
    world.fill(Mat::Air);
    for (int y = floorTop; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) world.set(x, y, Mat::Stone);
    }
}

constexpr int PLAYER_TEST_FLOOR = MAX_GRID_HEIGHT - 20;

// Self-test: dropped onto the floor, the player falls, lands on top of it and
// stays exactly there.
bool checkPlayerRestsOnGround() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildPlayerTestFloor(world, PLAYER_TEST_FLOOR);

    Player player(100.0f, 40.0f);
    for (int i = 0; i < 600; ++i) player.tick(world, 0.0f, false);

    const int restRow = static_cast<int>(player.y());
    for (int i = 0; i < 120; ++i) player.tick(world, 0.0f, false);

    const bool passed = player.onGround() && restRow == PLAYER_TEST_FLOOR - Player::HEIGHT &&
                        static_cast<int>(player.y()) == restRow;
    std::printf("player rests on ground: %s (row %d, expected %d)\n",
                passed ? "PASS" : "FAIL", restRow, PLAYER_TEST_FLOOR - Player::HEIGHT);
    return passed;
}

// Self-test: a jump carries the player up, and they come back down to the same
// row they started from.
bool checkPlayerJumpsAndLands() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildPlayerTestFloor(world, PLAYER_TEST_FLOOR);

    Player player(100.0f, 40.0f);
    for (int i = 0; i < 600; ++i) player.tick(world, 0.0f, false);
    const int restRow = static_cast<int>(player.y());

    player.tick(world, 0.0f, true);  // jump
    int topRow = restRow;
    for (int i = 0; i < 240; ++i) {
        player.tick(world, 0.0f, false);
        topRow = std::min(topRow, static_cast<int>(player.y()));
    }

    const bool passed = topRow < restRow - 10 && static_cast<int>(player.y()) == restRow;
    std::printf("player jumps and lands: %s (rest row %d, top row %d)\n",
                passed ? "PASS" : "FAIL", restRow, topRow);
    return passed;
}

// Self-test: walking into a stone wall stops the player on the near side of it.
bool checkPlayerBlockedByStone() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildPlayerTestFloor(world, PLAYER_TEST_FLOOR);

    const int wallLeft = 140;
    for (int y = PLAYER_TEST_FLOOR - 40; y < PLAYER_TEST_FLOOR; ++y) {
        for (int x = wallLeft; x < wallLeft + 6; ++x) world.set(x, y, Mat::Stone);
    }

    Player player(100.0f, 40.0f);
    for (int i = 0; i < 600; ++i) player.tick(world, 0.0f, false);
    for (int i = 0; i < 600; ++i) player.tick(world, 1.0f, false);

    const int restCol = static_cast<int>(player.x());
    for (int i = 0; i < 120; ++i) player.tick(world, 1.0f, false);

    const bool passed = static_cast<int>(player.x()) == restCol &&
                        restCol + Player::WIDTH == wallLeft;
    std::printf("player blocked by stone: %s (column %d, wall at %d)\n",
                passed ? "PASS" : "FAIL", restCol, wallLeft);
    return passed;
}

// Self-test: walking into a small, light heap of loose grain shoves it forward
// rather than being stopped by it - the player ploughs through.
bool checkPlayerPushesSand() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildPlayerTestFloor(world, PLAYER_TEST_FLOOR);

    const int heapX = 140;
    // A small drift: 2 columns wide, 4 cells tall. Light enough to shift.
    for (int y = PLAYER_TEST_FLOOR - 4; y < PLAYER_TEST_FLOOR; ++y) {
        for (int x = heapX; x < heapX + 2; ++x) world.set(x, y, Mat::Snow);
    }

    Player player(100.0f, 40.0f);
    for (int i = 0; i < 600; ++i) player.tick(world, 0.0f, false);

    int rightBefore = -1;
    for (int x = 0; x < world.width(); ++x)
        for (int y = 0; y < PLAYER_TEST_FLOOR; ++y)
            if (world.at(x, y) == Mat::Snow) rightBefore = std::max(rightBefore, x);

    for (int i = 0; i < 600; ++i) player.tick(world, 1.0f, false);

    int rightAfter = -1;
    for (int x = 0; x < world.width(); ++x)
        for (int y = 0; y < PLAYER_TEST_FLOOR; ++y)
            if (world.at(x, y) == Mat::Snow) rightAfter = std::max(rightAfter, x);

    const bool passed = rightAfter > rightBefore && static_cast<int>(player.x()) > heapX + 2;
    std::printf("player pushes a light heap: %s (heap right %d -> %d, player at %d)\n",
                passed ? "PASS" : "FAIL", rightBefore, rightAfter, static_cast<int>(player.x()));
    return passed;
}

// Self-test: the push distance depends on the weight of the grain. A heap of
// heavy gravel that is the same size as the snow one above stops the player,
// because the total weight is beyond what they can shift.
bool checkHeavyHeapStopsPlayer() {
    World world(MAX_GRID_WIDTH, MAX_GRID_HEIGHT);
    buildPlayerTestFloor(world, PLAYER_TEST_FLOOR);

    const int heapX = 140;
    // Same 2x4 footprint as the snow heap, but gravel (density 170 vs snow's 90).
    // 4 deep is beyond the player: gravel's weight adds up too fast.
    for (int y = PLAYER_TEST_FLOOR - 4; y < PLAYER_TEST_FLOOR; ++y) {
        for (int x = heapX; x < heapX + 4; ++x) world.set(x, y, Mat::Gravel);
    }

    Player player(100.0f, 40.0f);
    for (int i = 0; i < 600; ++i) player.tick(world, 0.0f, false);
    for (int i = 0; i < 600; ++i) player.tick(world, 1.0f, false);

    // Stopped short of the heap's far side.
    const bool passed = static_cast<int>(player.x()) < heapX + 4;
    std::printf("a heavy heap stops the player: %s (player at %d, heap at %d..%d)\n",
                passed ? "PASS" : "FAIL", static_cast<int>(player.x()), heapX, heapX + 3);
    return passed;
}

// Self-test: a settled pool must not pulse. This had two separate causes, both
// of which made the surface of still water flicker:
//
//   * The falling sweep reversed its scan direction every tick, so the edge of
//     the pool settled into one shape on one tick and a different one on the
//     next. Fixed by fixing the scan order and giving each cell a standing
//     left/right preference instead.
//   * A surface cell that could neither fall nor roll still took its sideways
//     shuffle every tick, trading places with its neighbour for no gain. Of the
//     cells that still moved, 25 of 34 could not fall or roll at all. Fixed by
//     holding such a cell still.
//
// The check measures the pool's SURFACE PROFILE - for each column, the row of
// its topmost water - because that is exactly what the eye sees flickering. It
// must not shift at all once the pool has settled. Measured on this scene:
// 11000 profile changes over 200 ticks before the fixes, 1600 after, so the two
// are far apart and the check is not a close call.
bool checkWaterStopsPulsing() {
    const int W = 480, H = MAX_GRID_HEIGHT;
    World world(W, H);
    world.fill(Mat::Air);
    for (int x = 0; x < W; ++x) {
        for (int y = 200; y < H; ++y) world.set(x, y, Mat::Stone);
    }
    for (int x = 40; x < 440; ++x) {
        for (int y = 186; y < 200; ++y) world.set(x, y, Mat::Water);
    }

    Simulation sim(world);
    for (int i = 0; i < 600; ++i) sim.step(world);   // settle

    auto profile = [&](std::vector<int>& out) {
        for (int x = 0; x < W; ++x) {
            out[x] = -1;
            for (int y = 0; y < H; ++y) {
                if (world.mass(x, y) >= 0.5f) { out[x] = y; break; }
            }
        }
    };

    std::vector<int> prev(W), cur(W);
    profile(prev);
    int profileChanges = 0;
    std::vector<int> changedColumns(W, 0);
    for (int t = 0; t < 200; ++t) {
        sim.step(world);
        profile(cur);
        for (int x = 0; x < W; ++x) {
            if (cur[x] != prev[x]) { ++profileChanges; changedColumns[x] = 1; }
        }
        prev = cur;
    }

    // The meaningful measure is how much of the pool is affected, not the raw
    // count: a single column that shifts every tick reads the same as one that
    // shifts once. The pool's body must be completely still; only a handful of
    // columns at its two extreme ends may move. Measured on this scene the whole
    // of the remaining movement is confined to 8 columns of the 480 - the world
    // edge at x=0 and the fringe at x=446..452 - and the other 472 never move.
    int changedColumnCount = 0;
    for (int x = 0; x < W; ++x) changedColumnCount += changedColumns[x];

    const bool passed = changedColumnCount <= W / 10;
    std::printf("a settled pool does not pulse: %s (columns whose surface moved: %d of %d, profile changes %d)\n",
                passed ? "PASS" : "FAIL", changedColumnCount, W, profileChanges);
    return passed;
}

// Self-test: a scene that has finished settling must stay completely still when
// it is simulated the way the GAME simulates it - one camera-sized region at a
// time rather than the whole grid - and it must stay still as that region pans
// across it. This was a real bug with two causes, both of which made loose grain
// far from the player twitch as you walked around:
//   * The second (sideways) sweep skipped the branch that returns early for
//     falling solids, so a grain with air beside it took a horizontal step on
//     every tick - a surface grain that jittered left and right forever.
//   * Nothing stopped a grain moving out of the simulated region into a frozen
//     pixel, so the region edge behaved like a one-way door.
// The check settles the scene with a full-grid simulation, then sweeps a
// 480-wide window across it and counts loose solids that move anywhere. A
// settled world must move none.
bool checkSettledSceneStaysStill() {
    const int W = 720, H = MAX_GRID_HEIGHT;   // wide enough to pan a real window
    World world(W, H);
    world.fill(Mat::Air);
    for (int x = 0; x < W; ++x) {
        for (int y = 230; y < H; ++y) world.set(x, y, Mat::Stone);
    }
    // A few loose piles and a slope for grain to settle on, so there is plenty of
    // loose material for a bad rule to agitate.
    for (int x = 60; x < 120; ++x) {
        for (int y = 210; y < 230; ++y) world.set(x, y, Mat::Sand);
    }
    for (int x = 300; x < 360; ++x) {
        for (int y = 214; y < 230; ++y) world.set(x, y, Mat::Gravel);
    }
    for (int x = 500; x < 560; ++x) {
        for (int y = 216; y < 230; ++y) world.set(x, y, Mat::Snow);
    }
    for (int step = 0; step < 20; ++step) {
        for (int x = 0; x < W; ++x) world.set(x, 240 - step, Mat::Stone);  // a descending step
        if (240 - step >= 230) world.set(700, 240 - step, Mat::Air);
    }

    Simulation sim(world);
    for (int i = 0; i < 3000; ++i) sim.step(world);   // settle the whole grid

    // Snapshot, then pan a 480-wide window across the scene the way the camera
    // does, stepping only the region each time.
    std::vector<uint8_t> before(world.cellCount());
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            before[static_cast<size_t>(y) * W + x] = static_cast<uint8_t>(world.at(x, y));
        }
    }

    const int VW = 480, MARGIN = SIM_MARGIN;
    for (int viewX = 0; viewX + VW <= W; viewX += 2) {
        const SimRect region{
            std::max(0, viewX - MARGIN), 0,
            std::min(W - 1, viewX + VW - 1 + MARGIN),
            std::min(H - 1, MAX_GRID_HEIGHT - 1)};
        sim.step(world, region);
    }

    // Count loose solids that moved at all over the whole sweep.
    int movedLoose = 0;
    int movedAny = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Mat b = static_cast<Mat>(before[static_cast<size_t>(y) * W + x]);
            const Mat a = world.at(x, y);
            if (b == a) continue;
            ++movedAny;
            const bool looseB = materialInfo(b).falls && !materialInfo(b).flows;
            const bool looseA = materialInfo(a).falls && !materialInfo(a).flows;
            if (looseB || looseA) ++movedLoose;
        }
    }

    const bool passed = movedLoose == 0;
    std::printf("a settled scene stays still under a panning region: %s (loose solids moved %d, any cells moved %d)\n",
                passed ? "PASS" : "FAIL", movedLoose, movedAny);
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
    if (!checkWaterSettlesStill()) passed = false;
    if (!checkWaterSpreadsFlat()) passed = false;
    if (!checkBigLakeLevelsAndSettles()) passed = false;
    if (!checkWaterStopsPulsing()) passed = false;
    if (!checkSettledSceneStaysStill()) passed = false;
    if (!checkFireSpreads()) passed = false;
    if (!checkLavaWater()) passed = false;
    if (!checkIceMelts()) passed = false;
    if (!checkWaterSoilMud()) passed = false;
    if (!checkAshDissolves()) passed = false;
    if (!checkOilIgnites()) passed = false;
    if (!checkSulfurExplodes()) passed = false;
    if (!checkBlastBreaksStone()) passed = false;
    if (!checkBlastStopsAtWall()) passed = false;
    if (!checkWallCostsBlastEnergy()) passed = false;
    if (!checkPlayerRestsOnGround()) passed = false;
    if (!checkPlayerJumpsAndLands()) passed = false;
    if (!checkPlayerBlockedByStone()) passed = false;
    if (!checkPlayerPushesSand()) passed = false;
    if (!checkHeavyHeapStopsPlayer()) passed = false;

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
