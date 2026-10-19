#include "materials.h"

// One row per material: display name and the flat colour it is drawn with.
// Bright and friendly by design — see devpost/spec.md > Look and Feel.
static const MaterialInfo kMaterials[] = {
    /* Air         */ {"Air",         { 150, 214, 247, 255 }},
    /* Bedrock     */ {"Bedrock",     {  58,  58,  66, 255 }},
    /* Stone       */ {"Stone",       { 130, 130, 138, 255 }},
    /* Limestone   */ {"Limestone",   { 196, 190, 170, 255 }},
    /* Basalt      */ {"Basalt",      {  78,  80,  92, 255 }},
    /* Obsidian    */ {"Obsidian",    {  40,  32,  48, 255 }},
    /* Bones       */ {"Bones",       { 226, 222, 200, 255 }},
    /* Soil        */ {"Soil",        { 110,  80,  50, 255 }},
    /* PackedDirt  */ {"Packed Dirt", { 124,  88,  58, 255 }},
    /* Sand        */ {"Sand",        { 232, 208, 140, 255 }},
    /* Gravel      */ {"Gravel",      { 150, 146, 140, 255 }},
    /* Clay        */ {"Clay",        { 176, 124, 100, 255 }},
    /* Snow        */ {"Snow",        { 245, 248, 252, 255 }},
    /* GlacialIce  */ {"Glacial Ice", { 170, 220, 240, 255 }},
    /* Wood        */ {"Wood",        { 146, 102,  64, 255 }},
    /* Bark        */ {"Bark",        { 110,  76,  48, 255 }},
    /* Foliage     */ {"Foliage",     {  96, 176,  88, 255 }},
    /* Mycelium    */ {"Mycelium",    { 170, 150, 180, 255 }},
    /* FungalCap   */ {"Fungal Cap",  { 206, 110, 120, 255 }},
    /* Water       */ {"Water",       {  70, 140, 230, 255 }},
    /* Lava        */ {"Lava",        { 245, 110,  32, 255 }},
    /* Oil         */ {"Oil",         {  48,  44,  44, 255 }},
    /* Acid        */ {"Acid",        { 190, 230,  90, 255 }},
    /* Mud         */ {"Mud",         { 108,  84,  64, 255 }},
    /* SulfurCrust */ {"Sulfur Crust",{ 214, 196,  86, 255 }},
    /* Ash         */ {"Ash",         { 170, 170, 170, 255 }},
    /* Fire        */ {"Fire",        { 250, 170,  50, 255 }},
};

static_assert(sizeof(kMaterials) / sizeof(kMaterials[0]) == static_cast<size_t>(Mat::COUNT),
              "material table must have one row per Mat value");

const MaterialInfo& materialInfo(Mat m) {
    const size_t index = static_cast<size_t>(m);
    if (index >= static_cast<size_t>(Mat::COUNT)) return kMaterials[0];
    return kMaterials[index];
}
