#include "generation.h"

#include "materials.h"
#include "structures.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

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
constexpr float kCaveThreshold = 0.62f;

// How much the caves open up as they go down. The rock near the surface is
// nearly solid; the rock below it is hollow in big caverns. Without this the
// deep world would be a wall of stone with a few pockets in it.
constexpr float kCaveDepthBias = 0.08f;

// How much the caves close up again in the deepest rock. The bottom of the
// world is the bedrock everything else is built on, and it is solid.
constexpr float kCaveBasement = 0.12f;

// How wide a cave is, and how tall. These are in cells of the noise's own
// space, so a cavern comes out roughly this many cells across. They are big
// on purpose: a cave is a place you can walk into, not a pocket of air in
// the rock.
constexpr float kCaveWidth = 900.0f;
constexpr float kCaveHeight = 700.0f;

// The third axis of the cave volume: how thick the slice of rock we are
// cutting is. Bigger means the tunnels are longer before they pinch.
constexpr float kCaveSlice = 600.0f;

// How far the warp pushes the cave noise about, in cells. This is what makes a
// cavern wind and curl instead of sitting in the rock as a lump.
constexpr float kCaveWarp = 150.0f;

// How many octaves of noise the cave field is built from. Each extra octave
// adds finer detail, and finer detail is what breaks a big cavern into a
// heap of small ones.
constexpr int kCaveOctaves = 1;

// The cave field is read every this many rows and filled in between. The
// field is smooth over hundreds of rows, so the shape is the same and it
// costs a quarter of the noise work.
constexpr int kCaveStep = 8;

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

// The twelve directions a gradient can point: the corners and the edges of a
// cube, seen from one side. Using all twelve keeps the noise from looking
// like it was combed in one direction.
static const float kGrad3[12][3] = {
    { 1.0f,  1.0f,  0.0f}, {-1.0f,  1.0f,  0.0f},
    { 1.0f, -1.0f,  0.0f}, {-1.0f, -1.0f,  0.0f},
    { 1.0f,  0.0f,  1.0f}, {-1.0f,  0.0f,  1.0f},
    { 1.0f,  0.0f, -1.0f}, {-1.0f,  0.0f, -1.0f},
    { 0.0f,  1.0f,  1.0f}, { 0.0f, -1.0f,  1.0f},
    { 0.0f,  1.0f, -1.0f}, { 0.0f, -1.0f, -1.0f},
};

// Smooth noise in one dimension: a number that wanders smoothly between 0 and
// 1 instead of jumping about.
//
// A gradient noise, not a value noise. The number held at each whole column
// is a slope rather than a height, and the line between them is smooth the
// whole way through. A value noise has a flat spot at every whole column,
// and the eye finds those flat spots and reads them as steps.
float noise1(float x, uint32_t seed) {
    const int cell = static_cast<int>(std::floor(x));
    const float f = x - static_cast<float>(cell);
    const float a = hash01(cell, 0, static_cast<int>(seed)) * 2.0f - 1.0f;
    const float b = hash01(cell + 1, 0, static_cast<int>(seed)) * 2.0f - 1.0f;
    const float t = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    return (a + (b - a) * t) * 0.5f + 0.5f;
}

// Simplex noise in two dimensions.
//
// The world is read off a lattice of triangles rather than a grid of squares.
// A feature can lean any way it likes and nothing lines up with anything
// else, which is the whole point of it: a grid shows through the shape it
// makes, and hills built on a grid have square corners.
static float simplex2(float x, float y, uint32_t seed) {
    // Skew the point into the lattice of triangles.
    constexpr float kSkew = 0.3660254037844386f;    // (sqrt(3) - 1) / 2
    constexpr float kUnskew = 0.21132486540518713f;  // (3 - sqrt(3)) / 6

    const float skew = (x + y) * kSkew;
    const int i = static_cast<int>(std::floor(x + skew));
    const int j = static_cast<int>(std::floor(y + skew));

    // Unskew it again to find where we are inside the triangle.
    const float unskew = (static_cast<float>(i) + static_cast<float>(j)) * kUnskew;
    const float x0 = x - (static_cast<float>(i) - unskew);
    const float y0 = y - (static_cast<float>(j) - unskew);

    // Which of the two triangles of the lattice cell we are in.
    const int i1 = (x0 > y0) ? 1 : 0;
    const int j1 = (x0 > y0) ? 0 : 1;

    const float x1 = x0 - static_cast<float>(i1) + kUnskew;
    const float y1 = y0 - static_cast<float>(j1) + kUnskew;
    const float x2 = x0 - 1.0f + 2.0f * kUnskew;
    const float y2 = y0 - 1.0f + 2.0f * kUnskew;

    const int s = static_cast<int>(seed);
    float total = 0.0f;

    float t = 0.5f - x0 * x0 - y0 * y0;
    if (t > 0.0f) {
        t *= t;
        const float* g = kGrad3[hashBits(i, j, s) % 12u];
        total += t * t * (g[0] * x0 + g[1] * y0);
    }
    t = 0.5f - x1 * x1 - y1 * y1;
    if (t > 0.0f) {
        t *= t;
        const float* g = kGrad3[hashBits(i + i1, j + j1, s) % 12u];
        total += t * t * (g[0] * x1 + g[1] * y1);
    }
    t = 0.5f - x2 * x2 - y2 * y2;
    if (t > 0.0f) {
        t *= t;
        const float* g = kGrad3[hashBits(i + 1, j + 1, s) % 12u];
        total += t * t * (g[0] * x2 + g[1] * y2);
    }
    return total * 70.0f;
}

// Smooth noise in two dimensions, between 0 and 1.
float noise2(float x, float y, uint32_t seed) {
    const float n = simplex2(x, y, seed) * 0.5f + 0.5f;
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

// Simplex noise in three dimensions.
//
// The caves are cut out of a three-dimensional volume, not drawn on a wall,
// and this is the knife. The third axis is what lets a tunnel pinch shut and
// open out again as you follow it, and what lets two tunnels that would have
// run side by side forever drift together and join.
static float simplex3(float x, float y, float z, uint32_t seed) {
    constexpr float kSkew = 1.0f / 3.0f;
    constexpr float kUnskew = 1.0f / 6.0f;

    const float skew = (x + y + z) * kSkew;
    const int i = static_cast<int>(std::floor(x + skew));
    const int j = static_cast<int>(std::floor(y + skew));
    const int k = static_cast<int>(std::floor(z + skew));

    const float unskew = (static_cast<float>(i) + static_cast<float>(j) +
                           static_cast<float>(k)) * kUnskew;
    const float x0 = x - (static_cast<float>(i) - unskew);
    const float y0 = y - (static_cast<float>(j) - unskew);
    const float z0 = z - (static_cast<float>(k) - unskew);

    // Which of the six triangles around the point we are in.
    int i1, j1, k1, i2, j2, k2;
    if (x0 >= y0) {
        if (y0 >= z0)      { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
        else               { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
    } else {
        if (y0 < z0)       { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
        else if (x0 < z0)  { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
        else               { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    }

    const float x1 = x0 - static_cast<float>(i1) + kUnskew;
    const float y1 = y0 - static_cast<float>(j1) + kUnskew;
    const float z1 = z0 - static_cast<float>(k1) + kUnskew;
    const float x2 = x0 - static_cast<float>(i2) + 2.0f * kUnskew;
    const float y2 = y0 - static_cast<float>(j2) + 2.0f * kUnskew;
    const float z2 = z0 - static_cast<float>(k2) + 2.0f * kUnskew;

    const int s = static_cast<int>(seed);
    float total = 0.0f;

    float t = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
    if (t > 0.0f) {
        t *= t;
        const float* g = kGrad3[hashBits(i, j, k + s) % 12u];
        total += t * t * (g[0] * x0 + g[1] * y0 + g[2] * z0);
    }
    t = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
    if (t > 0.0f) {
        t *= t;
        const float* g = kGrad3[hashBits(i + i1, j + j1, k + k1 + s) % 12u];
        total += t * t * (g[0] * x1 + g[1] * y1 + g[2] * z1);
    }
    t = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
    if (t > 0.0f) {
        t *= t;
        const float* g = kGrad3[hashBits(i + i2, j + j2, k + k2 + s) % 12u];
        total += t * t * (g[0] * x2 + g[1] * y2 + g[2] * z2);
    }
    return total * 32.0f;
}

// Smooth noise in three dimensions, between 0 and 1.
float noise3(float x, float y, float z, uint32_t seed) {
    const float n = simplex3(x, y, z, seed) * 0.5f + 0.5f;
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
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

float fbm3(float x, float y, float z, uint32_t seed, int octaves) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        total += noise3(x * frequency, y * frequency, z * frequency, seed + static_cast<uint32_t>(i) * 7919u) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return total / norm;
}

// Ridged noise in one dimension, as a multifractal.
//
// Ordinary noise makes round humps. Fold it - the same trick as folding a
// piece of paper - and it makes crests instead: a line that comes to a point
// at the top and runs down on both sides. That is what a mountain range looks
// like, so the ranges are built out of this rather than out of plain noise.
//
// A multifractal is more than one folded octave stacked together, and each
// octave is scaled by the one below it. Where the coarse fold already made a
// crest, the fine fold is allowed to sharpen it; where the coarse fold was
// flat, the fine fold is not allowed to make anything at all. That is what
// turns a smooth mountain into a jagged ridge, with the detail running along
// the crest instead of speckling the whole of it.
float ridged1(float x, uint32_t seed, int octaves) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    float weight = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        float n = noise1(x * frequency, seed + static_cast<uint32_t>(i) * 7919u);
        n = 1.0f - std::fabs(2.0f * n - 1.0f);   // fold: the peaks are at the edges
        n = n * n;                                // and square them, so the crests are sharp
        n *= weight;                             // only sharpen what is already a crest
        weight = std::min(1.0f, n * 2.5f);       // and hand the sharpness on downwards
        total += n * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return total / norm;
}

// The land at one column: where the ground starts, how deep the layers under
// it go, how much snow lies on top of it, and what the weather has left on
// the surface.
struct Land {
    int surface;    // the first solid row
    int profile;    // the first row of the ground itself, under snow and rubble
    int snowRows;   // how many rows of snow sit on top of it
    int screeRows;  // how many rows of rubble have slid down the cliffs above
    int soilRows;   // how deep the topsoil goes
    int dirtRows;   // how deep the packed dirt goes under the soil
    Mat topMat;     // what the ground surface is made of
    Mat screeMat;   // what the rubble is made of
};

// -- What the ground is made of ---------------------------------------------

// The climate of a place: how cold it is and how wet it is.
//
// The cold comes from a slow wave running across the world, and the wetness
// from a second, slower wave that does not care about the height at all.
// Both are maps over the world rather than over the rock, so a whole region
// shares one climate and the rock under it was laid down in that climate.
static float climateTemperature(int worldX) {
    return fbm1(static_cast<float>(worldX) / 8000.0f, kWorldSeed + 201u, 3);
}

static float climateMoisture(int worldX, int y) {
    return fbm2(static_cast<float>(worldX) / 6000.0f,
                static_cast<float>(y) / 4000.0f, kWorldSeed + 211u, 2);
}

// The rock's bands and its climate, sampled down a column.
//
// A band of rock is a hundred rows deep and does not change in eight rows, so
// the noise is read every kStrataStep rows and joined up in between. The
// ground is the biggest thing in the world, and this is what keeps reading it
// affordable.
struct Strata {
    static constexpr int kStep = 16;

    std::vector<float> band;
    std::vector<float> temperature;
    std::vector<float> moisture;

    void reset(int height) {
        const int count = height / kStep + 2;
        band.assign(count, 0.0f);
        temperature.assign(count, 0.0f);
        moisture.assign(count, 0.0f);
    }

    // Read the noise down one column.
    void sampleColumn(int worldX, int surface) {
        const float fx = static_cast<float>(worldX);
        const float cold = climateTemperature(worldX);
        for (size_t i = 0; i < band.size(); ++i) {
            const float fy = static_cast<float>(static_cast<int>(i) * kStep);

            // Domain warping: the rock is read at a point that another noise
            // has pushed about, so the bands fold and tilt the way real rock
            // folds instead of lying in flat stripes. Two rounds of it, so
            // the folds fold again.
            const float qx = fbm2(fx / 200.0f, fy / 150.0f, kWorldSeed + 10u, 2);
            const float qy = fbm2(fx / 200.0f, fy / 150.0f, kWorldSeed + 11u, 2);
            const float rx = fbm2((fx + qx * 60.0f + 30.0f) / 200.0f,
                                  (fy + qy * 60.0f + 15.0f) / 150.0f, kWorldSeed + 12u, 2);
            const float ry = fbm2((fx + qx * 60.0f - 30.0f) / 200.0f,
                                  (fy + qy * 60.0f - 15.0f) / 150.0f, kWorldSeed + 13u, 2);
            band[i] = fbm2((fx + rx * 80.0f) / 220.0f,
                           (fy + ry * 80.0f) / 140.0f, kWorldSeed + 31u, 3);
            temperature[i] = cold;
            moisture[i] = climateMoisture(worldX, static_cast<int>(i) * kStep);
        }
        (void)surface;
    }
};

// Read a sampled value back, joined up in between the samples.
static float readSample(const std::vector<float>& samples, int y) {
    const float f = static_cast<float>(y) / static_cast<float>(Strata::kStep);
    const int i = static_cast<int>(std::floor(f));
    const float t = f - static_cast<float>(i);
    const float a = samples[static_cast<size_t>(i)];
    const float b = samples[static_cast<size_t>(i) + 1];
    return a + (b - a) * t;
}

// The rock under the surface at a given depth. Soil on top, then packed dirt,
// then stone with bands of limestone and basalt mixed through it.
//
// The boundaries between the layers wander, because the depths they come from
// wander. The rock bands are wide and flat, the way a layer of rock is, and
// they tilt and bend rather than sitting in neat blobs.
//
// The climate decides what the rock is: a hot dry place lays down sand, a
// warm wet one lays down clay, and a cold one lays down plain limestone.
Mat layerMaterial(const Land& land, int depth, float band, float temp, float moist) {
    if (depth <= land.soilRows) return land.topMat;
    if (depth <= land.soilRows + land.dirtRows) return Mat::PackedDirt;
    if (band > 0.72f) return Mat::Basalt;
    if (band < 0.22f) {
        if (moist > 0.68f && temp > 0.35f) return Mat::Clay;
        return Mat::Limestone;
    }
    if (moist < 0.34f && temp > 0.58f && band > 0.45f && band < 0.55f) return Mat::Sand;
    return Mat::Stone;
}

// The cave field at a spot: one number. Above the threshold it is rock, below
// it, air.
//
// Caves are a property of the world, not of the chunk, so one can run across a
// chunk boundary without a wall appearing in the seam.
//
// They are DEEP: nothing within the top of the ground is hollowed out, so the
// roof over a cave is thick enough to walk on and a pond on the surface has
// solid rock under it.
//
// They are BIG: the noise that carves them is slow and wide, so a cavern is
// hundreds of cells across rather than a pocket of air in the rock.
//
// They are THREE-DIMENSIONAL. A cave system is not a shape on a wall, it is a
// hole through a volume, and this generator cuts the volume. The third axis
// drifts slowly along the world, so a tunnel pinches shut and opens out again
// as you follow it, and two tunnels that would have run side by side forever
// drift together and join.
//
// The field is read in DEPTH below the surface rather than in absolute rows,
// so a cavern is the same distance under the ground everywhere. A flat layer
// instead would come close to the surface under a hill and sink out of reach
// under a valley.
float caveField(int worldX, int y, int surface) {
    const float depth = static_cast<float>(y - surface);
    const float fx = static_cast<float>(worldX);

    // The third axis: a slow drift along the world. This is the slice of the
    // cave volume we are looking at. Because the slice moves as you travel,
    // the tunnels are not an extrusion of one flat pattern - they breathe.
    const float slice = fbm1(fx / 1500.0f, kWorldSeed + 71u, 2) * 5.0f;

    // The warp. The cave is read at a point that another noise has pushed
    // about, so it winds and curls instead of running in round blobs. This is
    // what joins one cavern to the next.
    //
    // One octave, and a gentle push. A hard warp folds the field back on
    // itself, and a field folded back on itself has a lot of small islands
    // near the threshold - which is exactly the swiss cheese that was there
    // before. The connecting comes from the drifting slice instead: it moves
    // the whole cavern sideways as you travel, so passages that would have
    // run side by side forever meet and join.
    const float wx = (noise3(fx / 380.0f, depth / 340.0f, slice / 320.0f, kWorldSeed + 43u) - 0.5f) * kCaveWarp;
    const float wy = (noise3(fx / 360.0f, depth / 320.0f, slice / 300.0f, kWorldSeed + 47u) - 0.5f) * kCaveWarp;
    const float wz = (noise3(fx / 340.0f, depth / 300.0f, slice / 280.0f, kWorldSeed + 49u) - 0.5f) * kCaveWarp;

    // The cave itself: slow, wide noise, so a cavern is a cavern.
    return fbm3((fx + wx) / kCaveWidth,
                (depth + wy) / kCaveHeight,
                (slice + wz) / kCaveSlice,
                kWorldSeed + 41u, kCaveOctaves);
}

// How much noise a cell needs before it counts as cave. The bar drops as the
// rock goes deeper, so the caves grow as they go down, and it rises again in
// the deepest rock, which is the solid floor the world stands on.
float caveThreshold(int worldX, int y, int surface) {
    (void)worldX;
    const float depth = static_cast<float>(y - surface);
    const float belt = std::min(1.0f, std::max(0.0f, (depth - 60.0f) / 400.0f));
    const float basement = std::min(1.0f, std::max(0.0f, (depth - 900.0f) / 700.0f));
    return kCaveThreshold - kCaveDepthBias * belt + kCaveBasement * basement;
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
// The land is four things stacked on top of each other:
//
//   * a great swell, thousands of columns wide, that makes the whole world
//     rise and fall;
//   * broad rolling hills on top of that;
//   * mountain ranges standing above both, with snow on their tops; and
//   * channels cut down through the hills, where the rain ran.
//
// Then the weather gets at it. Rock that stands too steeply does not stand:
// it slides down until the pile it makes is no steeper than loose rock can
// hold, and what comes off the cliff ends up as a fan of rubble at its foot.

// How many columns the mountain height is averaged over. The raw height curve
// has sharp kinks where the noise folds, and a seven-row cliff in the ground
// every few hundred columns is neither a hill nor a mountain - it is a wall,
// and the snow on top of it slides off. Averaging takes the kink out and
// leaves a slope, but only a little of it: a range whose faces are all gentle
// is a lump, not a range.
constexpr int kMountainSmoothing = 4;

// How far a fan of rubble reaches from the cliff it came off. Loose rock
// piles up at about one row down for every row across, so a cliff twenty
// rows high has a fan that runs twenty columns out from its foot.
constexpr int kScreeReach = 40;

// The deepest fan of rubble there can be. Past this the cliff was too big to
// be a cliff, and the rock under it did not come down in one piece.
constexpr int kScreeMax = 16;

// How steep a fan of rubble can stand. Loose rock slides until the pile is
// no steeper than the angle of repose, which for rubble is about one row
// down for every row and a half across.
constexpr float kScreeRepose = 0.67f;

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

// The height of the ground at a column: the shape of the land on its own,
// with no snow on it and no rubble under it.
//
// The channels are cut by the same trick the caves use. Folding the noise
// puts its middle in long winding lines, and long winding lines are where
// running water goes. The ground is cut down along them, and the deeper the
// line cuts the wider the valley it leaves behind, which is what a valley cut
// by a stream looks like.
static float profileHeight(int worldX) {
    const float x = static_cast<float>(worldX);

    // The great swell: the whole world rising and falling over thousands of
    // columns, so the land is never the same height two valleys apart.
    const float swell = fbm1(x / 3200.0f, kWorldSeed + 5u, 2) * 2.0f - 1.0f;

    // Broad rolling hills on top of that.
    const float hills = fbm1(x / 700.0f, kWorldSeed + 11u, 3) * 2.0f - 1.0f;

    const float mountain = mountainHeightAt(worldX);

    // The channels the rain cut.
    const float flow = noise1(x / 620.0f, kWorldSeed + 67u);
    const float fold = 1.0f - std::fabs(2.0f * flow - 1.0f);
    const float channel = std::max(0.0f, fold - 0.88f) / 0.12f;
    const float eroded = channel * channel * 30.0f *
                         fbm1(x / 1600.0f, kWorldSeed + 68u, 2);

    // Lowland level. Everything else is measured up from here.
    return 330.0f - swell * 30.0f - hills * 26.0f - mountain * 150.0f + eroded;
}

// The height of the ground at a column, remembered.
//
// How much rubble slides down a cliff depends on the shape of the ground a
// little way either side, so the same column gets asked for over and over.
// The answers are kept in a small cache of a fixed size, so a long walk
// through the world cannot fill the machine's memory.
int profileAt(int worldX) {
    static constexpr int kSlots = 8192;   // a power of two, so the index is cheap
    static std::vector<int> keys;
    static std::vector<int> values;
    if (keys.empty()) {
        keys.assign(kSlots, INT_MIN);
        values.assign(kSlots, 0);
    }

    const size_t slot = static_cast<size_t>(static_cast<uint32_t>(worldX) * 2654435761u) &
                        static_cast<size_t>(kSlots - 1);
    if (keys[slot] == worldX) return values[slot];

    const int row = static_cast<int>(std::lround(profileHeight(worldX)));
    keys[slot] = worldX;
    values[slot] = row;
    return row;
}

// How many rows of rubble have slid down the cliffs above this column.
//
// Loose rock does not stand in a vertical face. It slides until the pile it
// makes is no steeper than the angle of repose, and what comes off the cliff
// piles up at the foot of it. So every cliff has a fan of rubble below it,
// and the fan is as deep as the cliff is tall.
int screeRowsAt(int worldX) {
    const int here = profileAt(worldX);
    int deepest = 0;
    for (int d = 1; d <= kScreeReach; ++d) {
        // A cliff on the left: the ground there stands this many rows above
        // us. Only the part of it that is steeper than the rubble can hold
        // comes down, and the rest has already spread out. So the fan at this
        // column is the height of the cliff less what the rubble has already
        // run out over the ground between.
        const int left = here - profileAt(worldX - d) - static_cast<int>(d * kScreeRepose);
        const int right = here - profileAt(worldX + d) - static_cast<int>(d * kScreeRepose);
        deepest = std::max(deepest, std::max(left, right));
    }
    return std::min(deepest, kScreeMax);
}

// The land at one column: where the ground starts, how deep the layers under
// it go, how much snow lies on top of it, and what the weather has left on
// the surface.
static Land landAt(int worldX) {
    const float x = static_cast<float>(worldX);

    Land land;

    // The shape of the ground, and the rubble that has slid down it.
    const int profile = profileAt(worldX);
    land.screeRows = screeRowsAt(worldX);

    // The climate of the country this column is in.
    const float temp = climateTemperature(worldX);
    const float moist = climateMoisture(worldX, profile);

    // How high this column is. High ground is colder and thinner than the
    // country around it, and the soil does not reach up a mountain: what is
    // on a mountain is rock and rubble, whatever the weather is doing in the
    // valley below.
    const float mountain = mountainHeightAt(worldX);
    const bool high = mountain > 0.08f;

    // How deep the soil and the packed dirt go under the surface.
    //
    // These wander from column to column instead of stepping. A depth that
    // changes every few columns draws a staircase across the world, and a
    // staircase is the surest sign that a machine made something.
    //
    // On a mountain the soil is thin, and the rock comes up close under the
    // turf: a mountain with forty rows of dirt under it is a lump of earth
    // with a bump on top, not a mountain.
    const float soil = noise1(x / 38.0f, kWorldSeed + 61u);
    const float dirt = noise1(x / 74.0f, kWorldSeed + 62u);
    land.soilRows = 1 + static_cast<int>(soil * 3.0f);
    land.dirtRows = high ? 1 + static_cast<int>(dirt * 5.0f)
                         : 12 + static_cast<int>(dirt * 26.0f);

    // What the ground surface is made of.
    //
    // A hot dry country is sand. A wet one is mud. A warm damp one is clay.
    // A cold one is frozen ground. A mountain is bare rock. Everything else
    // is ordinary soil.
    if (high) {
        land.topMat = Mat::Stone;
        land.screeMat = Mat::Gravel;
    } else if (temp > 0.58f && moist < 0.36f) {
        land.topMat = Mat::Sand;
        land.screeMat = Mat::Sand;
    } else if (moist > 0.64f) {
        land.topMat = Mat::Mud;
        land.screeMat = Mat::Gravel;
    } else if (temp > 0.52f && moist > 0.52f) {
        land.topMat = Mat::Clay;
        land.screeMat = Mat::Gravel;
    } else if (temp < 0.28f) {
        land.topMat = Mat::PackedDirt;
        land.screeMat = Mat::Gravel;
    } else {
        land.topMat = Mat::Soil;
        land.screeMat = Mat::Gravel;
    }

    // Snow belongs to the mountain tops and to nothing else.
    //
    // It is decided by the mountain itself and by how cold the country is,
    // not by a line drawn across the map. A fixed line puts snow on a hill
    // in the lowlands whenever the ground happens to reach it, and leaves a
    // bare peak whenever the hills happen to be high there - which is exactly
    // how snow ended up all over the surface instead of up on the mountains.
    const float cold = 1.0f - temp;
    const float line = 0.42f - cold * 0.22f;   // the height the snow starts at
    const float m = mountain - line;
    if (m > 0.48f) land.snowRows = 5;
    else if (m > 0.38f) land.snowRows = 4;
    else if (m > 0.26f) land.snowRows = 3;
    else if (m > 0.13f) land.snowRows = 2;
    else if (m > 0.0f) land.snowRows = 1;
    else land.snowRows = 0;

    // The edge of the snow is ragged, not a clean line. The depth of the
    // cover wanders by a row or two, so the last of the snow melts away in
    // patches instead of stopping dead along a boundary.
    if (land.snowRows > 0) {
        const float fray = noise1(x / 13.0f, kWorldSeed + 63u);
        if (fray < 0.34f) --land.snowRows;
        else if (fray > 0.72f) ++land.snowRows;
    }

    // The ground itself starts under the snow and the rubble.
    land.profile = profile;
    land.surface = profile - land.screeRows - land.snowRows;
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
    //    dirt and rock underneath, snow on the mountain tops, rubble at the
    //    foot of the cliffs, bedrock on the floor.
    Strata strata;
    strata.reset(height);
    for (int lx = 0; lx < Chunk::WIDTH; ++lx) {
        const int worldX = baseX + lx;
        const Land land = landAt(worldX);
        const int surface = land.surface;
        strata.sampleColumn(worldX, land.profile);
        for (int y = 0; y < height; ++y) {
            Mat m = Mat::Air;
            if (y >= surface) {
                if (y >= height - 3) {
                    m = Mat::Bedrock;                    // the floor of the world
                } else if (y < surface + land.snowRows) {
                    m = Mat::Snow;                       // snow on the mountain tops
                } else if (y < surface + land.snowRows + land.screeRows) {
                    m = land.screeMat;                   // rubble at the foot of a cliff
                } else {
                    m = layerMaterial(land, y - land.profile,
                                      readSample(strata.band, y),
                                      readSample(strata.temperature, y),
                                      readSample(strata.moisture, y));
                }
            }
            const size_t cell = chunk.index(lx, y);
            chunk.cells[cell] = static_cast<uint8_t>(m);
            chunk.life[cell] = 0;
            chunk.mass[cell] = 0.0f;
        }
    }

    // 2. Caves, hollowed out of the rock.
    //
    //    The field is read every few rows and filled in between, because it is
    //    smooth over hundreds of rows: the shape is the same as reading it
    //    cell by cell, at a quarter of the cost.
    for (int lx = 0; lx < Chunk::WIDTH; ++lx) {
        const int worldX = baseX + lx;
        const int surface = groundRow(worldX);
        const int bottom = height - 3;

        int y = surface + kCaveRoof;
        float previous = caveField(worldX, y, surface);
        while (y < bottom) {
            const int nextY = std::min(y + kCaveStep, bottom);
            const float next = caveField(worldX, nextY, surface);
            for (int row = y; row < nextY; ++row) {
                // Smoothed between the two samples, so the wall of the cavern
                // does not come out in steps.
                const float t = static_cast<float>(row - y) / static_cast<float>(nextY - y);
                const float s = t * t * (3.0f - 2.0f * t);
                const float field = previous + (next - previous) * s;
                if (field <= caveThreshold(worldX, row, surface)) continue;
                const size_t cell = chunk.index(lx, row);
                chunk.cells[cell] = static_cast<uint8_t>(Mat::Air);
                chunk.mass[cell] = 0.0f;
            }
            previous = next;
            y = nextY;
        }
    }

    // 3. Pockets of loose material buried in the ground: sand, gravel, clay.
    //    They are big - a pocket you can dig a hole in - and their outline is
    //    lumpy, the way a pocket of loose sand in the rock is. A smooth oval
    //    with an even edge looks like it was stamped out.
    static const Mat kPocketMaterials[3] = {Mat::Sand, Mat::Gravel, Mat::Clay};
    for (int i = 0; i < 6; ++i) {
        const int kindHash = static_cast<int>(hash01(index, i, 91));
        const float temp = climateTemperature(baseX + 10);
        const float moist = climateMoisture(baseX + 10, 400);
        Mat m = Mat::Gravel;
        if (temp > 0.62f && moist < 0.38f) m = Mat::Sand;
        else if (moist > 0.58f && temp > 0.4f) m = Mat::Clay;
        else m = kPocketMaterials[kindHash % 3];
        const int lx = 10 + static_cast<int>(hash01(index, i, 92) * (Chunk::WIDTH - 20.0f));
        const int halfWidth = 8 + static_cast<int>(hash01(index, i, 93) * 12.0f);
        const int halfHeight = 6 + static_cast<int>(hash01(index, i, 94) * 8.0f);
        // Buried well down, in the deep rock where the caves are.
        const int centreY = groundRow(baseX + lx) + 60 +
                            static_cast<int>(hash01(index, i, 95) * 300.0f);
        for (int dy = -halfHeight; dy <= halfHeight; ++dy) {
            for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
                const float nx = static_cast<float>(dx) / static_cast<float>(halfWidth);
                const float ny = static_cast<float>(dy) / static_cast<float>(halfHeight);

                // The edge wanders. The lumpiness is on the scale of a few
                // cells, so the pocket has a broken outline.
                const float lump = noise1(static_cast<float>(dx) * 0.37f +
                                              static_cast<float>(dy) * 0.53f +
                                              static_cast<float>(lx) * 0.11f +
                                              static_cast<float>(i) * 7.0f,
                                          kWorldSeed + 81u);
                const float limit = 0.78f + lump * 0.42f;
                if (nx * nx + ny * ny > limit * limit) continue;

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
    //    The pond is a bowl dug into the ground, and the bowl is round: the
    //    floor dips away in the middle and comes back up at the shore, so the
    //    water is deep in the middle and tapers away to nothing at both ends.
    //    A pond dug as a rectangle - a flat floor, a straight wall at each end
    //    - is the most machine-made thing in the world, and it was.
    //
    //    The water in it is level from end to end, because water is level. The
    //    bed under it is rock, because the ground down there might be hollow -
    //    a cave under the pond would swallow the water whole.
    //
    //    It only appears where the ground is flat across the whole span. A
    //    bowl dug into a slope comes out as a stepped pit, and the water ends
    //    up pooled in the deepest step with the shallow ones left dry.
    int pondLeft = 0;
    int pondRight = -1;
    {
        const int lx = 24 + static_cast<int>(hash01(index, 0, 101) * (Chunk::WIDTH - 48.0f));
        const int half = 11 + static_cast<int>(hash01(index, 0, 102) * 11.0f);
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
            // The water's surface, and how deep the bowl goes under it.
            //
            // The surface has to be below the lowest ground in the span, or
            // the water would find a way out over it. The ground is allowed
            // to move three rows, so the water sits at least four rows down.
            const int base = groundRow(baseX + lx);
            const int waterDepth = 4 + static_cast<int>(hash01(index, 0, 103) * 3.0f);
            const int bowlDepth = 3 + static_cast<int>(hash01(index, 0, 104) * 6.0f);
            const int bedRows = 3;
            const int surface = base + waterDepth;

            for (int l = spanLeft; l <= spanRight; ++l) {
                const float t = static_cast<float>(l - lx) / static_cast<float>(half);

                // The shape of the bowl: deepest in the middle, up at the shore.
                float shape = 1.0f - t * t;
                if (shape < 0.0f) shape = 0.0f;
                shape = std::pow(shape, 0.6f);

                // The shore is not a smooth curve either.
                const float wobble = (noise1(static_cast<float>(baseX + l) / 9.0f,
                                             kWorldSeed + 71u) - 0.5f) * 1.8f;

                const int floor = surface + static_cast<int>(std::lround(bowlDepth * shape + wobble));
                if (floor <= surface) continue;   // this end of the pond is dry

                // Dig the bowl out, but never above the water's level: the
                // ground between the shore and the water stays as it was.
                const int g = groundRow(baseX + l);
                const int digFrom = std::max(g, surface);
                for (int y = digFrom; y < floor; ++y) {
                    if (y < 0 || y >= height) continue;
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::Air);
                    chunk.mass[cell] = 0.0f;
                }
                // Fill it.
                for (int y = surface; y < floor; ++y) {
                    if (y < 0 || y >= height) continue;
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::Water);
                    chunk.mass[cell] = 1.0f;
                }
                // The bed of the basin, so the pond has a bottom to stand on.
                for (int y = floor; y < floor + bedRows; ++y) {
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

    // 5. A lava pool deep down, in a stone chamber, with sulfur crust in the
    //    rock around it. The lava lies in the bottom of the chamber with a
    //    level surface, and the chamber is a hole in the rock with a lumpy
    //    outline - not a box with straight walls and a flat lid. Break the
    //    chamber and the lava finds the sulfur.
    {
        const int lx = 30 + static_cast<int>(hash01(index, 0, 111) * (Chunk::WIDTH - 60.0f));
        const int half = 8 + static_cast<int>(hash01(index, 0, 113) * 7.0f);
        const int halfHeight = 6 + static_cast<int>(hash01(index, 0, 114) * 5.0f);
        const int lavaDepth = 3 + static_cast<int>(hash01(index, 0, 115) * 3.0f);

        // The middle of the chamber, deep down, but never so deep that it runs
        // into the floor of the world.
        const int mid = std::min(groundRow(baseX + lx) + 90 +
                                     static_cast<int>(hash01(index, 0, 112) * 170.0f),
                                 height - 40);

        // The lava's surface: one level across the whole chamber, because
        // lava is level like water.
        const int lavaTop = mid + halfHeight - lavaDepth;

        // How far the rock is hollowed in this column, measured out from the
        // middle. The outline is lumpy, so the chamber is not an oval either.
        auto hollow = [&](int dx) -> int {
            const float t = static_cast<float>(dx) / static_cast<float>(half);
            float s = 1.0f - t * t;
            if (s <= 0.0f) return 0;
            s = std::sqrt(s);
            const float lump = noise1(static_cast<float>(dx) / 4.0f +
                                          static_cast<float>(lx) * 0.21f,
                                      kWorldSeed + 91u);
            const int h = static_cast<int>(std::lround(halfHeight * s * (0.75f + lump * 0.5f)));
            return h < 2 ? 2 : h;
        };

        // The chamber itself, and the shell of rock around it.
        for (int dx = -half - 2; dx <= half + 2; ++dx) {
            const int l = lx + dx;
            if (l < 0 || l >= Chunk::WIDTH) continue;
            const int h = hollow(dx);
            const int outer = halfHeight + 2;
            const int top = mid - (h > 0 ? h : outer);
            const int bottom = mid + (h > 0 ? h : outer);
            for (int y = top; y <= bottom; ++y) {
                if (y < 0 || y >= height - 3) continue;
                const bool inside = h > 0 && y >= mid - h && y <= mid + h;
                Mat m = Mat::Stone;
                if (inside) m = (y >= lavaTop) ? Mat::Lava : Mat::Air;
                const size_t cell = chunk.index(l, y);
                chunk.cells[cell] = static_cast<uint8_t>(m);
                chunk.mass[cell] = (m == Mat::Lava) ? 1.0f : 0.0f;
            }
        }

        // The sulfur crust in the rock around the lava, two cells out, so it
        // waits for the lava to break through rather than setting the chamber
        // off on the first tick.
        for (int dx = -half - 2; dx <= half + 2; ++dx) {
            const int l = lx + dx;
            if (l < 0 || l >= Chunk::WIDTH) continue;
            const int h = hollow(dx);
            const int lavaBottom = mid + h - 1;
            if (lavaBottom < lavaTop) continue;
            for (int y = lavaTop; y <= lavaBottom; ++y) {
                if (y < 0 || y >= height - 3) continue;
                for (const int side : {-2, +2}) {
                    const int sl = l + side;
                    if (sl < 0 || sl >= Chunk::WIDTH) continue;
                    const size_t cell = chunk.index(sl, y);
                    if (chunk.cells[cell] != static_cast<uint8_t>(Mat::Stone)) continue;
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::SulfurCrust);
                    chunk.mass[cell] = 0.0f;
                }
            }
        }

        // Any crust that ended up touching the lava would blow the chamber up
        // the moment it was made, so it goes back to being rock.
        for (int dx = -half - 3; dx <= half + 3; ++dx) {
            const int l = lx + dx;
            if (l < 0 || l >= Chunk::WIDTH) continue;
            for (int y = mid - halfHeight - 3; y <= mid + halfHeight + 3; ++y) {
                if (y < 0 || y >= height - 3) continue;
                const size_t cell = chunk.index(l, y);
                if (chunk.cells[cell] != static_cast<uint8_t>(Mat::SulfurCrust)) continue;
                const bool touchingLava =
                    (l > 0 && chunk.cells[chunk.index(l - 1, y)] == static_cast<uint8_t>(Mat::Lava)) ||
                    (l + 1 < Chunk::WIDTH &&
                     chunk.cells[chunk.index(l + 1, y)] == static_cast<uint8_t>(Mat::Lava)) ||
                    (y > 0 && chunk.cells[chunk.index(l, y - 1)] == static_cast<uint8_t>(Mat::Lava)) ||
                    (y + 1 < height - 3 &&
                     chunk.cells[chunk.index(l, y + 1)] == static_cast<uint8_t>(Mat::Lava));
                if (touchingLava) {
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::Stone);
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
