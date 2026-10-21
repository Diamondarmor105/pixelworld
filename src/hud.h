#pragma once

// The heads-up display: the little bit of information drawn on top of the
// world. It lives in its own file so the game loop in main.cpp stays about
// the game and not about drawing, and so the health bar has somewhere to go
// when the player can be hurt.

class Hud {
public:
    // The name of the tool the player is holding, top left.
    static void drawTool(const char* toolName);

    // The health bar, just under the tool name. A health of zero or less is
    // not drawn at all.
    static void drawHealth(int health, int maxHealth);
};
