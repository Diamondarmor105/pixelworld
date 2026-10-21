#include "materials.h"

// One row per material. Colour is what it looks like; the rest is what it does.
// Columns:
//   name | colour | density | falls | flows | viscosity | friction | rises |
//   flammable | burnTime | igniteChance | burnsInto | meltable | meltsInto |
//   washedByWater | blastResistance | acidCorrodes
//
// `viscosity` is read only for liquids: 0 flows like water, 255 barely moves.
// `friction` is read only for loose solids: 0 levels like water, 255 holds a
// cliff. `rises` marks a gas: it moves upward instead of downward, as an
// inverted liquid. Everything else is unaffected by the three columns.
//
// Adding a material is one enum value in materials.h plus one row here.
// Bright and friendly palette by design — see devpost/spec.md > Look and Feel.
static const MaterialInfo kMaterials[] = {
    /* Air          */ {"Air",          { 150, 214, 247, 255 },   0, false, false,   0,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,   0, false},
    /* Bedrock      */ {"Bedrock",      {  58,  58,  66, 255 }, 255, false, false,   0,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false, 255, false},
    /* Stone        */ {"Stone",        { 130, 130, 138, 255 }, 250, false, false,   0,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false, 120, true},
    /* Limestone    */ {"Limestone",    { 196, 190, 170, 255 }, 230, false, false,   0,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false, 140, true},
    /* Basalt       */ {"Basalt",       {  78,  80,  92, 255 }, 245, false, false,   0,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false, 220, false},
    /* Obsidian     */ {"Obsidian",     {  40,  32,  48, 255 }, 250, false, false,   0,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false, 250, false},
    /* Bones        */ {"Bones",        { 226, 222, 200, 255 }, 200, false, false,   0,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,  90, true},
    /* Soil         */ {"Soil",         { 110,  80,  50, 255 }, 145, true,  false,   0, 150, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,  25, true},
    /* PackedDirt   */ {"Packed Dirt",  { 124,  88,  58, 255 }, 150, true,  false,   0, 190, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,  30, true},
    /* Sand         */ {"Sand",         { 232, 208, 140, 255 }, 160, true,  false,   0, 110, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,  25, true},
    /* Gravel       */ {"Gravel",       { 150, 146, 140, 255 }, 170, true,  false,   0, 140, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,  35, true},
    /* Clay         */ {"Clay",         { 176, 124, 100, 255 }, 140, true,  false,   0, 200, false, false,  0,   0, Mat::Air,   true,  Mat::Water, false,  40, true},
    /* Snow         */ {"Snow",         { 245, 248, 252, 255 },  90, true,  false,   0, 170, false, true,   0,   0, Mat::Air,   true,  Mat::Water, false,  20, true},
    /* GlacialIce   */ {"Glacial Ice",  { 170, 220, 240, 255 }, 200, false, false,   0,   0, false, true,   0,   0, Mat::Air,   true,  Mat::Water, false,  60, true},
    /* Wood         */ {"Wood",         { 146, 102,  64, 255 }, 200, false, false,   0,   0, false, true,  60,  40, Mat::Ash,   false, Mat::Air,   false,  60, true},
    /* Bark         */ {"Bark",         { 110,  76,  48, 255 }, 210, false, false,   0,   0, false, true,  60,  35, Mat::Ash,   false, Mat::Air,   false,  65, true},
    /* Foliage      */ {"Foliage",      {  96, 176,  88, 255 },  80, false, false,   0,   0, false, true,  15,  70, Mat::Ash,   false, Mat::Air,   false,  15, true},
    /* Mycelium     */ {"Mycelium",     { 170, 150, 180, 255 }, 150, false, false,   0,   0, false, true,  20,  50, Mat::Ash,   false, Mat::Air,   false,  20, true},
    /* FungalCap    */ {"Fungal Cap",   { 206, 110, 120, 255 }, 120, false, false,   0,   0, false, true,  20,  50, Mat::Ash,   false, Mat::Air,   false,  20, true},
    /* Water        */ {"Water",        {  70, 140, 230, 255 }, 100, false, true,    0,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,   5, false},
    /* Lava         */ {"Lava",         { 245, 110,  32, 255 }, 220, false, true,  170,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,  90, false},
    /* Oil          */ {"Oil",          {  48,  44,  44, 255 },  60, false, true,   70,   0, false, true,  10, 110, Mat::Air,   false, Mat::Air,   false,  10, false},
    /* Acid         */ {"Acid",         { 190, 230,  90, 255 }, 105, false, true,   30,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,   5, false},
    /* Mud          */ {"Mud",          { 108,  84,  64, 255 }, 130, false, true,  210,   0, false, false,  0,   0, Mat::Air,   true,  Mat::Water, false,  30, false},
    /* Smoke        */ {"Smoke",        { 140, 140, 150, 255 },   5, false, false,   0,   0, true,  false,  0,   0, Mat::Air,   false, Mat::Air,   false,   0, false},
    /* SulfurCrust  */ {"Sulfur Crust", { 214, 196,  86, 255 }, 180, false, false,   0,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,  40, true},
    /* Ash          */ {"Ash",          { 170, 170, 170, 255 },  50, true,  false,   0, 120, false, false,  0,   0, Mat::Air,   false, Mat::Air,   true,   10, true},
    /* Fire         */ {"Fire",         { 250, 170,  50, 255 },   0, false, false,   0,   0, false, false,  0,   0, Mat::Air,   false, Mat::Air,   false,   0, false},
};

static_assert(sizeof(kMaterials) / sizeof(kMaterials[0]) == static_cast<size_t>(Mat::COUNT),
              "material table must have one row per Mat value");

const MaterialInfo& materialInfo(Mat m) {
    const size_t index = static_cast<size_t>(m);
    if (index >= static_cast<size_t>(Mat::COUNT)) return kMaterials[0];
    return kMaterials[index];
}

bool isBurning(Mat m, uint8_t life) {
    if (m == Mat::Fire) return true;
    return life > 0 && materialInfo(m).flammable;
}
