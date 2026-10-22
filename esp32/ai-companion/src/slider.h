// Slides whole screens sideways when you swipe.
//
// Before a swipe, the screen you're on and the one coming in are drawn into
// two pictures in PSRAM. While sliding, only those pictures are copied to the
// screen, side by side, a bit further each frame. Nothing is redrawn, so it
// keeps up with your finger.
#pragma once

#include "gfx.h"

namespace slider {

// The part of the screen that slides: between the top bar and the music bar
constexpr gfx::Rect AREA{0, 44, 800, 332};

bool begin();

// 0 = the screen you're on, 1 = the one sliding in. AREA.w * AREA.h pixels.
uint16_t* picture(int which);
void fill(int which, const gfx::Color& c);

// Puts both pictures on the screen, moved sideways by `offset` pixels.
// offset < 0: this screen moves left and the other one comes in from the right.
// offset > 0: this screen moves right and the other one comes in from the left.
void show(int offset);

}  // namespace slider
