#include "hud.h"

#include "raylib.h"

// The heads-up display, drawn in screen pixels on top of the world. The
// health bar is chunky on purpose: it is the same language as the world
// itself, blocks of colour rather than thin lines.

void Hud::drawTool(const char* toolName) {
    DrawText(toolName, 10, 10, 20, WHITE);
}

void Hud::drawHealth(int health, int maxHealth) {
    if (health <= 0 || maxHealth <= 0) return;

    const int width = 120;
    const int height = 14;
    const int x = 10;
    const int y = 36;

    // The empty part of the bar, then the filled part on top of it.
    DrawRectangle(x - 2, y - 2, width + 4, height + 4, Fade(BLACK, 0.6f));
    DrawRectangle(x, y, width, height, Fade(DARKGRAY, 0.85f));

    const float fraction = health / static_cast<float>(maxHealth);
    const int filled = static_cast<int>(width * fraction);
    const Color colour = (fraction > 0.5f) ? LIME : (fraction > 0.25f) ? GOLD : RED;
    DrawRectangle(x, y, filled, height, colour);

    DrawText(TextFormat("%d", health), x + width + 8, y - 3, 16, WHITE);
}
