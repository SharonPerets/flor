// The screen tabs in the top bar (tap an icon to jump to that screen) and the
// big Teams mute button shown on the face screen during a call.
//
// Drawing only, so it also runs in the PC preview. ui.cpp places them.
#pragma once

#include <stdint.h>

#include "gfx.h"

namespace tabs {

enum Icon : uint8_t { MUSIC, FACE, WEATHER, TIME, SKETCH, CLAUDE };

constexpr int MAX_TABS = 8;
constexpr float GAP = 44;  // distance between icons
constexpr gfx::Rect AREA{262, 0, 276, 44};

// badges: bit i = a small dot on tab i ("something needs you there")
void draw(gfx::Canvas& c, const uint8_t* icons, int count, int current, uint32_t badges);
// The tab at (x, y), or -1
int at(int x, int y, int count);

// The Teams mute button on the right of the face
constexpr float CALL_CX = 750, CALL_CY = 168, CALL_R = 38;
constexpr gfx::Rect CALL_AREA{704, 112, 96, 160};
void drawCallButton(gfx::Canvas& c, bool pressed);
bool onCallButton(int x, int y);

}  // namespace tabs
