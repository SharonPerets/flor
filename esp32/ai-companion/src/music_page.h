// The Music screen's Playing / Browse views on the device: keeps the lists and pictures the
// helper sends, handles touches and draws (music_browse.cpp does the drawing, music_screen.cpp
// the song itself). Since 4.8.19.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "ui.h"

namespace musicpage {

bool begin();  // false: not enough memory

void shown();   // the Music screen came up
void hidden();  // ...and went away (closes the keyboard)
void helperConnected(bool on);

// Messages from the helper: music_list, music_status, lyrics, and dictation / dictate_state
// while the search mic is on. Returns true if it was for this screen.
bool onMessage(const char* type, JsonObjectConst msg);
void onThumb(uint32_t id, const uint8_t* data, size_t len);
void onMedia(const ui::MediaInfo& song, bool newSong);

// Touches on the middle of the Music screen (the deck has its own targets)
bool usesTouch(int x, int y);  // false: main handles it (the song's progress bar)
void touchBegin(int x, int y);
void touchMove(int x, int y);
void touchEnd(int x, int y);
void touchCancel();  // the touch became a swipe to the next screen
bool blocksSwipe();  // the keyboard is open, or the list is being dragged

void markDirty();  // draw everything again
void draw(uint32_t now, const ui::MediaInfo& song, const char* emptyTitle, const char* emptyHint);

}  // namespace musicpage
