#pragma once

// The heads-up display: the little bit of information drawn on top of the
// world. It lives in its own file so the game loop in main.cpp stays about
// the game and not about drawing.

class Hud {
public:
    // The name of the tool the player is holding, top left.
    static void drawTool(const char* toolName);
};
