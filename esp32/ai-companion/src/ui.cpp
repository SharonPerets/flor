#include "ui.h"

#include <math.h>

#include "display.h"
#include "gfont.h"

namespace ui {
namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::Pt;
using gfx::Rect;

const Color BG = gfx::rgb(0x05070A);
const Color PANEL = gfx::rgb(0x10151C);
const Color BORDER = gfx::rgb(0x1F2935);
const Color BTN = gfx::rgb(0x151C25);
const Color BTN_PRESSED = gfx::rgb(0x2A3646);
const Color TILE = gfx::rgb(0x1C2733);
const Color TEXT = gfx::rgb(0xE8EEF4);
const Color TEXT_TITLE = gfx::rgb(0xF2F6FA);
const Color TEXT_MUTED = gfx::rgb(0x9AA7B5);
const Color TRACK = gfx::rgb(0x2A3441);
const Color PLAY_ICON = gfx::rgb(0x05201E);
const Color ACCENT = gfx::rgb(0x4FE3D6);
const Color ACCENT_PRESSED = gfx::rgb(0x8FF0E8);

// Subtitle area
constexpr int SUB_X = 16, SUB_Y = 318, SUB_W = 768, SUB_H = 58;
// Media bar
constexpr int BAR_X = 16, BAR_Y = 376, BAR_W = 768, BAR_H = 88;
constexpr int BAR_CY = BAR_Y + BAR_H / 2;
constexpr int TEXT_X = 106, TEXT_W = 250;
constexpr int PREV_X = 396, PLAY_X = 462, NEXT_X = 528;
constexpr int VOL_X0 = 598, VOL_X1 = 738;

constexpr Rect CLOCK_AREA{0, 0, 300, 44};
constexpr Rect STATUS_AREA{420, 0, 380, 44};
constexpr Rect SUB_AREA{SUB_X, SUB_Y, SUB_W, SUB_H};
constexpr Rect BAR_AREA{BAR_X, BAR_Y, BAR_W, BAR_H};
constexpr Rect TEXT_AREA{TEXT_X - 8, BAR_Y + 4, TEXT_W + 16, BAR_H - 8};
constexpr Rect BUTTON_AREA{PREV_X - 34, BAR_Y + 4, NEXT_X - PREV_X + 68, BAR_H - 8};
constexpr Rect VOLUME_AREA{560, BAR_Y + 4, 224, BAR_H - 8};

String clockText = "Gorgi";
String statusText = "";
Color statusColor = COLOR_TEXT_DIM;
bool statusPulse = false;
String subtitle = "";
MediaInfo media;
Target pressed = Target::None;
uint32_t pressedUntil = 0;

bool dirtyClock = true, dirtyStatus = true, dirtySub = true;
bool dirtyBar = true, dirtyText = true, dirtyButtons = true, dirtyVolume = true;
bool pulseOn = true;
uint32_t nextPulseMs = 0;

// ---------------------------------------------------------------- icons

void iconPrev(Canvas& c, float cx, float cy, const Color& col) {
  c.fillRRect(cx - 9.5f, cy - 8.5f, 3.2f, 17, 1.6f, col);
  const Pt tri[3] = {{cx + 8.5f, cy - 8.5f}, {cx + 8.5f, cy + 8.5f}, {cx - 5.0f, cy}};
  c.fillPolygon(tri, 3, col, 1.0f, 1.6f);
}

void iconNext(Canvas& c, float cx, float cy, const Color& col) {
  c.fillRRect(cx + 6.3f, cy - 8.5f, 3.2f, 17, 1.6f, col);
  const Pt tri[3] = {{cx - 8.5f, cy - 8.5f}, {cx - 8.5f, cy + 8.5f}, {cx + 5.0f, cy}};
  c.fillPolygon(tri, 3, col, 1.0f, 1.6f);
}

void iconPlay(Canvas& c, float cx, float cy, const Color& col) {
  const Pt tri[3] = {{cx - 7.5f, cy - 11.5f}, {cx - 7.5f, cy + 11.5f}, {cx + 11.5f, cy}};
  c.fillPolygon(tri, 3, col, 1.0f, 2.2f);
}

void iconPause(Canvas& c, float cx, float cy, const Color& col) {
  c.fillRRect(cx - 9.0f, cy - 10.5f, 6.0f, 21, 2.0f, col);
  c.fillRRect(cx + 3.0f, cy - 10.5f, 6.0f, 21, 2.0f, col);
}

void iconSpeaker(Canvas& c, float cx, float cy, const Color& col, bool loud) {
  c.fillRRect(cx - 9.0f, cy - 4.0f, 6.0f, 8.0f, 1.2f, col);
  const Pt cone[4] = {{cx - 5.5f, cy - 4.0f}, {cx + 1.5f, cy - 8.5f}, {cx + 1.5f, cy + 8.5f}, {cx - 5.5f, cy + 4.0f}};
  c.fillPolygon(cone, 4, col, 1.0f, 1.0f);
  if (loud) {
    c.strokeArc(cx + 3.0f, cy, 6.5f, 2.2f, -48, 48, col);
    c.strokeArc(cx + 3.0f, cy, 11.5f, 2.2f, -48, 48, col);
  }
}

void iconMic(Canvas& c, float cx, float cy, const Color& col) {
  c.fillCapsule(cx, cy - 6.5f, cx, cy - 1.5f, 3.4f, col);
  c.strokeArc(cx, cy - 1.5f, 6.8f, 1.8f, 15, 165, col);
  c.fillCapsule(cx, cy + 5.0f, cx, cy + 8.0f, 0.9f, col);
}

void iconNote(Canvas& c, float cx, float cy, const Color& col) {
  c.fillCircle(cx - 8.0f, cy + 9.0f, 5.0f, col);
  c.fillCircle(cx + 8.0f, cy + 6.0f, 5.0f, col);
  c.fillCapsule(cx - 3.2f, cy - 10.0f, cx - 3.2f, cy + 9.0f, 1.5f, col);
  c.fillCapsule(cx + 12.8f, cy - 13.0f, cx + 12.8f, cy + 6.0f, 1.5f, col);
  const Pt beam[4] = {{cx - 3.2f, cy - 10.0f}, {cx + 12.8f, cy - 13.0f}, {cx + 12.8f, cy - 7.5f}, {cx - 3.2f, cy - 4.5f}};
  c.fillPolygon(beam, 4, col, 1.0f, 0.8f);
}

// ---------------------------------------------------------------- helpers

// Cuts text to fit a width, adding "..." when needed
String fit(const gfont::Font& f, const String& s, float maxW) {
  if (Canvas::textWidth(f, s.c_str()) <= maxW) return s;
  String t = s;
  while (t.length() > 0 && Canvas::textWidth(f, (t + "...").c_str()) > maxW) t.remove(t.length() - 1);
  return t + "...";
}

// ---------------------------------------------------------------- parts (drawn in screen coordinates)

void drawClock(Canvas& c) { c.text(gfont::TITLE, clockText.c_str(), 20, 30, COLOR_TEXT_DIM); }

void drawStatus(Canvas& c) {
  if (statusText.length() == 0) return;
  const float tw = Canvas::textWidth(gfont::SMALL, statusText.c_str());
  const float w = tw + 52, x = 780 - w;
  c.fillRRect(x, 8, w, 32, 16, BORDER);
  c.fillRRect(x + 1, 9, w - 2, 30, 15, PANEL);
  iconMic(c, x + 21, 24, (statusPulse && !pulseOn) ? BORDER : statusColor);
  c.text(gfont::SMALL, statusText.c_str(), x + 38, 29, statusColor);
}

void drawSubtitle(Canvas& c) {
  // Word wrap into at most two lines
  const float maxW = 700;
  String lines[2];
  int line = 0;
  String word;
  String text = subtitle + " ";
  bool truncated = false;
  for (size_t i = 0; i < text.length(); i++) {
    char ch = text[i];
    if (ch != ' ' && ch != '\n') {
      word += ch;
      continue;
    }
    if (word.length() == 0) continue;
    String candidate = lines[line].length() ? lines[line] + " " + word : word;
    if (Canvas::textWidth(gfont::SUBTITLE, candidate.c_str()) <= maxW) {
      lines[line] = candidate;
    } else if (line == 0) {
      line = 1;
      lines[1] = word;
    } else {
      truncated = true;
      break;
    }
    word = "";
  }
  if (truncated) lines[1] = fit(gfont::SUBTITLE, lines[1] + " ...", maxW);

  auto centered = [&](const String& s, int baseline) {
    const float w = Canvas::textWidth(gfont::SUBTITLE, s.c_str());
    c.text(gfont::SUBTITLE, s.c_str(), SUB_X + (SUB_W - w) * 0.5f, baseline, TEXT);
  };
  if (lines[1].length() == 0) {
    centered(lines[0], SUB_Y + 38);
  } else {
    centered(lines[0], SUB_Y + 26);
    centered(lines[1], SUB_Y + 53);
  }
}

void drawBar(Canvas& c) {
  c.fillRRect(BAR_X, BAR_Y, BAR_W, BAR_H, 20, BORDER);
  c.fillRRect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, 19, PANEL);

  // album tile
  c.fillRRect(30, BAR_CY - 30, 60, 60, 13, TILE);
  iconNote(c, 60, BAR_CY, ACCENT);

  // song
  c.text(gfont::TITLE, fit(gfont::TITLE, media.title, TEXT_W - 4).c_str(), TEXT_X, BAR_CY - 3, TEXT_TITLE);
  c.text(gfont::SMALL, fit(gfont::SMALL, media.artist, TEXT_W - 4).c_str(), TEXT_X, BAR_CY + 21, TEXT_MUTED);

  // buttons
  c.fillCircle(PREV_X, BAR_CY, 26, pressed == Target::Prev ? BTN_PRESSED : BTN);
  iconPrev(c, PREV_X, BAR_CY, TEXT);
  c.fillCircle(PLAY_X, BAR_CY, 30, pressed == Target::PlayPause ? ACCENT_PRESSED : ACCENT);
  if (media.playing)
    iconPause(c, PLAY_X, BAR_CY, PLAY_ICON);
  else
    iconPlay(c, PLAY_X + 2, BAR_CY, PLAY_ICON);
  c.fillCircle(NEXT_X, BAR_CY, 26, pressed == Target::Next ? BTN_PRESSED : BTN);
  iconNext(c, NEXT_X, BAR_CY, TEXT);

  // volume
  iconSpeaker(c, 578, BAR_CY, TEXT_MUTED, false);
  const int v = constrain(media.volume, 0, 100);
  const float kx = VOL_X0 + (VOL_X1 - VOL_X0) * v / 100.0f;
  c.fillRRect(VOL_X0, BAR_CY - 3, VOL_X1 - VOL_X0, 6, 3, TRACK);
  if (kx > VOL_X0) c.fillRRect(VOL_X0, BAR_CY - 3, kx - VOL_X0, 6, 3, ACCENT);
  c.fillCircle(kx, BAR_CY + 1, 12, BG, 0.35f);  // soft shadow
  c.fillCircle(kx, BAR_CY, 10, TEXT);
  iconSpeaker(c, 760, BAR_CY, TEXT_MUTED, true);
}

void redraw(const Rect& area, const Color& bg, void (*draw)(Canvas&)) {
  display::render(area, bg, [&](Canvas& c) { draw(c); });
}

}  // namespace

void begin(LGFX&) {
  display::render(Rect{0, 0, SCREEN_W, SCREEN_H}, BG, [](Canvas&) {});
  dirtyClock = dirtyStatus = dirtySub = dirtyBar = dirtyText = dirtyButtons = dirtyVolume = true;
}

void setClock(const String& text) {
  if (text == clockText) return;
  clockText = text;
  dirtyClock = true;
}

void setStatus(const String& text, const Color& color, bool pulse) {
  if (text == statusText && color.c == statusColor.c && pulse == statusPulse) return;
  statusText = text;
  statusColor = color;
  statusPulse = pulse;
  pulseOn = true;
  dirtyStatus = true;
}

void setSubtitle(const String& text) {
  String t = toAscii(text);
  if (t == subtitle) return;
  subtitle = t;
  dirtySub = true;
}

void setMedia(const MediaInfo& info) {
  String title = toAscii(info.title), artist = toAscii(info.artist);
  if (title != media.title || artist != media.artist) dirtyText = true;
  if (info.playing != media.playing) dirtyButtons = true;
  if (info.volume != media.volume) dirtyVolume = true;
  media = info;
  media.title = title;
  media.artist = artist;
}

void setPlaying(bool playing) {
  if (playing == media.playing) return;
  media.playing = playing;
  dirtyButtons = true;
}

void setVolume(int volume) {
  volume = constrain(volume, 0, 100);
  if (volume == media.volume) return;
  media.volume = volume;
  dirtyVolume = true;
}

void showPressed(Target t) {
  if (t != Target::Prev && t != Target::PlayPause && t != Target::Next) return;
  pressed = t;
  pressedUntil = millis() + 180;
  dirtyButtons = true;
}

void update(uint32_t now) {
  if (pressed != Target::None && (int32_t)(now - pressedUntil) >= 0) {
    pressed = Target::None;
    dirtyButtons = true;
  }
  if (statusPulse && (int32_t)(now - nextPulseMs) >= 0) {
    pulseOn = !pulseOn;
    nextPulseMs = now + 450;
    dirtyStatus = true;
  }
  if (dirtyClock) { redraw(CLOCK_AREA, BG, drawClock); dirtyClock = false; }
  if (dirtyStatus) { redraw(STATUS_AREA, BG, drawStatus); dirtyStatus = false; }
  if (dirtySub) { redraw(SUB_AREA, BG, drawSubtitle); dirtySub = false; }
  if (dirtyBar) {  // the whole bar (first time): it also covers the parts below
    redraw(BAR_AREA, BG, drawBar);
    dirtyBar = dirtyText = dirtyButtons = dirtyVolume = false;
  }
  if (dirtyText) { redraw(TEXT_AREA, BG, drawBar); dirtyText = false; }
  if (dirtyButtons) { redraw(BUTTON_AREA, BG, drawBar); dirtyButtons = false; }
  if (dirtyVolume) { redraw(VOLUME_AREA, BG, drawBar); dirtyVolume = false; }
}

Target hitTest(int x, int y) {
  if (y >= BAR_Y - 8) {
    auto near = [&](int cx, int r) { return abs(x - cx) <= r && abs(y - BAR_CY) <= r; };
    if (near(PREV_X, 32)) return Target::Prev;
    if (near(PLAY_X, 34)) return Target::PlayPause;
    if (near(NEXT_X, 32)) return Target::Next;
    if (x >= VOL_X0 - 20 && x <= VOL_X1 + 20) return Target::Volume;
    return Target::None;
  }
  if (y < FACE_Y && x < 240) return Target::Clock;
  if (y >= FACE_Y && y < SUB_Y + SUB_H) return Target::Face;
  return Target::None;
}

int volumeFromX(int x) {
  return constrain((x - VOL_X0) * 100 / (VOL_X1 - VOL_X0), 0, 100);
}

String toAscii(const String& in) {
  String out;
  out.reserve(in.length());
  const uint8_t* s = (const uint8_t*)in.c_str();
  size_t n = in.length();
  for (size_t i = 0; i < n;) {
    uint8_t c = s[i];
    uint32_t cp;
    int len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
    else { i++; continue; }
    if (i + len > n) break;
    for (int k = 1; k < len; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    i += len;

    if (cp == '\r' || cp == '\t') out += ' ';
    else if (cp < 0x80) out += (char)cp;
    else if (cp == 0x2018 || cp == 0x2019) out += '\'';
    else if (cp == 0x201C || cp == 0x201D) out += '"';
    else if (cp == 0x2013 || cp == 0x2014) out += '-';
    else if (cp == 0x2026) out += "...";
    else if (cp == 0xA0) out += ' ';
    // anything else (emoji, accents) is dropped
  }
  return out;
}

}  // namespace ui
