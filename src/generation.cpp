#include "generation.h"

#include "materials.h"
#include "structures.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

void carveChunkArches(World& world, int chunk);

namespace {

// The seed of the whole world. One number decides everything: where the hills
// are, where the caves run, what is buried where.
constexpr uint32_t kWorldSeed = 0x9E3779B9u;

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
};


// -- What the ground is made of ---------------------------------------------

// The climate of a place: how cold it is and how wet it is.
//
// The cold comes from a slow wave running across the world, and the wetness
// from a second, slower wave that does not care about the height at all.
// Both are maps over the world rather than over the rock, so a whole region
// shares one climate and the rock under it was laid down in that climate.
// Provinces: the world is divided into regions, each with its own climate -
// a desert, a tundra, a wet forest. Each province has a random point along the
// world's length, and a column belongs to the province whose point is nearest.
// Where two provinces meet, their climates blend across a band, so the border
// is a gradual change rather than a line.
constexpr float kProvinceSpacing = 900.0f;
constexpr float kProvinceBlend = 0.35f;   // blend band, as a share of the spacing

struct Provinces {
    int nearest;
    int second;
    float weight;   // 1 deep inside the nearest province, 0.5 on the border
};

static Provinces provincesAt(int worldX) {
    const float fx = static_cast<float>(worldX);
    const int cell = static_cast<int>(std::floor(fx / kProvinceSpacing));
    float bestD = 1e9f, secondD = 1e9f;
    int best = 0, second = 0;
    for (int c = cell - 2; c <= cell + 2; ++c) {
        const int id = c;
        const float point = (static_cast<float>(c) + hash01(id, 0, static_cast<int>(kWorldSeed) + 1800)) * kProvinceSpacing;
        const float d = std::fabs(fx - point);
        if (d < bestD) {
            secondD = bestD; second = best;
            bestD = d; best = id;
        } else if (d < secondD) {
            secondD = d; second = id;
        }
    }
    const float band = kProvinceBlend * kProvinceSpacing;
    float t = std::min(1.0f, std::max(0.0f, (secondD - bestD) / band));
    t = t * t * (3.0f - 2.0f * t);
    return {best, second, 0.5f + 0.5f * t};
}

static float provinceTemperature(int id) {
    return 0.15f + 0.70f * hash01(id, 1, static_cast<int>(kWorldSeed) + 1801);
}

static float provinceMoisture(int id) {
    return 0.10f + 0.80f * hash01(id, 2, static_cast<int>(kWorldSeed) + 1802);
}

static float climateTemperature(int worldX) {
    const Provinces p = provincesAt(worldX);
    const float base = provinceTemperature(p.nearest) * p.weight +
                       provinceTemperature(p.second) * (1.0f - p.weight);
    return std::min(1.0f, std::max(0.0f, base + (fbm1(static_cast<float>(worldX) / 8000.0f, kWorldSeed + 201u, 3) - 0.5f) * 0.2f));
}

static float climateMoisture(int worldX, int y) {
    const Provinces p = provincesAt(worldX);
    const float base = provinceMoisture(p.nearest) * p.weight +
                       provinceMoisture(p.second) * (1.0f - p.weight);
    const float wander = fbm2(static_cast<float>(worldX) / 6000.0f,
                              static_cast<float>(y) / 4000.0f, kWorldSeed + 211u, 2);
    return std::min(1.0f, std::max(0.0f, base + (wander - 0.5f) * 0.25f));
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
        const float cold = climateTemperature(worldX);
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
    if (band > 0.40f && band < 0.46f && moist < 0.45f) return Mat::Sand;
    if (band > 0.62f && band < 0.66f && moist < 0.42f && temp > 0.45f) return Mat::Sand;
    if (band > 0.74f && band < 0.77f) return Mat::Gravel;
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

void carveChunkCaves(World& world, int chunk) {
    constexpr int kMargin = 64;
    constexpr int kMinHole = 80;
    const int height = world.height();
    const int ownX0 = chunk * Chunk::WIDTH;
    const int winX0 = ownX0 - kMargin;
    const int winW = Chunk::WIDTH + 2 * kMargin;
    const int rows = height - 3;
    auto at = [&](int x, int y) { return static_cast<size_t>(y) * winW + (x - winX0); };

    // Raw carve mask over the window. Pure noise and ground height only.
    std::vector<uint8_t> mask(static_cast<size_t>(winW) * rows, 0);
    for (int x = winX0; x < winX0 + winW; ++x) {
        const int surf = groundRow(x);
        for (int y = surf + 12; y < height - 100 && y < rows; ++y) {
            // Density: the product of two independent 3D noise fields, each
            // taken as an absolute value. A cave forms only where both fields
            // are large at the same point, so the open space breaks into
            // separate pockets instead of one connected sheet. Two scales:
            // small near the surface, large deeper, blended by absolute row.
            const float blend = std::min(1.0f, std::max(0.0f, (static_cast<float>(y - surf) - 60.0f) / 200.0f));
            const float fx = static_cast<float>(x), fy = static_cast<float>(y);
            // Domain warp: the fields are read at points pushed about by a smooth
            // offset that depends only on position. This bends each hole into an
            // irregular outline (lobed, elongated, bent) instead of a circle.
            const float warpX = (noise2(fx / 90.0f, fy / 90.0f, kWorldSeed + 1600u) - 0.5f) * 2.0f * kCaveWarp;
            const float warpY = (noise2(fx / 90.0f, fy / 90.0f, kWorldSeed + 1601u) - 0.5f) * 2.0f * kCaveWarp;
            const float wx = fx + warpX, wy = fy + warpY;
            const float smallA = std::fabs(noise3(wx / 24.0f, wy / 24.0f, 0.0f, kWorldSeed + 1200u) * 2.0f - 1.0f);
            const float smallB = std::fabs(noise3(wx / 24.0f, wy / 24.0f, 37.0f, kWorldSeed + 1202u) * 2.0f - 1.0f);
            const float largeA = std::fabs(noise3(wx / 220.0f, wy / 220.0f, 0.0f, kWorldSeed + 1201u) * 2.0f - 1.0f);
            const float largeB = std::fabs(noise3(wx / 220.0f, wy / 220.0f, 37.0f, kWorldSeed + 1203u) * 2.0f - 1.0f);
            const float small = smallA * smallB;
            const float large = largeA * largeB;
            const float v = small + (large - small) * blend;
            const float rough = noise2(fx * 0.9f, fy * 0.9f, kWorldSeed + 1300u);
            // Mid-scale edge detail: breaks the smooth outline of each hole so
            // it stops reading as a circle. It does not change the hole's size.
            const float edge = noise2(fx / 9.0f, fy / 9.0f, kWorldSeed + 1500u);
            const float thresh = kShallowThreshold + (kDeepThreshold - kShallowThreshold) * blend;
            if (v + (rough - 0.5f) * 0.10f + (edge - 0.5f) * 0.08f > thresh) mask[at(x, y)] = 1;
        }
    }

    // Morphological opening: erode twice, then dilate twice, 4-way. Anything
    // narrower than about five cells - the sheared slivers simplex noise leaves
    // at small scales - cannot survive, while round holes keep their shape.
    auto step = [&](bool erode) {
        std::vector<uint8_t> next(mask.size(), 0);
        const int dx[4] = {-1, 1, 0, 0};
        const int dy[4] = {0, 0, -1, 1};
        for (int x = winX0; x < winX0 + winW; ++x)
            for (int y = 0; y < rows; ++y) {
                bool hit = erode;
                for (int d = 0; d < 4; ++d) {
                    const int nx = x + dx[d], ny = y + dy[d];
                    const bool n = nx >= winX0 && nx < winX0 + winW && ny >= 0 && ny < rows &&
                                   mask[at(nx, ny)] != 0;
                    hit = erode ? (hit && n) : (hit || n);
                }
                next[at(x, y)] = hit ? 1 : 0;
            }
        mask.swap(next);
    };
    step(true);
    step(true);
    step(false);
    step(false);

    // Fill pieces too small to be caves. Real holes are much larger than this.
    std::vector<uint8_t> seen(mask.size(), 0);
    std::vector<int> stack;
    std::vector<int> piece;
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
                    if (nx < winX0 || nx >= winX0 + winW || ny < 0 || ny >= rows) continue;
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

    // Write this chunk's own columns. Water from the chunk's own ponds stays.
    for (int x = ownX0; x < ownX0 + Chunk::WIDTH; ++x)
        for (int y = 0; y < rows; ++y) {
            if (!mask[at(x, y)]) continue;
            if (world.at(x, y) == Mat::Water || world.at(x, y + 1) == Mat::Water) continue;
            world.set(x, y, Mat::Air);
        }
}


// Natural arches: horizontal tunnels through the faces of steep cliffs. A
// tunnel is an ellipse, centred a few rows below the cliff's top, so the rock
// above it stays solid and reads as a bridge where the tunnel passes through.
// Each chunk carves only its own columns, from the same hashed centres, so
// arches agree across chunk borders.
void carveCaves(World& world, int chunkStart, int chunkEnd) {
    for (int chunk = chunkStart; chunk <= chunkEnd; ++chunk) {
        if (world.hasChunk(chunk)) {
            carveChunkCaves(world, chunk);
            carveChunkArches(world, chunk);
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
// One wave, centred on zero, so it can raise or lower the ground.
static float shapeWave(float x, float wavelength, uint32_t seed, int octaves) {
    return (fbm1(x / wavelength, seed, octaves) - 0.5f) * 2.0f;
}

static float profileHeight(int worldX) {
    const float x = static_cast<float>(worldX);

    // The great swell: the whole world rising and falling over thousands of
    // columns, so the land is never the same height two valleys apart.
    const float swell = shapeWave(x, 5200.0f, kWorldSeed + 5u, 2);

    // Broad rolling hills on top of that.
    const float hills = shapeWave(x, 900.0f, kWorldSeed + 11u, 3);
    const float hills2 = shapeWave(x, 260.0f, kWorldSeed + 12u, 2);

    // Bumps and hollows a few dozen columns across. These are what keep the
    // ground from settling into long flat terraces: a land whose only shapes
    // are a kilometre wide is a staircase of identical steps, because every
    // step of one row is followed by a hundred columns of nothing.
    const float rough = shapeWave(x, 90.0f, kWorldSeed + 13u, 2);
    const float fine = shapeWave(x, 30.0f, kWorldSeed + 17u, 2);

    const float mountain = mountainHeightAt(worldX);

    // Crags: the short, jagged detail that only the mountains have. On the
    // lowlands this is zero, because a plain with a sawtooth on it is not a
    // mountain, it is a sawtooth.
    const float crags = shapeWave(x, 170.0f, kWorldSeed + 19u, 2) * mountain;

    // The channels the rain cut. The folded noise is high along a winding
    // line, and that line is where the water runs, so the ground is cut down
    // along it and the cut is widest where the fold is strongest.
    const float crestLine = ridged1(x / 520.0f, kWorldSeed + 67u, 2);
    const float channel = std::max(0.0f, crestLine - 0.62f) / 0.38f;
    const float eroded = channel * channel * 26.0f *
                         fbm1(x / 1600.0f, kWorldSeed + 68u, 2);

    // Lowland level. Everything else is measured up from here.
    return 330.0f
         - swell * 190.0f
         - hills * 130.0f
         - hills2 * 56.0f
         - rough * 38.0f
         - fine * 13.0f
         - mountain * 260.0f
         - crags * 56.0f
         + eroded;
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

    // How steep the ground is here: rows of fall per column, measured over a
    // few columns either side so one step does not count as a cliff.
    const float slope = std::fabs(static_cast<float>(profileAt(worldX + 4) - profileAt(worldX - 4))) / 8.0f;
    // Flat ground in a low valley collects deeper soil: the rain brings silt
    // down and it settles. Both measures change smoothly, so the soil depth
    // does not step from column to column.
    const float flat = 1.0f - std::min(1.0f, std::max(0.0f, (slope - 0.2f) / 0.5f));
    const float low = std::min(1.0f, std::max(0.0f, (static_cast<float>(profile) - 300.0f) / 80.0f));
    if (!high) {
        land.soilRows += static_cast<int>(std::lround(flat * low * 4.0f));
        land.dirtRows += static_cast<int>(std::lround(flat * low * 14.0f));
    }

    // Curvature: is this column on a crest (higher than the ground either
    // side of it) or in a hollow (lower than both)? Rows grow downward, so a
    // crest has a smaller row than the average of its neighbours.
    const float curve = (static_cast<float>(profileAt(worldX - 4) + profileAt(worldX + 4)) * 0.5f) -
                        static_cast<float>(profile);
    // A crest sheds its soil: rain and wind strip the top, so the rock shows.
    // A hollow collects what washes down from the slopes around it.
    if (!high && curve > 0.8f) land.soilRows = 1;
    if (!high && curve < -0.8f) {
        land.soilRows += 3;
        land.dirtRows += 6;
    }

    // What the ground surface is made of.
    //
    // A hot dry country is sand. A wet one is mud. A warm damp one is clay.
    // A cold one is frozen ground. A mountain is bare rock. Everything else
    // is ordinary soil.
    if (high) {
        land.topMat = Mat::Stone;
        land.screeMat = Mat::Gravel;
    } else if (!high && curve > 0.8f && slope > 0.25f) {
        // A bare ridge with some slope to it: the crest shows rock.
        land.topMat = Mat::Stone;
        land.screeMat = Mat::Gravel;
    } else if (slope > 0.9f) {
        // A steep face has no soil on it: the weather strips it to rock and
        // the loose rock slides down to the foot, whatever the climate is.
        land.topMat = Mat::Stone;
        land.screeMat = Mat::Gravel;
        land.soilRows = 1;
    } else if (temp > 0.55f && moist > 0.70f) {
        land.topMat = Mat::Mycelium;
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

static float columnSlope(int x) {
    return std::fabs(static_cast<float>(profileAt(x + 4) - profileAt(x - 4))) / 8.0f;
}

void carveChunkArches(World& world, int chunk) {
    constexpr int kSpacing = 64;
    constexpr int kReach = 24;
    const int height = world.height();
    const int ownX0 = chunk * Chunk::WIDTH;
    const int ownX1 = ownX0 + Chunk::WIDTH;
    for (int gx = (ownX0 - kReach) / kSpacing - 1; gx <= (ownX1 + kReach) / kSpacing + 1; ++gx) {
        const float jitter = hash01(gx, 0, static_cast<int>(kWorldSeed) + 1900);
        if (hash01(gx, 1, static_cast<int>(kWorldSeed) + 1901) > 0.35f) continue;
        const int cx = gx * kSpacing + static_cast<int>(jitter * kSpacing);
        if (columnSlope(cx) < 0.6f) continue;
        const int cy = profileAt(cx) + 18;
        const float rx = 10.0f + 6.0f * hash01(gx, 2, static_cast<int>(kWorldSeed) + 1902);
        const float ry = 3.0f + 1.0f * hash01(gx, 3, static_cast<int>(kWorldSeed) + 1903);
        for (int y = cy - 5; y <= cy + 5; ++y) {
            if (y < 0 || y >= height - 3) continue;
            for (int x = static_cast<int>(cx - rx) - 1; x <= static_cast<int>(cx + rx) + 1; ++x) {
                if (x < ownX0 || x >= ownX1) continue;
                const float dx = (x - cx) / rx, dy = (y - cy) / ry;
                if (dx * dx + dy * dy > 1.0f) continue;
                if (y < profileAt(x) + 12) continue;
                if (world.at(x, y) == Mat::Water) continue;
                world.set(x, y, Mat::Air);
            }
        }
    }
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
                    m = Mat::Bedrock;
                } else if (y >= height - 180) {
                    // The underworld: ash and a lava ocean at the very bottom.
                    m = (y >= height - 35) ? Mat::Lava : Mat::Ash;
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

    // 4. A pond on the surface, filling a natural basin.
    //
    //    Water is level, so the water surface sits at the height of the
    //    lowest rim of the basin. The basin is found the way a real lake is
    //    found: a low point in the ground with higher ground on both sides.
    //    Every cell of ground below that level is filled, and a sandy shore
    //    is left where the water meets the land.
    int pondLeft = 0;
    int pondRight = -1;
    {
        // Find the deepest natural basin in this chunk.
        int bestLowX = -1;
        int bestDepth = 0;
        for (int cx = 12; cx < Chunk::WIDTH - 12; ++cx) {
            const int half = 22;
            const int spanLeft = std::max(0, cx - half);
            const int spanRight = std::min(Chunk::WIDTH - 1, cx + half);
            int lowX = spanLeft;
            for (int l = spanLeft + 1; l <= spanRight; ++l)
                if (groundRow(baseX + l) > groundRow(baseX + lowX)) lowX = l;
            int rimLeft = groundRow(baseX + spanLeft);
            int rimRight = groundRow(baseX + spanRight);
            for (int l = lowX - 1; l >= spanLeft; --l) rimLeft = std::min(rimLeft, groundRow(baseX + l));
            for (int l = lowX + 1; l <= spanRight; ++l) rimRight = std::min(rimRight, groundRow(baseX + l));
            const int spill = std::max(rimLeft, rimRight);
            const int bottom = groundRow(baseX + lowX);
            const int depth = bottom - spill;
            if (depth > bestDepth) { bestDepth = depth; bestLowX = lowX; }
        }

        if (bestLowX >= 0 && bestDepth >= 4) {
            const int lx = bestLowX;
            const int half = 22;
            const int spanLeft = std::max(0, lx - half);
            const int spanRight = std::min(Chunk::WIDTH - 1, lx + half);
            int rimLeft = groundRow(baseX + spanLeft);
            int rimRight = groundRow(baseX + spanRight);
            for (int l = lx - 1; l >= spanLeft; --l) rimLeft = std::min(rimLeft, groundRow(baseX + l));
            for (int l = lx + 1; l <= spanRight; ++l) rimRight = std::min(rimRight, groundRow(baseX + l));
            const int spill = std::max(rimLeft, rimRight);
            const int waterTop = spill;
            const Mat shoreMat = (climateTemperature(baseX + lx) > 0.5f) ? Mat::Sand : Mat::Clay;

            for (int l = spanLeft; l <= spanRight; ++l) {
                const int g = groundRow(baseX + l);
                if (g < waterTop) continue;
                const float t = static_cast<float>(l - lx) / static_cast<float>(half);
                const int extra = static_cast<int>((1.0f - t * t) * 5.0f);
                const int floor = g + extra;
                const int digFrom = std::max(g, waterTop);
                for (int y = digFrom; y < floor; ++y) {
                    if (y < 0 || y >= height) continue;
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::Air);
                    chunk.mass[cell] = 0.0f;
                }
                for (int y = waterTop; y < floor; ++y) {
                    if (y < 0 || y >= height) continue;
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::Water);
                    chunk.mass[cell] = 1.0f;
                }
                for (int y = floor; y < floor + 3; ++y) {
                    if (y < 0 || y >= height) continue;
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(Mat::Stone);
                    chunk.mass[cell] = 0.0f;
                }
            }
            // Shoreline sediment.
            for (int l = spanLeft; l <= spanRight; ++l) {
                const int g = groundRow(baseX + l);
                if (g < waterTop) continue;
                for (int y = g; y < std::min(g + 2, height - 3); ++y) {
                    const size_t cell = chunk.index(l, y);
                    chunk.cells[cell] = static_cast<uint8_t>(shoreMat);
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
    std::vector<int> newChunks;
    for (int index = centre - radius; index <= centre + radius; ++index) {
        if (world.hasChunk(index)) continue;
        Chunk& chunk = world.chunk(index);
        generateChunk(index, chunk);
        newChunks.push_back(index);
    }
    if (!newChunks.empty()) {
        int lo = *std::min_element(newChunks.begin(), newChunks.end()) - 1;
        int hi = *std::max_element(newChunks.begin(), newChunks.end()) + 1;
        carveCaves(world, lo, hi);
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
