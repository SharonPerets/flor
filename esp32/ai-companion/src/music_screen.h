// The music screen (swipe right from the face): album art, the song, and how
// far into it you are. The music bar under it keeps the buttons.
#pragma once

#include <Arduino.h>

#include "gfx.h"
#include "ui.h"

namespace music {

constexpr int ART = 240;  // album art is ART x ART pixels

bool begin();

// Album art arrives from the helper in pieces (RGB565, row after row).
// Returns true when a picture is complete.
bool artChunk(uint32_t id, uint32_t total, uint32_t offset, const uint8_t* data, size_t len);
bool artReady(uint32_t id);

// emptyTitle / emptyHint are shown when nothing is playing.
void draw(gfx::Canvas& c, const ui::MediaInfo& song, uint32_t nowMs, const char* emptyTitle, const char* emptyHint);

// The progress bar and times, redrawn every second while a song plays
constexpr gfx::Rect PROGRESS_AREA{320, 280, 470, 60};
// Changes whenever the progress bar would look different (-1 = no bar)
int progressKey(const ui::MediaInfo& song, uint32_t nowMs);

}  // namespace music
