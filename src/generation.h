#pragma once

#include "world.h"

// World generation. In slice 1 this only builds a small hand-made scene so
// there is something to look at; the real endless chunk generation replaces
// (and extends) this in slice 6.
void buildTestScene(World& world);
