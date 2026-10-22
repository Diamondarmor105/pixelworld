#include "generation.h"

#include "materials.h"
#include "structures.h"

#include <algorithm>
#include <cmath>

namespace {

// The seed of the whole world. One number decides everything: where the hills
// are, where the caves run, what is buried where.
constexpr uint32_t kWorldSeed = 0x9E3779B9u;

// How far below the surface the caves begin. The roof over a cave is solid
// ground, so a pond on the surface has something to stand on and the hills do
// not all cave in.
constexpr int kCaveRoof = 30;

// How much of the noise has to be there before a cell counts as cave. The
// higher this is, the rarer the caves are.
constexpr float kCaveThreshold = 0.67f;

// -- Noise -----------------------------------------------------------------
//
// Everything the generator decides comes out of these. They are not random:
// the same inputs always give the same answer, which is what makes a chunk
// the same every time it is built.

// A repeatable pseudo-random number, the same every time for the same inputs.
uint32_t hashBits(int a, int b, int c) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u;
    h ^= static_cast<uint32_t>(b) * 668265263u;
    h ^= static_cast<uint32_t>(c) * 2246822519u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h;
}

// The same, as a number between 0 and 1.
float hash01(int a, int b, int c) {
    return static_cast<float>(hashBits(a, b, c) & 0xFFFFFFu) / static_cast<float>(0x1000000);
}

// Smooth noise in one dimension: a number that wanders smoothly between 0 and
// 1 instead of jumping about.
float noise1(float x, uint32_t seed) {
    const int cell = static_cast<int>(std::floor(x));
    const float f = x - static_cast<float>(cell);
    const float a = hash01(cell, 0, static_cast<int>(seed));
    const float b = hash01(cell + 1, 0, static_cast<int>(seed));
    const float t = f * f * (3.0f - 2.0f * f);
    return a + (b - a) * t;
}

// Smooth noise in two dimensions.
float noise2(float x, float y, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float tx = fx * fx * (3.0f - 2.0f * fx);
    const float ty = fy * fy * (3.0f - 2.0f * fy);
    const float a = hash01(ix,     iy,     static_cast<int>(seed));
    const float b = hash01(ix + 1, iy,     static_cast<int>(seed));
    const float c = hash01(ix,     iy + 1, static_cast<int>(seed));
    const float d = hash01(ix + 1, iy + 1, static_cast<int>(seed));
    const float top = a + (b - a) * tx;
    const float bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

// Several layers of noise on top of each other. The detail is what makes a
// surface look natural rather than smooth.
float fbm1(float x, uint32_t seed, int octaves) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        total += noise1(x * frequency, seed + static_cast<uint32_t>(i) * 7919u) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return total / norm;
}

float fbm2(float x, float y, uint32_t seed, int octaves) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        total += noise2(x * frequency, y * frequency, seed + static_cast<uint32_t>(i) * 7919u) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return total / norm;
}

// Ridged noise in one dimension.
//
// Ordinary noise makes round humps. Fold it - the same trick as folding a
// piece of paper - and it makes crests instead: a line that comes to a point
// at the top and runs down on both sides. That is what a mountain range looks
// like, so the ranges are built out of this rather than out of plain noise.
float ridged1(float x, uint32_t seed, int octaves) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        float n = noise1(x * frequency, seed + static_cast<uint32_t>(i) * 7919u);
        n = 1.0f - std::fabs(2.0f * n - 1.0f);   // fold: the peaks are at the edges
        total += n * n * amplitude;               // and square them, so the crests are sharp
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return total / norm;
}

// -- What the ground is made of ---------------------------------------------

// The rock under the surface at a given depth. Soil on top, then packed dirt,
// then stone with bands of limestone and basalt mixed through it.
Mat layerMaterial(int worldX, int y, int surface) {
    const int depth = y - surface;
    if (depth <= 2) return Mat::Soil;

    const int dirtDepth = 14 + static_cast<int>(hash01(worldX / 6, 5, static_cast<int>(kWorldSeed)) * 20.0f);
    if (depth < 3 + dirtDepth) return Mat::PackedDirt;

    const float band = fbm2(static_cast<float>(worldX) / 70.0f, static_cast<float>(y) / 30.0f,
                            kWorldSeed + 31u, 2);
    if (band > 0.70f) return Mat::Basalt;
    if (band < 0.26f) return Mat::Limestone;
    return Mat::Stone;
}

// Is there a cave at this spot?
//
// Caves are a property of the world, not of the chunk, so one can run across a
// chunk boundary without a wall appearing in the seam.
//
// They are DEEP: nothing within the top of the ground is hollowed out, so the
// roof over a cave is thick enough to walk on and a pond on the surface has
// solid rock under it.
//
// They are RARE but LARGE: the noise that carves them is slow, so a cavern is
// a cavern and not a scattering of pockets, and it is warped before it is read,
// so it bends and curls and runs into the cavern next to it instead of
// standing alone in the rock.
bool isCave(int worldX, int y, int surface) {
    const float depth = static_cast<float>(y - surface);
    if (depth < kCaveRoof) return false;   // the roof stays solid

    const float fx = static_cast<float>(worldX);

    // The warp. The cave is read at a point that another noise has pushed
    // about, so it winds instead of running in blobs. This is what joins one
    // cavern to the next.
    const float warpX = fbm2(fx / 300.0f, depth / 220.0f, kWorldSeed + 43u, 2) - 0.5f;
    const float warpY = fbm2(fx / 260.0f, depth / 240.0f, kWorldSeed + 47u, 2) - 0.5f;

    // The cave itself: slow noise, so a cavern is a cavern.
    //
    // It is read in DEPTH below the surface rather than in absolute rows, so a
    // cavern is the same distance under the ground everywhere. A flat layer
    // instead would come close to the surface under a hill and sink out of
    // reach under a valley.
    const float n = fbm2((fx + warpX * 220.0f) / 165.0f, (depth + warpY * 220.0f) / 140.0f,
                          kWorldSeed + 41u, 3);
    return n > kCaveThreshold;
}

// -- Drawing the hand-made patterns ----------------------------------------

// Puts one hand-made pattern into a chunk, standing on the ground at `baseX`
// and `baseY` (the bottom-left corner, in world cells).
//
// `baseY` is where the pattern's lowest *filled* row goes. A pattern whose last
// row is empty - a ruined wall whose base has eroded away - is lifted so that
// the part that still exists is the part touching the dirt.
void drawPattern(Chunk& chunk, const StructurePattern& pattern, int baseX, int baseY) {
    const int height = static_cast<int>(chunk.cells.size() / Chunk::WIDTH);
    const int lowestFilled = structureLowestFilledRow(pattern);
    if (lowestFilled < 0) return;

    for (int row = 0; row <= lowestFilled; ++row) {
        const char* line = pattern.rows[row];
        // The pattern is written top row first, but it stands on the ground, so
        // its bottom row goes at `baseY` and the rest stack upwards.
        const int y = baseY - (lowestFilled - row);
        if (y < 0 || y >= height) continue;

        for (int column = 0; line[column] != '\0'; ++column) {
            const int localX = baseX + column;
            if (localX < 0 || localX >= Chunk::WIDTH) continue;
            const Mat m = structureMaterial(line[column]);
            if (m == Mat::Air) continue;
            const size_t cell = chunk.index(localX, y);
            chunk.cells[cell] = static_cast<uint8_t>(m);
            chunk.mass[cell] = isMobile(m) ? 1.0f : 0.0f;
        }
    }
}

// -- The hand-made test scenes ----------------------------------------------
//
// These are the small fixed scenes the self-test runs against. They are not
// part of the endless world; they are there so a check has something small and
// known to measure.

int testSurfaceRow(int x, int worldHeight) {
    const int base = static_cast<int>(worldHeight * 0.72f);
    const int wave = static_cast<int>(7.0f * std::sin(static_cast<float>(x) * 0.02f));
    const int hill = static_cast<int>(4.0f * std::sin(static_cast<float>(x) * 0.005f));
    return base - wave - hill;
}

// Ground: packed dirt at the top, stone underneath, bedrock floor.
void buildGround(World& world) {
    const int height = world.height();

    for (int x = 0; x < TEST_SCENE_WIDTH; ++x) {
        const int surface = testSurfaceRow(x, height);
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

// Fills a rectangle with flame.
void setFire(World& world, int x0, int x1, int y0, int y1) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            world.set(x, y, Mat::Fire);
            world.setLife(x, y, FIRE_LIFE);
        }
    }
}

// A pool of lava in a stone cup, sunk into the ground. The stone keeps it from
// wandering off across the surface.
void buildLavaPool(World& world, int centerX, int halfWidth, int depth) {
    const int top = testSurfaceRow(centerX, world.height()) - 2;
    for (int y = top; y <= top + depth + 1; ++y) {
        for (int dx = -halfWidth - 1; dx <= halfWidth + 1; ++dx) {
            const bool wall = std::abs(dx) == halfWidth + 1 || y == top + depth + 1;
            world.set(centerX + dx, y, wall ? Mat::Stone : Mat::Lava);
        }
    }
}

// A bank of snow lying on the ground.
void buildSnowBank(World& world, int centerX, int groundY, int halfWidth) {
    for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
        const int rise = halfWidth - std::abs(dx) + 1;
        for (int dy = 0; dy < rise; ++dy) {
            world.set(centerX + dx, groundY - 1 - dy, Mat::Snow);
        }
    }
}

// A block of glacial ice buried in the stone, waiting to be dug out and melted.
void buildIceBlock(World& world, int centerX, int topY, int halfWidth, int depth) {
    for (int y = topY; y < topY + depth; ++y) {
        for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
            const float nx = static_cast<float>(dx) / static_cast<float>(halfWidth);
            const float ny = static_cast<float>(y - topY - depth / 2) / (depth * 0.5f);
            if (nx * nx + ny * ny <= 1.0f) world.set(centerX + dx, y, Mat::GlacialIce);
        }
    }
}

// A pile of ash lying on the ground, waiting to be washed away.
void buildAshPile(World& world, int centerX, int groundY, int halfWidth) {
    for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
        const int rise = halfWidth - std::abs(dx) + 1;
        for (int dy = 0; dy < rise; ++dy) {
            world.set(centerX + dx, groundY - 1 - dy, Mat::Ash);
        }
    }
}

// A seam of sulfur crust under the ground. Lava reaching it blows a crater.
void buildSulfurSeam(World& world, int startX, int endX, int y, int thickness) {
    for (int x = startX; x < endX; ++x) {
        for (int dy = 0; dy < thickness; ++dy) {
            world.set(x, y + dy, Mat::SulfurCrust);
        }
    }
}

// A wall of stone standing on the ground, to walk into and be stopped by.
void buildWall(World& world, int centerX, int groundY, int halfWidth, int height) {
    for (int y = groundY - height; y < groundY; ++y) {
        for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
            world.set(centerX + dx, y, Mat::Stone);
        }
    }
}

// A gap in the ground: a pit with a flat floor, wide enough that it has to be
// jumped rather than walked.
void buildGap(World& world, int centerX, int halfWidth, int depth) {
    const int height = world.height();
    for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
        const int x = centerX + dx;
        const int surface = testSurfaceRow(x, height);
        for (int y = surface; y < surface + depth; ++y) {
            world.set(x, y, Mat::Air);
        }
    }
}

// A sand pit: a shallow dish cut into the ground and lined with loose sand, with
// sides gentle enough to walk down into and straight back out of. The player
// stands on the sand rather than sinking through it - see devpost/spec.md >
// Components > Player.
void buildSandPit(World& world, int centerX, int halfWidth, int depth) {
    const int height = world.height();
    for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
        const int x = centerX + dx;
        const int surface = testSurfaceRow(x, height);
        const float t = static_cast<float>(std::abs(dx)) / static_cast<float>(halfWidth);
        const int drop = static_cast<int>(static_cast<float>(depth) * (1.0f - t * t));

        for (int y = surface; y < surface + drop; ++y) world.set(x, y, Mat::Air);
        for (int y = surface + drop; y < surface + drop + 6; ++y) {
            world.set(x, y, Mat::Sand);
        }
    }
}

// A stone floor across the bottom of a test scene.
void buildTestFloor(World& world, int floorTop) {
    for (int x = 0; x < TEST_SCENE_WIDTH; ++x) {
        for (int y = floorTop; y < world.height(); ++y) {
            world.set(x, y, Mat::Stone);
        }
    }
}

}  // namespace

// -- The shape of the land --------------------------------------------------
//
// The land is three things stacked on top of each other:
//
//   * a great swell, thousands of columns wide, that makes the whole world
//     rise and fall;
//   * broad rolling hills on top of that; and
//   * mountain ranges standing above both, with snow on their tops.
//
// All three are large. The land is meant to look like a landscape you walk
// through for a long time, not a ripple you step over.

// How many columns the mountain height is averaged over. The raw height curve
// has sharp kinks where the noise folds, and a seven-row cliff in the ground
// every few hundred columns is neither a hill nor a mountain - it is a wall,
// and the snow on top of it slides off. Averaging takes the kink out and
// leaves a slope.
constexpr int kMountainSmoothing = 7;

// The height of the mountain at a column, before it is smoothed.
static float rawMountainHeightAt(int worldX) {
    const float x = static_cast<float>(worldX);

    // The crest of the range, folded so that it comes to a point.
    const float crest = ridged1(x / 1500.0f, kWorldSeed + 23u, 3);

    // Where the ranges are. Most of the world has none: the ranges cross it
    // now and then, with wide plains between them.
    const float rangeMask = fbm1(x / 3600.0f, kWorldSeed + 29u, 2);

    // A range has to be both high enough to be a range and in a place where
    // there is one. The geometric mean of the two keeps the ranges broad
    // instead of cutting them down to a few lonely peaks.
    const float crestHeight = std::max(0.0f, (crest - 0.30f) / 0.42f);
    const float rangeHeight = std::max(0.0f, (rangeMask - 0.40f) / 0.36f);
    return std::min(1.0f, std::sqrt(crestHeight * rangeHeight));
}

// How high the mountain is at this column: 0 in the lowlands, 1 at the very top
// of a range.
float mountainHeightAt(int worldX) {
    float total = 0.0f;
    for (int d = -kMountainSmoothing; d <= kMountainSmoothing; ++d) {
        total += rawMountainHeightAt(worldX + d);
    }
    return total / static_cast<float>(2 * kMountainSmoothing + 1);
}

// The land at one column: where the ground starts, and how much snow lies on
// top of it.
struct Land {
    int surface;    // the first solid row
    int snowRows;   // how many rows of snow sit on it
};

static Land landAt(int worldX) {
    const float x = static_cast<float>(worldX);

    // The great swell: the whole world rising and falling over thousands of
    // columns, so the land is never the same height two valleys apart.
    const float swell = fbm1(x / 3200.0f, kWorldSeed + 5u, 2) * 2.0f - 1.0f;

    // Broad rolling hills on top of that.
    const float hills = fbm1(x / 700.0f, kWorldSeed + 11u, 3) * 2.0f - 1.0f;

    const float mountain = mountainHeightAt(worldX);

    // Lowland level. Everything else is measured up from here.
    const float surface = 330.0f - swell * 30.0f - hills * 26.0f - mountain * 150.0f;

    Land land;
    land.surface = static_cast<int>(std::lround(surface));

    // Snow belongs to the mountain tops and to nothing else.
    //
    // It is decided by the mountain itself, not by a line drawn across the
    // map. A fixed line puts snow on a hill in the lowlands whenever the
    // ground happens to reach it, and leaves a bare peak whenever the hills
    // happen to be high there - which is exactly how snow ended up all over
    // the surface instead of up on the mountains.
    const float m = mountain;
    if (m > 0.90f) land.snowRows = 5;
    else if (m > 0.80f) land.snowRows = 4;
    else if (m > 0.68f) land.snowRows = 3;
    else if (m > 0.55f) land.snowRows = 2;
    else if (m > 0.42f) land.snowRows = 1;
    else land.snowRows = 0;
    return land;
}

// -- The endless world ------------------------------------------------------

int groundRow(int worldX) {
    return landAt(worldX).surface;
}

int snowRowsAt(int worldX) {
    return landAt(worldX).snowRows;
}

void generateChunk(int index, Chunk& chunk, std::vector<StructurePlacement>* placements) {
    const int baseX = index * Chunk::WIDTH;
    const int height = static_cast<int>(chunk.cells.size() / Chunk::WIDTH);
    if (height <= 0) return;

    // 1. The ground: a surface that follows the height curve, layered soil,
    //    dirt and rock underneath, snow on the mountain tops, bedrock on the
    //    floor.
    for (int lx = 0; lx < Chunk::WIDTH; ++lx) {
        const int worldX = baseX + lx;
        const Land land = landAt(worldX);
        const int surface = land.surface;
        for (int y = 0; y < height; ++y) {
            Mat m = Mat::Air;
            if (y >= surface) {
                if (y >= height - 3) {
                    m = Mat::Bedrock;                    // the floor of the world
                } else if (y < surface + land.snowRows) {
                    m = Mat::Snow;                       // snow on the mountain tops
                } else {
                    m = layerMaterial(worldX, y, surface);
                }
            }
            const size_t cell = chunk.index(lx, y);
            chunk.cells[cell] = static_cast<uint8_t>(m);
            chunk.life[cell] = 0;
            chunk.mass[cell] = 0.0f;
        }
    }

    // 2. Caves, hollowed out of the rock.
    for (int lx = 0; lx < Chunk::WIDTH; ++lx) {
        const int worldX = baseX + lx;
        const int surface = groundRow(worldX);
        for (int y = surface + kCaveRoof; y < height - 3; ++y) {
            if (!isCave(worldX, y, surface)) continue;
            const size_t cell = chunk.index(lx, y);
            chunk.cells[cell] = static_cast<uint8_t>(Mat::Air);
            chunk.mass[cell] = 0.0f;
        }
    }

    // 3. Pockets of loose material buried in the ground: sand, gravel, clay.
    //    They are big - a pocket you can dig a hole in.
    static const Mat kPocketMaterials[3] = {Mat::Sand, Mat::Gravel, Mat::Clay};
    for (int i = 0; i < 4; ++i) {
        const int kind = static_cast<int>(hash01(index, i, 91) * 3.0f) % 3;
        const Mat m = kPocketMaterials[kind];
        const int lx = 10 + static_cast<int>(hash01(index, i, 92) * (Chunk::WIDTH - 20.0f));
        const int halfWidth = 6 + static_cast<int>(hash01(index, i, 93) * 9.0f);
        const int halfHeight = 4 + static_cast<int>(hash01(index, i, 94) * 6.0f);
        const int centreY = groundRow(baseX + lx) + 34 +
                            static_cast<int>(hash01(index, i, 95) * 60.0f);
        for (int dy = -halfHeight; dy <= halfHeight; ++dy) {
            for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
                const float nx = static_cast<float>(dx) / static_cast<float>(halfWidth);
                const float ny = static_cast<float>(dy) / static_cast<float>(halfHeight);
                if (nx * nx + ny * ny > 1.0f) continue;
                const int l = lx + dx;
                const int y = centreY + dy;
                if (l < 0 || l >= Chunk::WIDTH || y < 0 || y >= height - 3) continue;
                const size_t cell = chunk.index(l, y);
                chunk.cells[cell] = static_cast<uint8_t>(m);
                chunk.mass[cell] = 1.0f;
            }
        }
    }

    // 4. A pond on the surface, where the ground is flat enough to hold one.
    //
    //    The pond is a stone basin dug into the ground: a FLAT floor below
    //    the lowest rim, so the water in it is level from end to end, a lip of
    //    rock at each side to stop it wandering off down the hill, and a
    //    stone floor underneath, because the ground down there might be hollow
    //    - a cave under the pond would swallow the water whole.
    //
    //    It only appears where the ground is flat across the whole span. A
    //    bowl dug into a slope comes out as a stepped pit, and the water ends
    //    up pooled in the deepest step with the shallow ones left dry.
    int pondLeft = 0;
    int pondRight = -1;
    {
        const int lx = 24 + static_cast<int>(hash01(index, 0, 101) * (Chunk::WIDTH - 48.0f));
        const int half = 10 + static_cast<int>(hash01(index, 0, 102) * 9.0f);
        const int spanLeft = lx - half;
        const int spanRight = lx + half;

        // How much the ground moves up and down across the pond and its rims.
        int lowest = 0;
        int highest = 1 << 30;
        for (int l = spanLeft - 1; l <= spanRight + 1; ++l) {
            const int g = groundRow(baseX + l);
            lowest = std::max(lowest, g);
            highest = std::min(highest, g);
        }

        if (lowest - highest <= 3) {
            // The floor of the bowl: one row under the lowest rim of the whole
            // span, so the pond is a shallow dip rather than a hole.
            const int floorRow = lowest + 1;
            const int waterDepth = 4;
            const int floorThickness = 3;

            // Dig the bowl out.
            for (int l = spanLeft; l <= spanRight; ++l) {
                const int s = groundRow(baseX + l);
                for (int y = s; y < floorRow + waterDepth; ++y) {
                    if (y < 0 || y >= height) continue;
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::Air);
                    chunk.mass[cell] = 0.0f;
                }
            }
            // Fill it.
            for (int l = spanLeft; l <= spanRight; ++l) {
                for (int y = floorRow; y < floorRow + waterDepth; ++y) {
                    if (y < 0 || y >= height) continue;
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::Water);
                    chunk.mass[cell] = 1.0f;
                }
            }
            // The lip: rock at each side, up to the level of the water.
            for (int l : {spanLeft - 1, spanRight + 1}) {
                if (l < 0 || l >= Chunk::WIDTH) continue;
                const int s = groundRow(baseX + l);
                for (int y = s; y < floorRow + waterDepth; ++y) {
                    if (y < 0 || y >= height) continue;
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::Stone);
                    chunk.mass[cell] = 0.0f;
                }
            }
            // The floor of the basin, so the pond has a bottom to stand on.
            for (int l = spanLeft - 1; l <= spanRight + 1; ++l) {
                if (l < 0 || l >= Chunk::WIDTH) continue;
                for (int y = floorRow + waterDepth; y < floorRow + waterDepth + floorThickness;
                     ++y) {
                    if (y < 0 || y >= height) continue;
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::Stone);
                    chunk.mass[cell] = 0.0f;
                }
            }
            pondLeft = spanLeft;
            pondRight = spanRight;
        }
    }

    // 5. A lava pool deep down, in a stone cup, with sulfur crust in the rock
    //    beside it. The cup is deeper than the lava, so the lava sits in the
    //    bottom of it and stays put. Break the cup and the lava finds the
    //    sulfur.
    {
        const int lx = 30 + static_cast<int>(hash01(index, 0, 111) * (Chunk::WIDTH - 60.0f));
        const int half = 8 + static_cast<int>(hash01(index, 0, 113) * 7.0f);
        const int cupHeight = 17;
        const int lavaHeight = 8;
        // Deep down, but never so deep that the cup runs into the floor of the
        // world.
        const int top = std::min(groundRow(baseX + lx) + 80 +
                                     static_cast<int>(hash01(index, 0, 112) * 50.0f),
                                 height - cupHeight - 8);

        // The cup: walls at each side and a floor at the bottom. The inside
        // above the lava is hollow air.
        for (int dx = -half - 1; dx <= half + 1; ++dx) {
            for (int dy = 0; dy < cupHeight; ++dy) {
                const int l = lx + dx;
                const int y = top + dy;
                if (l < 0 || l >= Chunk::WIDTH || y < 0 || y >= height - 3) continue;
                const bool wall = std::abs(dx) == half + 1;
                const bool floor = dy == cupHeight - 1;
                Mat m = Mat::Air;
                if (wall || floor) m = Mat::Stone;
                else if (dy >= cupHeight - 1 - lavaHeight) m = Mat::Lava;
                const size_t cell = chunk.index(l, y);
                chunk.cells[cell] = static_cast<uint8_t>(m);
                chunk.mass[cell] = (m == Mat::Lava) ? 1.0f : 0.0f;
            }
        }
        // The sulfur is two cells out from the lava, so it waits for the lava to
        // break the cup rather than setting it off on the first tick.
        const int lavaTop = top + cupHeight - 1 - lavaHeight;
        for (int dx = -half - 4; dx <= half + 4; ++dx) {
            if (std::abs(dx) >= half + 2 && std::abs(dx) <= half + 4) {
                for (int dy = 0; dy < 3; ++dy) {
                    const int l = lx + dx;
                    const int y = lavaTop + dy;
                    if (l < 0 || l >= Chunk::WIDTH || y < 0 || y >= height - 3) continue;
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::SulfurCrust);
                    chunk.mass[cell] = 0.0f;
                }
            }
        }
    }

    // 6. The hand-made things that stand on the ground: a ruined wall, a tree,
    //    a bush, a pile of bones. Each chunk's own recipe decides which ones
    //    and where, and each one is kept inside the chunk so no seam appears.
    struct Footprint {
        int left;
        int right;
    };
    std::vector<Footprint> footprints;

    for (int i = 0; i < 3; ++i) {
        const int kind = static_cast<int>(hash01(index, i, 121) * structureKindCount()) %
                         structureKindCount();
        const int variant = static_cast<int>(hash01(index, i, 122) * 8.0f);
        const StructurePattern& pattern = structurePattern(kind, variant);
        const int patternWidth = structurePatternWidth(pattern);
        const int patternHeight = pattern.rowCount;
        if (patternWidth <= 0 || patternWidth >= Chunk::WIDTH - 12) continue;

        const int room = Chunk::WIDTH - 12 - patternWidth;
        const int lx = 6 + static_cast<int>(hash01(index, i, 123) * static_cast<float>(room));

        // Keep clear of the pond, so nothing is left standing in the water.
        if (pondRight >= pondLeft && lx + patternWidth - 1 >= pondLeft && lx <= pondRight) {
            continue;
        }

        // Keep clear of the other structures, so one does not grow out of
        // another.
        bool clashes = false;
        for (const Footprint& f : footprints) {
            if (lx + patternWidth - 1 >= f.left && lx <= f.right) clashes = true;
        }
        if (clashes) continue;

        // The structure stands on the ground: its foot goes on the lowest
        // ground underneath it, so it is never left hanging in the air, and
        // the row above that, so it is not buried in the dirt either.
        int base = 0;
        for (int dx = 0; dx < patternWidth; ++dx) {
            base = std::max(base, groundRow(baseX + lx + dx));
        }
        base = std::min(base, height - patternHeight - 1);
        if (base < patternHeight) continue;

        drawPattern(chunk, pattern, lx, base - 1);
        footprints.push_back({lx, lx + patternWidth - 1});

        if (placements) {
            StructurePlacement placement;
            placement.x = baseX + lx;
            placement.y = base - 1;
            placement.width = patternWidth;
            placement.height = patternHeight;
            placement.kind = kind;
            placement.variant = variant;
            placements->push_back(placement);
        }
    }
}

void generateChunksAround(World& world, int worldX, int radius) {
    const int centre = world.chunkIndexOf(worldX);
    for (int index = centre - radius; index <= centre + radius; ++index) {
        if (world.hasChunk(index)) continue;
        Chunk& chunk = world.chunk(index);
        generateChunk(index, chunk);
    }
}

// -- The hand-made test scenes ----------------------------------------------

void buildTestScene(World& world) {
    const int width = TEST_SCENE_WIDTH;
    const int height = world.height();

    world.fill(0, width - 1, Mat::Air);
    buildGround(world);

    // Something to walk into, something to jump over, and something to stand in.
    // The wall is short enough to jump with a well-timed leap, so it stops you
    // walking but does not wall off half the level.
    buildWall(world, width * 22 / 100, testSurfaceRow(width * 22 / 100, height), 4, 16);
    buildGap(world, width * 64 / 100, 10, 18);
    buildSandPit(world, width * 56 / 100, 26, 12);

    // A column of sand hanging in the air above the ground. It falls and spreads
    // into a heap you can walk up, which is the point.
    const int columnCenter = width / 2;
    const int columnBottom = testSurfaceRow(columnCenter, height) - 44;
    for (int y = columnBottom - 50; y <= columnBottom; ++y) {
        for (int x = columnCenter - 14; x <= columnCenter + 14; ++x) {
            world.set(x, y, Mat::Sand);
        }
    }

    // A gravel heap on the left: also falls and settles into a slope.
    const int heapCenter = width / 6;
    const int heapBase = testSurfaceRow(heapCenter, height);
    for (int dx = -30; dx <= 30; ++dx) {
        const int rise = 30 - std::abs(dx);
        for (int dy = 0; dy < rise; ++dy) {
            world.set(heapCenter + dx, heapBase - dy, Mat::Gravel);
        }
    }

    // A bank of soil on the right third, to show a looser, darker ground.
    const int bankCenter = width * 3 / 5;
    const int bankBase = testSurfaceRow(bankCenter, height);
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
        const int surface = testSurfaceRow(x, height);
        const int depth = 10 + (poolHalf - std::abs(dx)) / 3;
        for (int y = surface; y < surface + depth; ++y) {
            world.set(x, y, Mat::Air);
        }
    }
    for (int dx = -poolHalf; dx < poolHalf; ++dx) {
        const int x = poolCenter + dx;
        const int surface = testSurfaceRow(x, height);
        for (int y = surface; y < surface + 7; ++y) {
            world.set(x, y, Mat::Water);
        }
    }

    // An oil slick floating on the left of the pool. Oil is lighter than water,
    // so the simulation sorts the two out and the oil rises to the surface.
    for (int x = poolCenter - 26; x < poolCenter - 6; ++x) {
        const int surface = testSurfaceRow(x, height);
        for (int y = surface; y < surface + 3; ++y) {
            world.set(x, y, Mat::Oil);
        }
    }

    // Two trees and a fallen log, to burn. The trunks are shorter than the
    // player can jump, so they are scenery rather than barriers.
    buildTree(world, width * 42 / 100, testSurfaceRow(width * 42 / 100, height), 20, 13);
    buildTree(world, width * 90 / 100, testSurfaceRow(width * 90 / 100, height), 16, 10);
    buildLog(world, width * 84 / 100, width * 84 / 100 + 18, testSurfaceRow(width * 84 / 100, height));

    // A lava pool in its own stone cup, walled off from the pond. Break the
    // ground between them and the water crusts the lava into stone.
    buildLavaPool(world, width * 31 / 100, 14, 6);

    // A seam of sulfur buried just below the lava pool. Nothing happens until
    // lava finds its way down to it - then the ground blows.
    buildSulfurSeam(world, width * 29 / 100, width * 34 / 100,
                    testSurfaceRow(width * 31 / 100, height) + 10, 2);

    // Snow on the far left, a pile of ash to be washed away, and a block of
    // glacial ice buried under the sand column.
    buildSnowBank(world, width * 3 / 100, testSurfaceRow(width * 3 / 100, height), 12);
    buildAshPile(world, width * 37 / 100, testSurfaceRow(width * 37 / 100, height), 8);
    buildIceBlock(world, width / 2, testSurfaceRow(width / 2, height) + 12, 12, 14);
}

void buildBurnTestScene(World& world) {
    const int width = TEST_SCENE_WIDTH;
    const int height = world.height();

    world.fill(0, width - 1, Mat::Air);
    buildTestFloor(world, height - 6);

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
    const int width = TEST_SCENE_WIDTH;
    const int cx = width / 2;
    const int bottom = world.height() - 60;

    world.fill(0, width - 1, Mat::Air);
    buildTestFloor(world, world.height() - 4);

    // A block of sand hanging well above the floor.
    for (int y = bottom - 24; y <= bottom; ++y) {
        for (int x = cx - 12; x <= cx + 12; ++x) {
            world.set(x, y, Mat::Sand);
        }
    }
}

void buildLavaWaterScene(World& world, int lavaHalfWidth) {
    const int width = TEST_SCENE_WIDTH;
    const int cx = width / 2;
    const int bottom = world.height() - 31;

    world.fill(0, width - 1, Mat::Air);
    buildTestFloor(world, world.height() - 30);

    // A stone-walled well: lava at the bottom, water on top of it. Water is the
    // lighter of the two, so it stays on top and the two keep touching.
    for (int y = bottom - 22; y <= bottom; ++y) {
        for (int dx = -lavaHalfWidth - 1; dx <= lavaHalfWidth + 1; ++dx) {
            const bool wall = std::abs(dx) == lavaHalfWidth + 1;
            Mat m = Mat::Air;
            if (wall) m = Mat::Stone;
            else if (y >= bottom - 14) m = Mat::Lava;
            else m = Mat::Water;
            world.set(cx + dx, y, m);
        }
    }
}

void buildIceMeltsScene(World& world) {
    const int width = TEST_SCENE_WIDTH;
    const int cx = width / 2;
    const int bottom = world.height() - 31;

    world.fill(0, width - 1, Mat::Air);
    buildTestFloor(world, world.height() - 30);

    // A block of glacial ice and a bank of snow, each with flame pressed into it.
    for (int y = bottom - 20; y <= bottom; ++y) {
        for (int x = cx - 40; x <= cx - 20; ++x) world.set(x, y, Mat::GlacialIce);
    }
    for (int y = bottom - 10; y <= bottom; ++y) {
        for (int x = cx + 10; x <= cx + 30; ++x) world.set(x, y, Mat::Snow);
    }
    setFire(world, cx - 42, cx - 18, bottom - 23, bottom - 19);
    setFire(world, cx + 8, cx + 32, bottom - 13, bottom - 9);
}

void buildMudScene(World& world) {
    const int width = TEST_SCENE_WIDTH;
    const int height = world.height();
    const int floorTop = height - 40;

    world.fill(0, width - 1, Mat::Air);
    buildTestFloor(world, floorTop);

    // Stone walls on either side, soil in the bottom, water soaking onto it.
    for (int y = floorTop - 30; y < floorTop; ++y) {
        for (int x = 40; x < 46; ++x) world.set(x, y, Mat::Stone);
        for (int x = width - 46; x < width - 40; ++x) world.set(x, y, Mat::Stone);
    }
    for (int y = floorTop - 10; y < floorTop; ++y) {
        for (int x = 60; x < width - 60; ++x) world.set(x, y, Mat::Soil);
    }
    for (int y = floorTop - 26; y < floorTop - 11; ++y) {
        for (int x = 60; x < width - 60; ++x) world.set(x, y, Mat::Water);
    }
}

void buildAshScene(World& world) {
    const int width = TEST_SCENE_WIDTH;
    const int floorTop = world.height() - 40;

    world.fill(0, width - 1, Mat::Air);
    buildTestFloor(world, floorTop);

    for (int y = floorTop - 30; y < floorTop; ++y) {
        for (int x = 40; x < 46; ++x) world.set(x, y, Mat::Stone);
        for (int x = width - 46; x < width - 40; ++x) world.set(x, y, Mat::Stone);
    }
    for (int y = floorTop - 10; y < floorTop; ++y) {
        for (int x = 60; x < width - 60; ++x) world.set(x, y, Mat::Ash);
    }
    for (int y = floorTop - 26; y < floorTop - 11; ++y) {
        for (int x = 60; x < width - 60; ++x) world.set(x, y, Mat::Water);
    }
}

void buildOilScene(World& world) {
    const int width = TEST_SCENE_WIDTH;
    const int cx = width / 2;
    const int bottom = world.height() - 31;

    world.fill(0, width - 1, Mat::Air);
    buildTestFloor(world, world.height() - 30);

    // Oil floating on lava. It should go up on the spot, fiercely.
    for (int y = bottom - 10; y <= bottom; ++y) {
        for (int x = cx - 60; x <= cx + 60; ++x) world.set(x, y, Mat::Lava);
    }
    for (int y = bottom - 16; y <= bottom - 11; ++y) {
        for (int x = cx - 60; x <= cx + 60; ++x) world.set(x, y, Mat::Oil);
    }
}

void buildSulfurScene(World& world) {
    const int width = TEST_SCENE_WIDTH;
    const int cx = width / 2;
    const int bottom = world.height() - 31;

    world.fill(0, width - 1, Mat::Air);
    buildTestFloor(world, world.height() - 30);

    // A slab of packed dirt with a blob of sulfur crust buried in it, and a
    // pocket of lava sitting directly on top of the blob. The lava is on the
    // sulfur from the first tick, and keeps falling onto more of it as it blows
    // craters out from under itself.
    for (int y = bottom - 30; y <= bottom; ++y) {
        for (int x = cx - 100; x <= cx + 100; ++x) world.set(x, y, Mat::PackedDirt);
    }
    for (int y = bottom - 10; y <= bottom - 1; ++y) {
        for (int x = cx - 5; x <= cx + 4; ++x) world.set(x, y, Mat::SulfurCrust);
    }
    for (int y = bottom - 24; y <= bottom - 11; ++y) {
        for (int x = cx - 5; x <= cx + 4; ++x) world.set(x, y, Mat::Lava);
    }
}
