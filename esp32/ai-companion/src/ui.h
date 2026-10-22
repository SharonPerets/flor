// Everything on screen except the face: top bar, subtitles and the media bar.
#pragma once

#include <Arduino.h>

#include "board.h"
#include "gfx.h"

namespace ui {

// Screen layout (pixels)
constexpr int FACE_X = 100;
constexpr int FACE_Y = 44;

enum class Target { None, Clock, Face, Prev, PlayPause, Next, Volume };

struct MediaInfo {
  String title;
  String artist;
  String album;
  bool playing = false;
  int volume = 50;  // 0..100
  // How far into the song (seconds), as of millis() == posAtMs. Not every app reports it.
  bool hasPos = false;
  float pos = 0, dur = 0;
  uint32_t posAtMs = 0;
  uint32_t artId = 0;  // album art sent by the helper (0 = none)
};

// Draws the empty screen background. Call once, after display::begin().
void begin();

void setClock(const String& text);
void setStatus(const String& text, const gfx::Color& color, bool pulse);
void setSubtitle(const String& text);
void setMedia(const MediaInfo& info);
void setPlaying(bool playing);
void setVolume(int volume);
void showPressed(Target t);  // brief highlight on a button
// Screens: dots in the top bar show which one is on. position is the screen's
// number (0..count-1); in between while sliding.
void setPages(int count, float position);
void setPagePosition(float position);
// Draws the subtitle right away (used to draw the face screen into a picture)
void drawSubtitleNow();
// The subtitle only shows on the face screen
void setSubtitleVisible(bool visible);
// Shows a line of text with a progress bar under the face, right away
// (used while a firmware update arrives, when nothing else gets drawn).
void showProgress(const String& text, int percent);

// Redraws whatever changed. Call every frame.
void update(uint32_t nowMs);

Target hitTest(int x, int y);
int volumeFromX(int x);

// Converts UTF-8 text (curly quotes, dashes, emoji...) into plain ASCII
// that the fonts can show.
String toAscii(const String& utf8);

// Screen background
constexpr gfx::Color BACKGROUND = gfx::rgb(0x05070A);

// Status colours
constexpr gfx::Color COLOR_TEXT_DIM = gfx::rgb(0xC9D3DD);
constexpr gfx::Color COLOR_LISTEN = gfx::rgb(0xFF7EB3);
constexpr gfx::Color COLOR_THINK = gfx::rgb(0xFFD166);
constexpr gfx::Color COLOR_ACCENT = gfx::rgb(0x4FE3D6);

}  // namespace ui
