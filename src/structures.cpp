#include "structures.h"

namespace {

// -- Ruined walls ----------------------------------------------------------
// A wall that was standing once. Some of it is still there; the rest has come
// down over the years.

const char* const kRuinA[] = {
    "SSS.SSS.SS.S",
    "SSS.SSS.SS.S",
    "SSS.SSS.SS.S",
    "SSS.SSS.SS.S",
    ".SS.SSS.SS.S",
    ".SS.SSS.SS.S",
    "..S.SSS.SS..",
    "..S.SSS.SS..",
    "...S.SS..S..",
    "...S.SS..S..",
    "....S.......",
    "....S.......",
};

const char* const kRuinB[] = {
    "NNN.NN.NNNNN",
    "NNN.NN.NNNNN",
    "NNN.NN.NNNNN",
    "NNN.NN.NNNNN",
    "NNN.NN.NNNN.",
    "NNN.NN.NNN..",
    ".NN.NN.NNN..",
    ".NN.NN.NN...",
    ".NN.NN.NN...",
    "..N..N......",
    "..N..N......",
};

const char* const kRuinC[] = {
    "SSSSSSSS.SSSS",
    "SSSSSSSS.SSSS",
    ".SSSSSSSSSSSS",
    ".SSSSSSSSSSSS",
    "..SSSSSSSSSSS",
    "..SSSSSSSS.SS",
    "...SSSSSS..SS",
    "...SSSSSS..SS",
    "....SSSSS..SS",
    "....SSSS.....",
    "....SSSS.....",
    ".............",
};

// -- Fallen pillars --------------------------------------------------------
// A column of stone that came down in one piece and has been lying here ever
// since. It is round, so it is drawn with a rounded end.

const char* const kPillarA[] = {
    "NNNNNNNNNNNNNN",
    "NNNNNNNNNNNNNN",
    ".NNNNNNNNNNNN.",
    "..NNNNNNNNNN..",
    "...NNNNNNNN...",
    "....NNNNNN....",
    ".....NNNN.....",
};

const char* const kPillarB[] = {
    "SSSSSSSSSSSSSSSS",
    "SSSSSSSSSSSSSSSS",
    ".SSSSSSSSSSSSSS.",
    "..SSSSSSSSSSSS..",
    "...SSSSSSSSSS...",
    "....SSSSSSSS....",
    ".....SSSSSS.....",
    "......SSSS......",
};

const char* const kPillarC[] = {
    "XXXXXXXXXX",
    "XXXXXXXXXX",
    ".XXXXXXXX.",
    "..XXXXXX..",
    "...XXXX...",
    "....XX....",
};

// -- Trees -----------------------------------------------------------------
// A trunk of wood with a rounded crown of leaves on top.
//
// One rule governs every tree here: the LEAVES reach all the way down, but
// the WOOD does not. Only the trunk is wood, and it is narrow. A branch
// hanging over your face at eye level stops you dead - you try to jump, bump
// your head on the branch, and drop straight back down again - so the player
// has to be able to stand beside a tree and jump. With the leaves doing
// nothing at all (you push straight through them) and the wood kept down in
// the trunk, they can walk into a tree, jump, and land on top of it.

const char* const kTreeA[] = {
    "....LLLL....",
    "...LLLLLL...",
    "..LLLLLLLL..",
    ".LLLLLLLLLL.",
    "LLLLLLLLLLLL",
    "LLLLLLLLLLLL",
    ".LLLLLLLLLL.",
    "..LLLLLLLL..",
    "...LLLLLL...",
    "..LLLLLLLL..",
    ".LLLLLLLLLL.",
    "..LLLLLLLL..",
    "...LLLLLL...",
    "....TTTT....",
    "....TTTT....",
    "....TTTT....",
    "....TTTT....",
    "....TTTT....",
};

const char* const kTreeB[] = {
    ".....LLL.....",
    "...LLLLLLL...",
    "..LLLLLLLLL..",
    ".LLLLLLLLLLL.",
    "LLLLLLLLLLLLL",
    "LLLLLLLLLLLLL",
    "LLLLLLLLLLLLL",
    "LLLLLLLLLLLLL",
    ".LLLLLLLLLLL.",
    "..LLLLLLLLL..",
    "...LLLLLLL...",
    "..LLLLLLLLL..",
    ".LLLLLLLLLLL.",
    "..LLLLLLLLL..",
    "...LLLLLLL...",
    ".....LLL.....",
    ".....TTTT....",
    ".....TTTT....",
    ".....TTTT....",
    ".....TTTT....",
};

const char* const kTreeC[] = {
    "..LLL..LLL..",
    ".LLLLLLLLLL.",
    "LLLLLLLLLLLL",
    "LLLLLLLLLLLL",
    "LLLLLLLLLLLL",
    ".LLLLLLLLLL.",
    "..LLLLLLLL..",
    "...LLLLLL...",
    "..LLLLLLLL..",
    ".LLLLLLLLLL.",
    "..LLLLLLLL..",
    "....TTTT....",
    "....TTTT....",
    "....TTTT....",
    "....TTTT....",
    "....TTTT....",
};

// -- Giant trees -----------------------------------------------------------
// The same shape, but a great deal bigger, with a trunk to climb.

const char* const kGiantTreeA[] = {
    "......LLLLLL......",
    "....LLLLLLLLLL....",
    "..LLLLLLLLLLLLLL..",
    ".LLLLLLLLLLLLLLLL.",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    ".LLLLLLLLLLLLLLLL.",
    ".LLLLLLLLLLLLLLLL.",
    "..LLLLLLLLLLLLLL..",
    "..LLLLLLLLLLLLLL..",
    "...LLLLLLLLLLLL...",
    "....LLLLLLLLLL....",
    ".....LLLLLLLL.....",
    ".......LLLLL......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
};

const char* const kGiantTreeB[] = {
    ".....LLLLLLLL.....",
    "...LLLLLLLLLLLL...",
    ".LLLLLLLLLLLLLLLL.",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    ".LLLLLLLLLLLLLLLL.",
    ".LLLLLLLLLLLLLLLL.",
    "..LLLLLLLLLLLLLL..",
    "..LLLLLLLLLLLLLL..",
    "...LLLLLLLLLLLL...",
    "....LLLLLLLLLL....",
    ".....LLLLLLLL.....",
    "......LLLLLLL.....",
    "......LLLLLLL.....",
    ".......LLLLL......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
};

const char* const kGiantTreeC[] = {
    "....LLLLLLLLLL....",
    "..LLLLLLLLLLLLLL..",
    ".LLLLLLLLLLLLLLLL.",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    "LLLLLLLLLLLLLLLLLL",
    ".LLLLLLLLLLLLLLLL.",
    ".LLLLLLLLLLLLLLLL.",
    "..LLLLLLLLLLLLLL..",
    "..LLLLLLLLLLLLLL..",
    "...LLLLLLLLLLLL...",
    "....LLLLLLLLLL....",
    ".....LLLLLLLL.....",
    "......LLLLLLL.....",
    "......LLLLLLL.....",
    ".......LLLLL......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
    ".......TTTT.......",
};

// -- Bushes ----------------------------------------------------------------

const char* const kBushA[] = {
    "..LLLL..",
    ".LLLLLL.",
    "LLLLLLLL",
    "LLLLLLLL",
    ".LLLLLL.",
    "..L..L..",
};

const char* const kBushB[] = {
    "....LL....",
    "..LLLLLL..",
    ".LLLLLLLL.",
    "LLLLLLLLLL",
    "LLLLLLLLLL",
    ".LLLLLLLL.",
    "..LL..LL..",
};

const char* const kBushC[] = {
    "..FF..FF..",
    ".FFFFFFFF.",
    "..LLLLLL..",
    ".LLLLLLLL.",
    "LLLLLLLLLL",
    "LLLLLLLLLL",
    ".LLLLLLLL.",
    "..L....L..",
};

// -- Cacti -----------------------------------------------------------------

const char* const kCactusA[] = {
    "...L...",
    "...L.L.",
    ".L.L.L.",
    ".L.LLL.",
    ".LLL...",
    "...L...",
    "...L...",
};

const char* const kCactusB[] = {
    "..L..",
    "..L.L",
    "..LLL",
    "..L..",
    "..L..",
    "..L..",
};

const char* const kCactusC[] = {
    ".L.",
    "LLL",
    ".L.",
    ".L.",
    ".L.",
};

// -- Dead bushes -----------------------------------------------------------

const char* const kDeadBushA[] = {
    "B..B..B",
    ".B.BB.B",
    "..BBBB.",
    "...TT..",
};

const char* const kDeadBushB[] = {
    "B.B.B",
    ".BBB.",
    "..T..",
};

const char* const kDeadBushC[] = {
    "BB..BB",
    ".B..B.",
    "..TT..",
};

// -- Grass tufts -----------------------------------------------------------

const char* const kTuftA[] = {
    "L..L",
    ".LL.",
    "L.LL",
    ".L..",
};

const char* const kTuftB[] = {
    "..L..L",
    ".LL.L.",
    "L.LL.L",
    ".L..L.",
};

const char* const kTuftC[] = {
    "L...L",
    ".L.L.",
    "..L..",
    ".LL.L",
};

// -- Buried ruins ----------------------------------------------------------
// Something bigger was built here. Almost all of it is under the ground now,
// and only a corner or two still shows.

const char* const kBuriedRuinA[] = {
    "NN..NN",
    "NNNNNN",
    "NNNNNN",
    "..PP..",
    ".PPPP.",
    "PPPPPP",
};

const char* const kBuriedRuinB[] = {
    "..S..S..",
    ".SSSSSS.",
    "SSSSSSSS",
    ".PPPPPP.",
    "PPPPPPPP",
};

const char* const kBuriedRuinC[] = {
    ".SS.SS.",
    "SSSSSSS",
    "SSSSSSS",
    ".PPPPP.",
    "PPPPPPP",
};

// -- Bones -----------------------------------------------------------------
// Somebody did not make it here.

const char* const kBonesA[] = {
    "..YYYYY..",
    ".YYYYYYY.",
    "YYYYYYYYY",
    ".YYYYYYY.",
    "..YYYYY..",
    "...YYY...",
    "..Y...Y..",
};

const char* const kBonesB[] = {
    "..YYYY..",
    ".YYYYYY.",
    "YYYYYYYY",
    ".Y....Y.",
    "..YYYY..",
    ".YY..YY.",
    "Y..YY..Y",
    ".YYYYYY.",
};

const char* const kBonesC[] = {
    "....Y....",
    "...YYY...",
    "..YYYYY..",
    ".YYY.YYY.",
    "YYYY.YYYY",
    ".YYY.YYY.",
    "..YYYYY..",
    "...YYY...",
};

// -- The library -----------------------------------------------------------

const StructureSet kKinds[] = {
    {"ruin",        {{kRuinA, 12}, {kRuinB, 11}, {kRuinC, 12}}, 3},
    {"pillar",      {{kPillarA, 7}, {kPillarB, 8}, {kPillarC, 6}}, 3},
    {"tree",        {{kTreeA, 18}, {kTreeB, 20}, {kTreeC, 16}}, 3},
    {"giant tree",  {{kGiantTreeA, 26}, {kGiantTreeB, 28}, {kGiantTreeC, 27}}, 3},
    {"bush",        {{kBushA, 6}, {kBushB, 7}, {kBushC, 8}}, 3},
    {"tuft",        {{kTuftA, 4}, {kTuftB, 4}, {kTuftC, 4}}, 3},
    {"buried ruin", {{kBuriedRuinA, 6}, {kBuriedRuinB, 5}, {kBuriedRuinC, 5}}, 3},
    {"bones",       {{kBonesA, 7}, {kBonesB, 8}, {kBonesC, 8}}, 3},
    // Desert kinds: these only stand on the sand.
    {"cactus",      {{kCactusA, 7}, {kCactusB, 6}, {kCactusC, 5}}, 3},
    {"dead bush",   {{kDeadBushA, 4}, {kDeadBushB, 3}, {kDeadBushC, 3}}, 3},
};

constexpr int kFirstDesertKind = 8;

}  // namespace

int structureKindCount() {
    return static_cast<int>(sizeof(kKinds) / sizeof(kKinds[0]));
}

int structureFirstDesertKind() {
    return kFirstDesertKind;
}

const StructureSet& structureKind(int index) {
    if (index < 0 || index >= structureKindCount()) return kKinds[0];
    return kKinds[index];
}

const StructurePattern& structurePattern(int kind, int variant) {
    const StructureSet& set = structureKind(kind);
    if (set.count <= 0) return set.variants[0];
    int which = variant % set.count;
    if (which < 0) which += set.count;
    return set.variants[which];
}

int structurePatternWidth(const StructurePattern& pattern) {
    int width = 0;
    for (int row = 0; row < pattern.rowCount; ++row) {
        int n = 0;
        while (pattern.rows[row][n] != '\0') ++n;
        if (n > width) width = n;
    }
    return width;
}

int structureLowestFilledRow(const StructurePattern& pattern) {
    for (int row = pattern.rowCount - 1; row >= 0; --row) {
        const char* line = pattern.rows[row];
        for (int column = 0; line[column] != '\0'; ++column) {
            if (structureMaterial(line[column]) != Mat::Air) return row;
        }
    }
    return -1;
}

Mat structureMaterial(char c) {
    switch (c) {
        case 'T': return Mat::Wood;
        case 'B': return Mat::Bark;
        case 'L': return Mat::Foliage;
        case 'S': return Mat::Stone;
        case 'N': return Mat::Limestone;
        case 'X': return Mat::Basalt;
        case 'O': return Mat::Obsidian;
        case 'Y': return Mat::Bones;
        case 'G': return Mat::Gravel;
        case 'C': return Mat::Clay;
        case 'D': return Mat::Sand;
        case 'P': return Mat::PackedDirt;
        case 'M': return Mat::Mycelium;
        case 'F': return Mat::FungalCap;
        default:  return Mat::Air;
    }
}
