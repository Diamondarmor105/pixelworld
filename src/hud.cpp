#include "hud.h"

#include "raylib.h"

// The heads-up display, drawn in screen pixels on top of the world.

void Hud::drawTool(const char* toolName) {
    DrawText(toolName, 10, 10, 20, WHITE);
}
