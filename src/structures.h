#pragma once

#include "materials.h"

// The hand-made structures: little pictures, drawn one character at a time,
// that the world generator stands on the ground.
//
// The legend:
//   '.'  nothing            'T'  wood          'B'  bark
//   'L'  foliage            'S'  stone         'N'  limestone
//   'X'  basalt             'O'  obsidian      'Y'  bones
//   'G'  gravel             'C'  clay          'D'  sand
//   'P'  packed dirt        'M'  mycelium      'F'  fungal cap
//
// A kind always has at least three variants, so the same kind never looks the
// same twice in a row.

// One picture: rows of text, top row first.
struct StructurePattern {
    const char* const* rows;
    int rowCount;
};

// All the patterns of one kind of structure.
struct StructureSet {
    const char* kind;
    StructurePattern variants[3];
    int count;
};

// How many kinds of structure there are.
int structureKindCount();

// The first kind that only grows in the desert (cacti, dead bushes). Every kind
// from here to the end of the list is a desert kind.
int structureFirstDesertKind();

// The first kind that only grows in the tundra (pines, tundra shrubs).
int structureFirstTundraKind();

// The patterns for one kind.
const StructureSet& structureKind(int index);

// The pattern for a kind and a variant. The variant wraps around, so any
// number at all works.
const StructurePattern& structurePattern(int kind, int variant);

// How wide a pattern is, in cells.
int structurePatternWidth(const StructurePattern& pattern);

// The lowest row of a pattern that has anything in it.
//
// A pattern whose last row is empty - a ruined wall whose base has eroded away
// - would otherwise stand one row off the ground, hovering on nothing. This
// finds the row that actually touches the dirt, so the pattern can be lined up
// on that one instead.
int structureLowestFilledRow(const StructurePattern& pattern);

// What one character of a pattern means. '.' and anything unknown mean
// "nothing here".
Mat structureMaterial(char c);
