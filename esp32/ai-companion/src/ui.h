// Everything on screen except the face: top bar, subtitles and the media bar.
#pragma once

#include <Arduino.h>

#include "board.h"

namespace ui {

// Screen layout (pixels)
constexpr int FACE_X = 100;
constexpr int FACE_Y = 44;

enum class Target { None, Face, Prev, PlayPause, Next, Volume };

struct MediaInfo {
  String title;
  String artist;
  bool playing = false;
  int volume = 50;  // 0..100
};

void begin(LGFX& lcd);

void setClock(const String& text);
void setStatus(const String& text, uint16_t color, bool pulse);
void setSubtitle(const String& text);
void setMedia(const MediaInfo& info);
void setPlaying(bool playing);
void setVolume(int volume);
void showPressed(Target t);  // brief highlight on a button

// Redraws whatever changed. Call every frame.
void update(uint32_t nowMs);

Target hitTest(int x, int y);
int volumeFromX(int x);

// Converts UTF-8 text (curly quotes, dashes, emoji...) into plain ASCII
// that the built-in fonts can show.
String toAscii(const String& utf8);

// Status colours
constexpr uint16_t rgb565(uint32_t hex) {
  return ((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F);
}
constexpr uint16_t COLOR_TEXT_DIM = rgb565(0xC9D3DD);
constexpr uint16_t COLOR_LISTEN = rgb565(0xFF7EB3);
constexpr uint16_t COLOR_THINK = rgb565(0xFFD166);
constexpr uint16_t COLOR_ACCENT = rgb565(0x4FE3D6);

}  // namespace ui
