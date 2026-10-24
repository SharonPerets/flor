// Everything on screen except the face: top bar, subtitles and the media bar.
#pragma once

#include <Arduino.h>

#include "board.h"
#include "gfx.h"

namespace ui {

// Screen layout (pixels)
constexpr int TOP_H = 44;   // the top bar
constexpr int FACE_X = 100;
constexpr int FACE_Y = 56;  // a little lower since 4.3: the music bar folds away

enum class Target { None, Clock, Face, Prev, PlayPause, Next, Volume, Tab, CallMute, Mini, Talk };

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
void begin(LGFX& lcd);

void setClock(const String& text);
// A small countdown next to the clock ("9:41"), shown on every screen while a
// timer or focus session runs. Empty text hides it.
void setTimerChip(const String& text, bool focus);
void setStatus(const String& text, const gfx::Color& color, bool pulse);
// A real "Talk" button in the top right (screens without a face to tap). It shows
// while the status is empty; Listening / Thinking / On a call show instead.
void setTalkButton(bool on);
// No music bar or slim line at the bottom (the Sketch screen uses all of it)
void setBottomHidden(bool hidden);
void setSubtitle(const String& text);
// While Gorgi speaks: show only the first `words` words of the subtitle (-1 = all of it).
// Call after setSubtitle (which shows all of it).
void setSubtitleReveal(int words);
void setMedia(const MediaInfo& info);
void setPlaying(bool playing);
void setVolume(int volume);
void showPressed(Target t);  // brief highlight on a button
// Screens: icons in the top bar (tabs.h), tap one to go there.
void setTabs(const uint8_t* icons, int count);
void setTab(int current);
void setTabBadges(uint32_t badges);  // bit i: a small dot on tab i
int tabAt(int x, int y);             // -1 if none
// The big Teams mute button next to the face (only during a call)
void setCallButton(bool visible);
// The small handle at the top (pull down for the Action Center); dot = there are notifications
void setNoticeDot(bool on);
// The music bar: folded into a slim "now playing" line, opened by tapping it
void openMusicBar();     // opens it for 10 s (again: another 10 s)
bool musicBarOpen();
// Everything was covered (the Action Center): draw all of it again
void redrawAll();
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

// Cleans UTF-8 text for the screen: keeps English and Hebrew letters, turns curly quotes and
// dashes into plain ones, drops what the fonts can't show (emoji, accents, Hebrew vowel marks).
String cleanText(const String& utf8);

// Screen background
constexpr gfx::Color BACKGROUND = gfx::rgb(0x05070A);

// Status colours
constexpr gfx::Color COLOR_TEXT_DIM = gfx::rgb(0xC9D3DD);
constexpr gfx::Color COLOR_LISTEN = gfx::rgb(0xFF7EB3);
constexpr gfx::Color COLOR_THINK = gfx::rgb(0xFFD166);
constexpr gfx::Color COLOR_ACCENT = gfx::rgb(0x4FE3D6);

}  // namespace ui
