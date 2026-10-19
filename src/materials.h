#pragma once

#include <cstddef>
#include <cstdint>

#include "raylib.h"

// Every material that can occupy a grid cell. The simulation reads properties
// for these from the table in materials.cpp, so adding a material is one enum
// value plus one table row.
enum class Mat : uint8_t {
    Air = 0,

    // Solids — stay where they are, do not fall.
    Bedrock,
    Stone,
    Limestone,
    Basalt,
    Obsidian,
    Bones,

    // Loose solids — fall when unsupported and pile up.
    Soil,
    PackedDirt,
    Sand,
    Gravel,
    Clay,
    Snow,
    GlacialIce,

    // Flammable organics — catch fire and burn down to ash.
    Wood,
    Bark,
    Foliage,
    Mycelium,
    FungalCap,

    // Liquids — flow, spread and pool.
    Water,
    Lava,
    Oil,
    Acid,
    Mud,

    // Effects and byproducts.
    SulfurCrust,
    Ash,
    Fire,

    COUNT
};

struct MaterialInfo {
    const char* name;
    Color color;
};

// Returns the description of a material. Out-of-range values fall back to Air.
const MaterialInfo& materialInfo(Mat m);

inline bool isAir(Mat m) { return m == Mat::Air; }
