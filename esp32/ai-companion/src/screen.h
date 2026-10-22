// The LCD panel, with two pictures ("double buffering").
//
// The panel shows one picture while Gorgi draws the next one into the other.
// present() swaps them exactly when the panel starts a new refresh, so you
// never see a half-drawn picture (no tearing when screens slide).
//
// The panel is fed from a small buffer in the chip's fast internal RAM
// ("bounce buffer") instead of straight from PSRAM, so heavy drawing can no
// longer make the picture drift sideways.
#pragma once

#include <stdint.h>

#include "gfx.h"

namespace screen {

// Turns the panel on (black). Call once at startup.
bool begin();

// Puts pixels into the picture being prepared. px holds h rows of w pixels,
// `stride` pixels apart. Nothing shows until present().
void write(int x, int y, int w, int h, const uint16_t* px, int stride);

// Tells the screen that all of `area` will be redrawn before the next
// present(), so it doesn't need to prepare it (saves time when sliding).
// Call before the first write of a frame.
void willCover(const gfx::Rect& area);

// Shows everything written since the last present(), at the next refresh.
void present();

struct Stats {
  uint32_t frames = 0;  // pictures shown
  uint32_t waitUs = 0;  // time spent waiting for the panel
  uint32_t syncUs = 0;  // time spent bringing the hidden picture up to date
};
Stats takeStats();

}  // namespace screen
