#pragma once

#include <cstddef>
#include <cstdint>

#include "raylib.h"

// Every material that can occupy a grid cell. The simulation reads the
// properties for these from the one table in materials.cpp, so adding a
// material is one enum value plus one table row.
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

// Everything the simulation and renderer need to know about a material.
// Filled in per material in the table; unused behaviour is switched off with
// `false` and the matching target left as Mat::Air.
struct MaterialInfo {
    const char* name;
    Color color;

    uint8_t density;          // heavier materials sink through lighter ones; air is 0
    bool falls;               // loose solid: falls and piles when unsupported
    bool flows;               // liquid: spreads sideways and pools
    uint8_t flowSlack;        // caps how far a liquid reaches sideways, in cells per tick
    bool flammable;           // catches fire from an adjacent flame
    Mat burnsInto;            // what is left after burning
    bool meltable;            // turns to `meltsInto` when fire or lava is adjacent
    Mat meltsInto;
    bool washedByWater;       // dissolves away when water is adjacent
    uint8_t blastResistance;  // 0 = blown away easily, 255 = immune to explosions
    bool acidCorrodes;        // acid eats through this material
};

// Returns the description of a material. Out-of-range values fall back to Air.
const MaterialInfo& materialInfo(Mat m);

inline bool isAir(Mat m) { return m == Mat::Air; }

// Loose solids and liquids are the materials that move.
inline bool isMobile(Mat m) {
    const MaterialInfo& info = materialInfo(m);
    return info.falls || info.flows;
}
