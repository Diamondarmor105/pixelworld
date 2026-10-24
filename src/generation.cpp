#include "generation.h"

#include "materials.h"
#include "structures.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <thread>
#include <vector>

// The seed of the whole world.
static uint32_t kWorldSeed = 0x9E3779B9u;

// The height cache used by profileAt. Its answers depend on the seed, so it is
// emptied whenever the seed changes.
constexpr int kProfileSlots = 65536;   // a power of two, so the index is cheap
static std::vector<int> profileKeys;
static std::vector<int> profileValues;

void setWorldSeed(uint32_t seed) {
    kWorldSeed = seed ? seed : 0x9E3779B9u;
    profileKeys.assign(kProfileSlots, INT_MIN);
    profileValues.assign(kProfileSlots, 0);
}
uint32_t worldSeed() { return kWorldSeed; }

static float volcanicWeightAt(float x);

namespace {

// A staircase that is continuous: flat treads at each multiple of `step`, with
// each riser rising smoothly over the last part of the step. A hard staircase
// jumps a whole step in one column, which reads as a misaligned height.
static float smoothStair(float h, float step) {
    const float q = h / step;
    const float n = std::floor(q);
    const float t = std::min(1.0f, std::max(0.0f, (q - n - 0.3f) / 0.4f));
    return (n + t * t * (3.0f - 2.0f * t)) * step;
}


// The seed of the whole world. One number decides everything: where the hills
// are, where the caves run, what is buried where. Randomized per run unless a
// seed is supplied through setWorldSeed().
// (Defined outside the anonymous namespace so main() can reach it.)
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
    Mat bodyMat;    // what the topsoil body is made of (sand under a desert)
    int skinRows;   // how many rows of the surface material lie under the top (grass)
    Mat altMat;     // the neighbouring biome's surface material, for the checker
    float altShare; // how much of the top layer takes altMat, 0..1
};

Land landAt(int worldX);


// -- What the ground is made of ---------------------------------------------

// The climate of a place: how cold it is and how wet it is.
//
// The cold comes from a slow wave running across the world, and the wetness
// from a second, slower wave that does not care about the height at all.
// Both are maps over the world rather than over the rock, so a whole region
// shares one climate and the rock under it was laid down in that climate.
// Climate: a 2D map. Temperature and moisture vary smoothly across the world
// in both directions, so biome borders are curves over the map rather than
// strips. Temperature falls with height above sea level, so highlands are
// colder. The seed shifts the whole map, so every world has its own climate.
constexpr int kSeaRow = 330;   // the row of sea level
// How large the biomes are: every climate and zone feature is this many times
// the size it was first built at.
constexpr float kBiomeScale = 2.8f;
// How much wider the land features are than before: the height curve is read
// at worldX / kWorldScale.
constexpr float kWorldScale = 1.6f;
// How much taller the land rises and sinks above and below sea level.
constexpr float kHeightScale = 1.5f;
// How deep the surface layers go: the topsoil and the packed dirt under it are
// this many times their original depth, before the rock starts.
constexpr float kSurfaceDepth = 3.0f;

static float climateTemperature(int worldX, int worldY) {
    const float fx = static_cast<float>(worldX), fy = static_cast<float>(worldY);
    const float field = fbm2(fx / (1400.0f * kBiomeScale), fy / (1000.0f * kBiomeScale), kWorldSeed + 201u, 3) - 0.5f;
    const float shift = (hash01(0, 7, static_cast<int>(kWorldSeed) + 1803) - 0.5f) * 0.2f;
    const float altitude = std::max(0.0f, static_cast<float>(kSeaRow) - fy);
    return std::min(1.0f, std::max(0.0f, 0.5f + field * 1.8f + shift - altitude / 1200.0f));
}

static float climateMoisture(int worldX, int worldY) {
    const float fx = static_cast<float>(worldX), fy = static_cast<float>(worldY);
    const float field = fbm2(fx / (1200.0f * kBiomeScale), fy / (900.0f * kBiomeScale), kWorldSeed + 211u, 3) - 0.5f;
    return std::min(1.0f, std::max(0.0f, 0.5f + field * 1.8f));
}

// The rock's bands and its climate, sampled down a column.
//
// A band of rock is a hundred rows deep and does not change in eight rows, so
// the noise is read every kStrataStep rows and joined up in between. The
// ground is the biggest thing in the world, and this is what keeps reading it
// affordable.
// Width of a fault block, in columns.
constexpr float kFaultSpacing = 1400.0f;

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
        for (size_t i = 0; i < band.size(); ++i) {
            const float fy = static_cast<float>(static_cast<int>(i) * kStep);

            // The rock is laid down in beds, and the beds are bent by the
            // weight of the rock above them. Domain warping folds the stack
            // so the beds tilt and buckle instead of lying in flat stripes,
            // but the warp stays smaller than a bed so the layers survive.
            const float qx = (fbm2(fx / 260.0f, fy / 200.0f, kWorldSeed + 10u, 2) - 0.5f) * 2.0f;
            const float qy = (fbm2(fx / 260.0f, fy / 200.0f, kWorldSeed + 11u, 2) - 0.5f) * 2.0f;
            const float rx = (fbm2((fx + qx * 50.0f) / 230.0f, (fy + qy * 50.0f) / 170.0f,
                                    kWorldSeed + 12u, 2) - 0.5f) * 2.0f;
            const float ry = (fbm2((fx - qy * 40.0f) / 230.0f, (fy - qx * 40.0f) / 170.0f,
                                    kWorldSeed + 13u, 2) - 0.5f) * 2.0f;

            // A slow, deep vertical gradient gives the beds their overall
            // order: different rock types at different depths, the way real
            // strata are stacked. The warped noise on top breaks them up.
            const float depthGrad = std::min(1.0f, std::max(0.0f, fy / 1800.0f));
            // Faults: the world is cut into blocks kFaultSpacing columns wide,
            // and each block shifts up or down and tilts on its own, so the rock
            // snaps at each block edge and tilts inside it. The block depends on
            // world position alone, so chunks agree.
            const int block = static_cast<int>(std::floor(fx / kFaultSpacing));
            const float blockShift = (hash01(block, 0, static_cast<int>(kWorldSeed) + 1700) - 0.5f) * 260.0f;
            const float blockTilt = (hash01(block, 1, static_cast<int>(kWorldSeed) + 1701) - 0.5f) * 0.34f;
            const float fromEdge = fx - static_cast<float>(block) * kFaultSpacing;
            const float faulted = fy + blockShift + blockTilt * fromEdge;
            const float beds = fbm2((fx + rx * 70.0f) / 200.0f,
                                    (faulted + ry * 70.0f) / 130.0f,
                                    kWorldSeed + 31u, 3);
            float b = beds * 0.7f + depthGrad * 0.3f;
            // Sharpen transitions so beds have crisp edges.
            b = b * b * (3.0f - 2.0f * b);
            band[i] = b;
            temperature[i] = climateTemperature(worldX, static_cast<int>(i) * kStep);
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
    if (depth <= 0 || depth <= land.skinRows) return land.topMat;
    if (depth <= land.soilRows) return land.bodyMat;
    if (depth <= land.soilRows + land.dirtRows) return Mat::PackedDirt;
    // Thin beds of other rock in the stone. The band value is a sawtooth that
    // runs from 0 to 1 through each bed, so the seams are a few rows wide
    // and the beds themselves are tens of rows deep.
    // Deep rock: basalt and obsidian dominate near the underworld.
    if (depth > 900) {
        if (band > 0.5f) return Mat::Basalt;
        return Mat::Obsidian;
    }
    // Obsidian veins deep down (volcanic glass).
    if (depth > 700 && band > 0.85f && band < 0.88f) return Mat::Obsidian;
    if (band > 0.90f) return Mat::Basalt;                       // dark seams
    if (band < 0.20f) {
        if (moist > 0.60f && temp > 0.30f) return Mat::Clay;    // wet beds
        return Mat::Limestone;                                  // pale beds
    }
    // Sandstone-like bands in dry regions.
    if (band > 0.40f && band < 0.46f && moist < 0.45f) return Mat::Sandstone;
    if (band > 0.62f && band < 0.66f && moist < 0.42f && temp > 0.45f) return Mat::Sandstone;
    if (band > 0.74f && band < 0.77f) return Mat::Gravel;
    // Granite: hard crystalline beds in the deeper rock; marble where it is hot.
    if (depth > 400 && band > 0.10f && band < 0.14f) return Mat::Granite;
    if (temp > 0.50f && band > 0.30f && band < 0.33f) return Mat::Marble;
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


// Carves the caves of one chunk. The result depends only on the chunk index,
// not on which other chunks happen to be built alongside it, so chunks agree
// at their seams however they were batched. The cleanup steps look at a fixed
// margin around the chunk, and the chunk writes only its own columns.
// Shallow caves: small holes, fairly common near the surface.
constexpr float kShallowThreshold = 0.13f;
// Deep caves: larger holes, rarer, so the bar is higher.
constexpr float kDeepThreshold = 0.20f;
// How far the cave fields are pushed about, in cells (the domain warp).
constexpr float kCaveWarp = 40.0f;
// Caves begin this many rows below the bottom of the packed dirt.
constexpr int kCaveTop = 20;

// Runs f(first, last) over the range [0, count), split across the machine's
// cores. The pieces touch different cells, and the noise they read is pure, so
// the result is the same as running them one after another.
template <class F>
void parallelRanges(int count, F f) {
    const int hardware = static_cast<int>(std::thread::hardware_concurrency());
    const int workers = std::max(1, std::min(hardware, count / 16));
    if (workers <= 1) {
        f(0, count);
        return;
    }
    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(workers));
    for (int w = 0; w < workers; ++w) {
        const int first = count * w / workers;
        const int last = count * (w + 1) / workers;
        pool.emplace_back([&f, first, last] { f(first, last); });
    }
    for (std::thread& t : pool) t.join();
}

void carveChunkCaves(World& world, int chunk) {
    constexpr int kMargin = 64;
    constexpr int kMinHole = 30;
    const int height = world.height();
    const int ownX0 = chunk * Chunk::WIDTH;
    const int winX0 = ownX0 - kMargin;
    const int winW = Chunk::WIDTH + 2 * kMargin;
    const int rows = height - 3;
    auto at = [&](int x, int y) { return static_cast<size_t>(y) * winW + (x - winX0); };

    // The buffers are kept from one chunk to the next, since carving runs for
    // every new chunk. They are shared by the worker threads below, so they are
    // plain statics rather than thread-local.
    static std::vector<uint8_t> mask;
    static std::vector<uint8_t> nextMask;
    static std::vector<uint8_t> seen;
    static std::vector<int> stack;
    static std::vector<int> piece;
    const size_t cells = static_cast<size_t>(winW) * rows;
    mask.assign(cells, 0);
    nextMask.resize(cells);
    seen.assign(cells, 0);

    // The ground and the volcanic stretch depend only on the column, so they
    // are read once per column here, before the threads start.
    // The caves start below the topsoil and the packed dirt, so each column
    // gives the row where its rock begins.
    std::vector<int> surfCol(winW);
    std::vector<int> startCol(winW);
    std::vector<float> tubeCol(winW);
    for (int i = 0; i < winW; ++i) {
        const Land land = landAt(winX0 + i);
        surfCol[i] = land.surface;
        startCol[i] = land.surface + land.soilRows + land.dirtRows + kCaveTop;
        tubeCol[i] = 1.0f + 1.4f * volcanicWeightAt(winX0 + i);
    }

    // Independent warp + beds, in the style of the strata but with its own
    // scales and seeds, so caves have the same irregular folded shape as the
    // rock layers without copying them. The mask is computed first.
    parallelRanges(winW, [&](int first, int last) {
        for (int i = first; i < last; ++i) {
            const int x = winX0 + i;
            const int surf = surfCol[i];
            const float tube = tubeCol[i];
            for (int y = startCol[i]; y < rows; ++y) {
                const float fx = static_cast<float>(x);
                const float fy = static_cast<float>(y);
                const float qx = (fbm2(fx / 280.0f, fy / 220.0f, kWorldSeed + 950u, 2) - 0.5f) * 2.0f;
                const float qy = (fbm2(fx / 280.0f, fy / 220.0f, kWorldSeed + 951u, 2) - 0.5f) * 2.0f;
                const float rx = (fbm2((fx + qx * 55.0f) / 250.0f, (fy + qy * 55.0f) / 190.0f,
                                        kWorldSeed + 952u, 2) - 0.5f) * 2.0f;
                const float ry = (fbm2((fx - qy * 45.0f) / 250.0f, (fy - qx * 45.0f) / 190.0f,
                                        kWorldSeed + 953u, 2) - 0.5f) * 2.0f;
                // Volcanic country stretches the field vertically (see tubeCol).
                const float beds = fbm2((fx + rx * 75.0f) / 220.0f,
                                        (fy + ry * 75.0f) / (140.0f * tube),
                                        kWorldSeed + 971u, 3);
                const float depthFactor = std::min(1.0f, std::max(0.0f, (fy - static_cast<float>(surf) - 20.0f) / 400.0f));
                const float thresh = 0.78f - depthFactor * 0.16f;   // looser deep
                if (beds > thresh) mask[at(x, y)] = 1;
            }
        }
    });

    // Morphological opening: erode twice, then dilate twice, 4-way. Removes
    // thin slivers while keeping round holes.
    auto step = [&](bool erode) {
        parallelRanges(winW, [&](int first, int last) {
            const int dx[4] = {-1, 1, 0, 0};
            const int dy[4] = {0, 0, -1, 1};
            for (int i = first; i < last; ++i) {
                const int x = winX0 + i;
                for (int y = 0; y < rows; ++y) {
                    bool hit = erode;
                    for (int d = 0; d < 4; ++d) {
                        const int nx = x + dx[d], ny = y + dy[d];
                        const bool n = nx >= winX0 && nx < winX0 + winW && ny >= 0 && ny < rows &&
                                       mask[at(nx, ny)] != 0;
                        hit = erode ? (hit && n) : (hit || n);
                    }
                    nextMask[at(x, y)] = hit ? 1 : 0;
                }
            }
        });
        mask.swap(nextMask);
    };
    step(true);
    step(true);
    step(false);
    step(false);

    // Fill pieces too small to be caves.
    for (int x = winX0; x < winX0 + winW; ++x)
        for (int y = 0; y < rows; ++y) {
            if (!mask[at(x, y)] || seen[at(x, y)]) continue;
            piece.clear();
            stack.clear();
            stack.push_back(x);
            stack.push_back(y);
            seen[at(x, y)] = 1;
            while (!stack.empty()) {
                const int cy = stack.back(); stack.pop_back();
                const int cx = stack.back(); stack.pop_back();
                piece.push_back(cx);
                piece.push_back(cy);
                const int dx[4] = {-1, 1, 0, 0};
                const int dy[4] = {0, 0, -1, 1};
                for (int d = 0; d < 4; ++d) {
                    const int nx = cx + dx[d], ny = cy + dy[d];
                    if (ny < 0 || ny >= rows) continue;
                    if (seen[at(nx, ny)] || !mask[at(nx, ny)]) continue;
                    seen[at(nx, ny)] = 1;
                    stack.push_back(nx);
                    stack.push_back(ny);
                }
            }
            if (piece.size() / 2 < static_cast<size_t>(kMinHole)) {
                for (size_t i = 0; i < piece.size(); i += 2) mask[at(piece[i], piece[i + 1])] = 0;
            }
        }

    // Write this chunk's own columns. Only rock becomes air; sand keeps support.
    for (int x = ownX0; x < ownX0 + Chunk::WIDTH; ++x)
        for (int y = 0; y < rows; ++y) {
            if (!mask[at(x, y)]) continue;
            if (world.at(x, y) == Mat::Water || world.at(x, y + 1) == Mat::Water) continue;
            if (world.at(x, y) == Mat::Air) continue;
            if (world.at(x, y) == Mat::Bedrock) continue;
            world.set(x, y, Mat::Air);
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
// Width of a fault block in columns, and of the mountain range fault blocks.
constexpr float kFaultBlock = 900.0f;
// Width of a tectonic plate cell in columns.
constexpr float kPlateWidth = 1500.0f;

static float rawMountainHeightAt(int worldX) {
    const float x = static_cast<float>(worldX);

    // Fault blocks: the land is cut into blocks kFaultBlock columns wide. Each
    // block tilts up on its west edge, where the rock breaks into a sheer
    // face, and falls away gently to the east. Each block has its own height.
    const int block = static_cast<int>(std::floor(x / kFaultBlock));
    const float f = (x - static_cast<float>(block) * kFaultBlock) / kFaultBlock;
    const float blockHeight = 0.55f + 0.45f * hash01(block, 0, static_cast<int>(kWorldSeed) + 1910);
    // The west face rises over the first part of the block rather than
    // dropping in one column, so the land is continuous at the block edge and
    // the face is a steep slope, not a wall. The rest of the block falls away
    // gently to the east.
    constexpr float kFace = 0.40f;
    const float face = std::min(1.0f, f / kFace);
    const float rise = face * face * (3.0f - 2.0f * face);
    float crest = blockHeight * rise * std::pow(1.0f - f, 2.5f);

    // Tectonic plates: the world is cut into plates along x, each with a random
    // point. Where two neighbouring points are almost equally near, the column
    // lies on a seam between plates, and the range rises there.
    const float plateCell = kPlateWidth;
    const int pc = static_cast<int>(std::floor(x / plateCell));
    float nearest = 1e9f;
    float second = 1e9f;
    for (int c = pc - 1; c <= pc + 1; ++c) {
        const float point = (static_cast<float>(c) + hash01(c, 0, static_cast<int>(kWorldSeed) + 1110)) * plateCell;
        const float d = std::fabs(x - point);
        second = d < nearest ? nearest : std::min(second, d);
        nearest = std::min(nearest, d);
    }
    const float seamness = 1.0f - std::min(1.0f, (second - nearest) / (0.35f * plateCell));
    crest *= 1.0f + 0.6f * seamness * seamness;

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
// One wave, centred on zero, so it can raise or lower the ground.
static float shapeWave(float x, float wavelength, uint32_t seed, int octaves) {
    return (fbm1(x / wavelength, seed, octaves) - 0.5f) * 2.0f;
}

// Biomes: each region of the world has its own height formula, so plains,
// hills, mountains and canyons have different shapes, not one shape scaled.
// Each biome sits at a point on a low-frequency zone map. Where two biomes
// meet, their heights are blended by weight across the overlap, so there is
// no cliff at a biome border.
enum class BiomeType { Plains = 0, Hills = 1, Mountains = 2, Canyons = 3, Desert = 4, Tundra = 5, Volcanic = 6, Scrubland = 7 };
constexpr int kBiomeCount = 8;
// The first four biomes are placed on the zone map; the desert is placed by
// temperature instead, so it appears where the country is hot.
constexpr int kZoneBiomes = 4;
constexpr float kBiomeCentre[kZoneBiomes] = {0.30f, 0.45f, 0.62f, 0.80f};
constexpr float kBiomeReach = 0.12f;   // how far a biome's influence spreads on the zone map

// How much each biome counts at this column. Each biome's weight is a smooth
// bell around its centre on the zone map. The bells never reach zero, so some
// biome always applies and the blend has no gaps, however the zone map varies.
// The weights sum to 1.
static void biomeWeights(int worldX, int worldY, float out[kBiomeCount]) {
    const float zone = fbm1(static_cast<float>(worldX) / (4000.0f * kBiomeScale), kWorldSeed + 500u, 2);
    float sum = 0.0f;
    for (int i = 0; i < kZoneBiomes; ++i) {
        const float d = (zone - kBiomeCentre[i]) / kBiomeReach;
        out[i] = std::exp(-d * d);
        sum += out[i];
    }
    for (int i = 0; i < kZoneBiomes; ++i) out[i] /= sum;

    const float hot = std::min(1.0f, std::max(0.0f, (climateTemperature(worldX, worldY) - 0.50f) / 0.28f));
    const float desert = hot * hot * (3.0f - 2.0f * hot);
    const float cold = std::min(1.0f, std::max(0.0f, (0.27f - climateTemperature(worldX, worldY)) / 0.10f));
    const float tundra = cold * cold * (3.0f - 2.0f * cold);
    // Volcanic country: patches of hot ground where a 2D noise runs high.
    const float volcanicNoise = fbm2(static_cast<float>(worldX) / (900.0f * kBiomeScale), static_cast<float>(worldY) / (700.0f * kBiomeScale), kWorldSeed + 950u, 2);
    // Volcanic ground sits in the warm band just below the desert, so the two
    // can win in different places instead of the desert always taking over.
    const float warm = std::min(1.0f, std::max(0.0f, (climateTemperature(worldX, worldY) - 0.42f) / 0.15f));
    const float volcanic = warm * std::min(1.0f, std::max(0.0f, (volcanicNoise - 0.45f) / 0.15f)) * (1.0f - desert);
    // Never negative: where the special biomes together cover everything, the
    // zone biomes simply step aside.
    const float zoneShare = std::max(0.0f, 1.0f - desert - tundra - volcanic);
    for (int i = 0; i < kZoneBiomes; ++i) out[i] *= zoneShare;
    out[kZoneBiomes] = desert;
    out[kZoneBiomes + 1] = tundra;
    out[kZoneBiomes + 2] = volcanic;
    // Scrubland: the ecotone between desert and plains. It peaks where the two
    // are equally strong and fades out in the core of either. Weights are then
    // renormalized so they still sum to 1.
    // A ring around the desert edge: it peaks where desert is half strong.
    out[kZoneBiomes + 3] = 4.0f * out[kZoneBiomes] * (1.0f - out[kZoneBiomes]);
    float total = 0.0f;
    for (int i = 0; i < kBiomeCount; ++i) total += out[i];
    if (total <= 1e-6f) { out[static_cast<int>(BiomeType::Plains)] = 1.0f; return; }
    for (int i = 0; i < kBiomeCount; ++i) out[i] /= total;
}

// How much of the volcanic biome this column is (0..1).
static float volcanicWeightAt(float x) {
    float w[kBiomeCount];
    biomeWeights(static_cast<int>(std::lround(x)), kSeaRow, w);
    return w[static_cast<int>(BiomeType::Volcanic)];
}


// The height offset (in rows, positive is lower) one biome gives a column.
// A smooth bump: 1 at its centre, falling linearly to 0 at `reach`.
static float tent(float d, float reach) {
    return std::max(0.0f, 1.0f - std::fabs(d) / reach);
}

// Scattered bumps (or hollows) placed one per cell of `cell` columns. Each
// cell's bump is summed with its neighbours', so the result is continuous
// across cell borders. `salt` picks which bumps these are.
static float scatter(float x, float cell, uint32_t salt, float height, float reachMin, float reachMax) {
    const int c0 = static_cast<int>(std::floor(x / cell));
    float total = 0.0f;
    for (int c = c0 - 1; c <= c0 + 1; ++c) {
        const float centre = (static_cast<float>(c) + hash01(c, 0, static_cast<int>(kWorldSeed) + static_cast<int>(salt))) * cell;
        const float reach = reachMin + (reachMax - reachMin) * hash01(c, 1, static_cast<int>(kWorldSeed) + static_cast<int>(salt));
        total += height * tent(x - centre, reach);
    }
    return total;
}

// Glacial valleys: U-shaped troughs cut into the high peaks where moisture
// is high enough for ice to have carved them. One candidate per 700-column
// cell; the curve (1 - (d/r)^2)^2 is zero at its rim with zero slope, so the
// trough has no ridge along its edge.
static float glacialValleys(float x) {
    constexpr float kCell = 700.0f;
    const int c0 = static_cast<int>(std::floor(x / kCell));
    float total = 0.0f;
    for (int c = c0 - 1; c <= c0 + 1; ++c) {
        const uint32_t salt = static_cast<uint32_t>(kWorldSeed) + 1060u;
        if (hash01(c, 0, static_cast<int>(salt)) > 0.5f) continue;   // half the cells have one
        const float centre = (static_cast<float>(c) + hash01(c, 1, static_cast<int>(salt))) * kCell;
        const float radius = 50.0f + 40.0f * hash01(c, 2, static_cast<int>(salt));
        const float depth = 60.0f + 30.0f * hash01(c, 3, static_cast<int>(salt));
        const float u = std::max(0.0f, 1.0f - ((x - centre) / radius) * ((x - centre) / radius));
        total += depth * u * u;
    }
    // Only where the land is high and wet: the ice needed to carve them.
    const float m = mountainHeightAt(static_cast<int>(std::lround(x)));
    const float wet = std::min(1.0f, std::max(0.0f, (climateMoisture(static_cast<int>(std::lround(x)), kSeaRow) - 0.30f) / 0.20f));
    const float high = std::min(1.0f, std::max(0.0f, (m - 0.02f) / 0.10f));
    return total * wet * high;
}

static float biomeOffset(BiomeType biome, float x) {
    switch (biome) {
        case BiomeType::Plains: {
            // Smooth flat ground, with broad shallow basins where moisture pools.
            const float fineRipple = shapeWave(x, 150.0f, kWorldSeed + 17u, 1) * 4.0f;
            const float basinMask = fbm1(x / 2000.0f, kWorldSeed + 101u, 2);
            const float depression = basinMask < 0.35f ? (0.35f - basinMask) * 15.0f : 0.0f;
            return -fineRipple - depression;
        }
        case BiomeType::Hills: {
            // Asymmetric cuestas: a long climb, then a steep drop.
            constexpr float kWaveLength = 300.0f;
            const float phase = x / kWaveLength - std::floor(x / kWaveLength);
            const float cuesta = phase < 0.75f
                ? std::sin(phase * 2.094f) * 40.0f
                : (1.0f - (phase - 0.75f) / 0.25f) * 40.0f;
            return -cuesta - shapeWave(x, 60.0f, kWorldSeed + 13u, 1) * 8.0f;
        }
        case BiomeType::Mountains: {
            // Fault-block ranges. The upper part of each range breaks into
            // jagged ridges, and glacial valleys cut the rock below them.
            const float m = mountainHeightAt(static_cast<int>(std::lround(x)));
            const float high = std::min(1.0f, std::max(0.0f, (m - 0.66f) / 0.34f));
            const float jag = ridged1(x / 300.0f, kWorldSeed + 95u, 3) * 50.0f * high;
            return -40.0f - m * 280.0f - jag + glacialValleys(x);
        }
        case BiomeType::Desert: {
            // Dune fields: the spacing and height change slowly across the
            // desert, from tall megadunes to low rippled flats. The spacing is
            // set by a warp on the phase, which keeps the phase rising with x
            // (dividing x by a varying wavelength would run it backwards far out).
            const float warp = (fbm1(x / 1200.0f, kWorldSeed + 500u, 2) - 0.5f) * 0.6f;
            const float cycles = x / 240.0f + warp;
            const float phase = cycles - std::floor(cycles);
            constexpr float kCrest = 0.85f;
            const float duneAmplitude = 25.0f + fbm1(x / 400.0f, kWorldSeed + 510u, 1) * 20.0f;
            const float dune = phase < kCrest
                ? duneAmplitude * (phase / kCrest) * (phase / kCrest)
                : duneAmplitude * (1.0f - (phase - kCrest) / (1.0f - kCrest));
            return dune + shapeWave(x, 35.0f, kWorldSeed + 80u, 1) * 2.5f;
        }
        case BiomeType::Tundra: {
            // Rolling permafrost ground, with frost-heaved bumps and pingos.
            const float hills = shapeWave(x, 600.0f, kWorldSeed + 90u, 2) * 20.0f;
            const float frostHeave = std::fabs(fbm1(x / 35.0f, kWorldSeed + 92u, 3)) * 5.0f;
            return -hills - frostHeave - scatter(x, 48.0f, 1040u, 14.0f, 14.0f, 24.0f);
        }
        case BiomeType::Volcanic: {
            // Ridged volcanic peaks. Near the top the summit is pressed down,
            // and craters are cut in with scatter.
            const float crest = ridged1(x / 800.0f, kWorldSeed + 95u, 3);
            const float calderaDip = crest > 0.72f ? (crest - 0.72f) * 90.0f : 0.0f;
            return -(crest * 130.0f - calderaDip) + scatter(x, 900.0f, 1050u, 40.0f, 40.0f, 70.0f);
        }
        case BiomeType::Scrubland: {
            // Low, uneven ground with little relief.
            return -shapeWave(x, 300.0f, kWorldSeed + 100u, 2) * 12.0f;
        }
        case BiomeType::Canyons: {
            // Trench walls that step down in flat rock benches.
            const float trench = std::fabs(shapeWave(x, 600.0f, kWorldSeed + 45u, 2)) * 80.0f;
            const float bench = smoothStair(trench, 14.0f);
            return 0.25f * trench + 0.75f * bench;
        }
    }
    return 0.0f;
}

static float profileHeight(int worldX) {
    // Every land feature is read at this column divided by kWorldScale, so the
    // whole land is stretched sideways: swells, ranges, dunes and plate seams
    // all grow together. The heights are not scaled.
    const float x0 = static_cast<float>(worldX) / kWorldScale;
    // Domain warp: every height input is read at a point nudged by a slow
    // noise offset, so ranges and valleys meander and pinch instead of running
    // in straight, parallel bands.
    const float x = x0 + (fbm1(x0 / 400.0f, kWorldSeed + 1900u, 2) - 0.5f) * 2.0f * 200.0f;

    // The swell is shared by every biome, so the continents still roll.
    const float swell = shapeWave(x, 5200.0f, kWorldSeed + 5u, 2);

    // The channels the rain cut, present everywhere.
    const float crestLine = ridged1(x / 520.0f, kWorldSeed + 67u, 2);
    const float channel = std::max(0.0f, crestLine - 0.62f) / 0.38f;
    const float eroded = channel * channel * 26.0f * fbm1(x / 1600.0f, kWorldSeed + 68u, 2);

    float w[kBiomeCount];
    biomeWeights(worldX, kSeaRow, w);
    float offset = 0.0f;
    for (int i = 0; i < kBiomeCount; ++i) {
        if (w[i] <= 0.0f) continue;
        offset += w[i] * biomeOffset(static_cast<BiomeType>(i), x);
    }

    // Continents: a very slow noise (thousands of columns across) raises broad
    // highlands and sinks broad basins. The basins are flattened into wide,
    // level floors. There is no water in them.
    const float cont = fbm1(x0 / 12000.0f, kWorldSeed + 1100u, 3) - 0.5f;
    const float basin = std::min(1.0f, std::max(0.0f, (-cont - 0.06f) / 0.12f));
    const float h = 330.0f - swell * 190.0f + offset * (1.0f - 0.75f * basin) + eroded
                  - cont * 180.0f + basin * 35.0f;
    float row = static_cast<float>(kSeaRow) + (h - static_cast<float>(kSeaRow)) * kHeightScale;
    // The top of the world is row 0. Peaks that climb above kCeilingRow bend
    // over into a rounded ceiling that approaches row 20 but never reaches it,
    // so the land always stays inside the world.
    constexpr float kCeilingRow = 50.0f;
    constexpr float kCeilingSoft = 30.0f;
    if (row < kCeilingRow) row = kCeilingRow - kCeilingSoft * (1.0f - std::exp((row - kCeilingRow) / kCeilingSoft));
    return row;
}

// The height of the ground at a column, remembered.
//
// How much rubble slides down a cliff depends on the shape of the ground a
// little way either side, so the same column gets asked for over and over.
// The answers are kept in a small cache of a fixed size, so a long walk
// through the world cannot fill the machine's memory.
int profileAt(int worldX) {
    if (profileKeys.empty()) setWorldSeed(kWorldSeed);

    const size_t slot = static_cast<size_t>(static_cast<uint32_t>(worldX) * 2654435761u) &
                        static_cast<size_t>(kProfileSlots - 1);
    if (profileKeys[slot] == worldX) return profileValues[slot];

    // Terracing: in arid provinces (hot and dry) the land is stepped into
    // benches and mesas. The height is blended toward its stepped value, so
    // the steps sit where the country is arid and nowhere else.
    const float raw = profileHeight(worldX);
    float biomeW[kBiomeCount];
    biomeWeights(worldX, kSeaRow, biomeW);
    // Terracing is for the dry country around the desert; the dunes keep
    // their own shape.
    const float arid = std::min(1.0f, std::max(0.0f, (climateTemperature(worldX, kSeaRow) - 0.55f) / 0.10f)) *
                       std::min(1.0f, std::max(0.0f, (0.40f - climateMoisture(worldX, kSeaRow)) / 0.10f)) *
                       (1.0f - biomeW[static_cast<int>(BiomeType::Desert)]);
    constexpr float kBench = 14.0f;   // rows per step
    const float stepped = smoothStair(raw, kBench);
    const int row = static_cast<int>(std::lround(raw + (stepped - raw) * arid));
    profileKeys[slot] = worldX;
    profileValues[slot] = row;
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
// The ground row averaged over nearby columns. The biome map is read at this
// row rather than the exact ground, so small bumps in the terrain cannot flip
// the climate (and with it the biome) from one column to the next.
static int smoothSurfaceRow(int worldX) {
    constexpr int kHalf = 24;
    int total = 0;
    for (int k = -kHalf; k <= kHalf; ++k) total += profileAt(worldX + k);
    return total / (2 * kHalf + 1);
}

// The top material a biome gives the ground surface.
static Mat topMatFor(int biome, float temp) {
    switch (static_cast<BiomeType>(biome)) {
        case BiomeType::Desert: return Mat::Sand;
        case BiomeType::Tundra: return Mat::Snow;
        case BiomeType::Volcanic: return Mat::Basalt;
        case BiomeType::Scrubland: return Mat::Gravel;
        case BiomeType::Hills: return Mat::Gravel;
        case BiomeType::Plains: return temp < 0.30f ? Mat::Snow : Mat::Grass;
        case BiomeType::Mountains: return Mat::Stone;
        default: return temp < 0.30f ? Mat::Snow : Mat::Soil;
    }
}

namespace {

Land landAt(int worldX) {
    const float x = static_cast<float>(worldX);

    Land land;

    // The surface is the height curve itself: no rubble, no snow cap, no
    // steep-face or hollow rules. What it is made of depends only on the
    // climate of the province it is in.
    const int profile = profileAt(worldX);
    const int smooth = smoothSurfaceRow(worldX);
    const float temp = climateTemperature(worldX, smooth);

    land.profile = profile;
    land.surface = profile;
    land.screeRows = 0;
    land.snowRows = 0;

    land.soilRows = 1 + static_cast<int>(noise1(x / 38.0f, kWorldSeed + 61u) * 3.0f);
    land.dirtRows = 12 + static_cast<int>(noise1(x / 74.0f, kWorldSeed + 62u) * 26.0f);

    // Cold country is snow, hot country is sand, everything else is dirt.
    // The desert is dune sand: a deep layer over a thin dirt floor.
    float biomeW[kBiomeCount];
    biomeWeights(worldX, smooth, biomeW);
    // The surface is the strongest biome on the 2D climate map.
    int dom = 0;
    for (int i = 1; i < kBiomeCount; ++i) if (biomeW[i] > biomeW[dom]) dom = i;
    int sec = dom == 0 ? 1 : 0;
    for (int i = 0; i < kBiomeCount; ++i)
        if (i != dom && biomeW[i] > biomeW[sec]) sec = i;
    // Where two biomes are close in strength, the second one's share is large
    // and the layers beneath mix the two as a checker (see generateChunk). Deep
    // inside one biome the share is zero, so the biome stays pure.
    const float share = biomeW[sec] / (biomeW[dom] + biomeW[sec] + 1e-6f);
    land.altShare = std::min(1.0f, std::max(0.0f, (share - 0.42f) / 0.16f));
    land.topMat = topMatFor(dom, temp);
    land.altMat = topMatFor(sec, temp);

    // Layer depths are a weighted average over every biome, so no single biome
    // "wins" a depth: the sand, snow and basalt layers thin out smoothly across
    // each border instead of stopping in a column.
    const float soil0 = 1.0f + noise1(x / 38.0f, kWorldSeed + 61u) * 3.0f;
    const float dirt0 = 12.0f + noise1(x / 74.0f, kWorldSeed + 62u) * 26.0f;
    const float soilOf[kBiomeCount] = {
        soil0,                                             // Plains
        2.0f + noise1(x / 38.0f, kWorldSeed + 61u) * 3.0f, // Hills
        3.0f,                                              // Mountains
        soil0,                                             // Canyons
        30.0f + noise1(x / 45.0f, kWorldSeed + 72u) * 10.0f, // Desert
        3.0f + noise1(x / 38.0f, kWorldSeed + 61u) * 3.0f, // Tundra
        3.0f,                                              // Volcanic
        4.0f};                                             // Scrubland
    const float dirtOf[kBiomeCount] = {
        dirt0,                                             // Plains
        8.0f + noise1(x / 74.0f, kWorldSeed + 62u) * 12.0f, // Hills
        10.0f,                                             // Mountains
        dirt0,                                             // Canyons
        0.0f,                                              // Desert (sand sits on stone)
        10.0f + noise1(x / 74.0f, kWorldSeed + 62u) * 12.0f, // Tundra
        10.0f,                                             // Volcanic
        12.0f + noise1(x / 74.0f, kWorldSeed + 62u) * 12.0f}; // Scrubland
    float soilW = 0.0f, dirtW = 0.0f;
    for (int i = 0; i < kBiomeCount; ++i) {
        soilW += biomeW[i] * soilOf[i];
        dirtW += biomeW[i] * dirtOf[i];
    }
    land.soilRows = static_cast<int>(std::lround(soilW * kSurfaceDepth));
    land.dirtRows = static_cast<int>(std::lround(dirtW * kSurfaceDepth));
    // Grass is a skin over soil: the body beneath it is soil, not grass.
    const Mat body = land.topMat == Mat::Grass ? Mat::Soil : land.topMat;
    land.skinRows = land.topMat == Mat::Grass ? 3 : 0;
    land.bodyMat = biomeW[static_cast<int>(BiomeType::Desert)] > 0.5f ? Mat::Sand : body;
    land.screeMat = body;
    return land;
}

}  // namespace

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
                    m = Mat::Bedrock;
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
            // Biome border checker: in the topsoil, cells of the neighbouring
            // biome appear in a 2D noise checker, in proportion to its share.
            if (y >= surface && land.altShare > 0.0f && y - land.profile < land.soilRows &&
                noise2(static_cast<float>(worldX) / 5.0f, static_cast<float>(y) / 5.0f, kWorldSeed + 80u) < land.altShare) {
                m = land.altMat;
            }
            const size_t cell = chunk.index(lx, y);
            chunk.cells[cell] = static_cast<uint8_t>(m);
            chunk.life[cell] = 0;
            chunk.mass[cell] = isMobile(m) ? 1.0f : 0.0f;
        }
    }

    // 3. Pockets of loose material buried in the ground: sand, gravel, clay.
    //    They are big - a pocket you can dig a hole in - and their outline is
    //    lumpy, the way a pocket of loose sand in the rock is. A smooth oval
    //    with an even edge looks like it was stamped out.
    static const Mat kPocketMaterials[3] = {Mat::Sand, Mat::Gravel, Mat::Clay};
    for (int i = 0; i < 6; ++i) {
        const int kindHash = static_cast<int>(hash01(index, i, 91));
        const float temp = climateTemperature(baseX + 10, 400);
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
                // Never refill a cell that the cave carver has already hollowed.
                if (chunk.cells[cell] == static_cast<uint8_t>(Mat::Air)) continue;
                chunk.cells[cell] = static_cast<uint8_t>(m);
                chunk.mass[cell] = 1.0f;
            }
        }
    }


    // Vegetation: trees, bushes and fallen logs stand on the soil of the
    // plains and hills. Each chunk picks its own few, and each stays inside the
    // chunk so no seam shows.
    struct Footprint {
        int left;
        int right;
    };
    std::vector<Footprint> footprints;
    for (int i = 0; i < 6; ++i) {
        const int kind = static_cast<int>(hash01(index, i, 121) * structureKindCount()) % structureKindCount();
        const int variant = static_cast<int>(hash01(index, i, 122) * 8.0f);
        const StructurePattern& pattern = structurePattern(kind, variant);
        const int patternWidth = structurePatternWidth(pattern);
        const int patternHeight = pattern.rowCount;
        if (patternWidth <= 0 || patternWidth >= Chunk::WIDTH - 12) continue;

        const int room = Chunk::WIDTH - 12 - patternWidth;
        const int lx = 6 + static_cast<int>(hash01(index, i, 123) * static_cast<float>(room));
        const int wx = baseX + lx;

        // Only on the living ground of the plains and hills: no trees in the
        // desert, the tundra, the volcanic country or on bare rock.
        float bw[kBiomeCount];
        biomeWeights(wx, smoothSurfaceRow(wx), bw);
        const float living = bw[static_cast<int>(BiomeType::Plains)] + bw[static_cast<int>(BiomeType::Hills)];
        const Mat ground = landAt(wx).topMat;
        if (living < 0.3f || (ground != Mat::Soil && ground != Mat::Grass)) continue;

        bool clashes = false;
        for (const Footprint& f : footprints) {
            if (lx + patternWidth - 1 >= f.left && lx <= f.right) clashes = true;
        }
        if (clashes) continue;

        // Stand on the lowest ground under the footprint, so nothing hangs.
        int base = 0;
        for (int dx = 0; dx < patternWidth; ++dx) base = std::max(base, groundRow(baseX + lx + dx));
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
    std::vector<int> newChunks;
    for (int index = centre - radius; index <= centre + radius; ++index) {
        if (world.hasChunk(index)) continue;
        Chunk& chunk = world.chunk(index);
        generateChunk(index, chunk);
        newChunks.push_back(index);
    }
    // Carve caves AFTER all chunks are generated, so the strata are in place.
    // A chunk's caves depend only on the ground and the noise, not on its
    // neighbours, so only the new chunks need carving.
    for (int index : newChunks) carveChunkCaves(world, index);
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
