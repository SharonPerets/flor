#include "ui.h"

#include <math.h>

#include "bidi.h"
#include "display.h"
#include "gfont.h"
#include "panel.h"
#include "tabs.h"

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
constexpr int SUB_X = 16, SUB_Y = 330, SUB_W = 768, SUB_H = 58;
// Media bar
constexpr int BAR_X = 16, BAR_Y = 392, BAR_W = 768, BAR_H = 84;
constexpr int BAR_CY = BAR_Y + BAR_H / 2;
constexpr int TEXT_X = 106, TEXT_W = 250;
constexpr int PREV_X = 396, PLAY_X = 462, NEXT_X = 528;
constexpr int VOL_X0 = 598, VOL_X1 = 738;

constexpr Rect CLOCK_AREA{0, 0, 248, 44};  // stops before the tabs (it used to wipe the first icon)
constexpr Rect STATUS_AREA{540, 0, 260, 44};
constexpr Rect SUB_AREA{SUB_X, SUB_Y, SUB_W, SUB_H};
constexpr Rect BAR_AREA{BAR_X, BAR_Y, BAR_W, BAR_H};
constexpr Rect TEXT_AREA{TEXT_X - 8, BAR_Y + 4, TEXT_W + 16, BAR_H - 8};
constexpr Rect BUTTON_AREA{PREV_X - 34, BAR_Y + 4, NEXT_X - PREV_X + 68, BAR_H - 8};
constexpr Rect VOLUME_AREA{560, BAR_Y + 4, 224, BAR_H - 8};
// The Music screen's own bar ("deck"): bigger buttons and a wide volume slider, always open
constexpr int DECK_PREV_X = 80, DECK_PLAY_X = 146, DECK_NEXT_X = 212;
constexpr int DECK_LIKE_X = 292, DECK_DISLIKE_X = 352;  // thumbs up / down (YouTube Music, since 4.9)
constexpr int DECK_VOL_X0 = 440, DECK_VOL_X1 = 690;
constexpr Rect DECK_BUTTON_AREA{40, BAR_Y + 4, 352, BAR_H - 8};
constexpr Rect DECK_VOLUME_AREA{392, BAR_Y + 4, 384, BAR_H - 8};

String clockText = "Gorgi";
String chipText = "";
bool chipFocus = false;
const Color FOCUS_C = gfx::rgb(0xB69CFF);
String statusText = "";
Color statusColor = COLOR_TEXT_DIM;
bool statusPulse = false;
bool talkButton = false;
bool bottomHidden = false;
constexpr float TALK_W = 104;  // the Talk button, right-aligned at x 780
String subtitle = "";
int subtitleReveal = -1;  // words of the subtitle shown so far (-1 = all)
int progress = -1;  // progress bar under the subtitle, -1 = none
MediaInfo media;
Target pressed = Target::None;
uint32_t pressedUntil = 0;

uint8_t tabIcons[tabs::MAX_TABS];
int tabCount = 0, tabCurrent = 0;
uint32_t tabBadges = 0;
bool callVisible = false, callShown = false, dirtyCall = false;
bool noticeDot = false;
// The round Talk button left of the face (since 4.8.8: tapping the face itself does nothing)
constexpr float FT_CX = 50, FT_CY = 186, FT_R = 36;
constexpr Rect FACE_TALK_AREA{0, 128, 100, 136};
const Color STOP_RED = gfx::rgb(0xE5484D);
bool faceTalkActive = false;  // a conversation is on: the button says Stop
bool dirtyFaceTalk = true;
bool barOpen = false;
bool deck = false;  // the Music screen: the big bar stays open
uint32_t barOpenUntil = 0;
constexpr uint32_t BAR_OPEN_MS = 10000;
constexpr Rect BOTTOM_AREA{0, 388, 800, 92};  // the music bar or the slim line
const Color LINE = gfx::rgb(0x161D27);
bool subtitleVisible = true;
bool dirtyTabs = true;
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

// Thumbs up (or down: turned around), from a 24x24 icon grid, `size` px tall
void iconThumb(Canvas& c, float cx, float cy, bool up, const Color& col, float size = 24) {
  const float k = size / 24;
  auto P = [&](float x, float y) {
    if (!up) { x = 24 - x; y = 24 - y; }
    return Pt{cx + (x - 12) * k, cy + (y - 12) * k};
  };
  const Pt cuff[4] = {P(1, 9.5f), P(5, 9.5f), P(5, 21), P(1, 21)};
  const Pt finger[6] = {P(7, 9.2f), P(13.4f, 2.2f), P(14.9f, 2.4f), P(15.8f, 3.8f), P(14.8f, 9), P(7, 11)};
  const Pt palm[8] = {P(7, 9.5f), P(14, 8.2f), P(21.2f, 8.2f), P(23, 10), P(23, 12), P(19.9f, 19.9f), P(18, 21), P(7, 21)};
  c.fillPolygon(cuff, 4, col, 1.0f, 0.8f * k);
  c.fillPolygon(finger, 6, col, 1.0f, 1.0f * k);
  c.fillPolygon(palm, 8, col, 1.0f, 1.2f * k);
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
  while (t.length() > 0 && Canvas::textWidth(f, (t + "...").c_str()) > maxW) t.remove(bidi::dropLast(t.c_str(), t.length()));
  return t + "...";
}

// ---------------------------------------------------------------- parts (drawn in screen coordinates)

void drawClock(Canvas& c) {
  c.fillRect(0, tabs::LINE_Y, 800, 1, LINE);
  c.text(gfont::TITLE, clockText.c_str(), 20, 30, COLOR_TEXT_DIM);
  if (chipText.length() == 0) return;
  // countdown chip: a little ring and the time left
  const Color& col = chipFocus ? FOCUS_C : ACCENT;
  const float x = 20 + Canvas::textWidth(gfont::TITLE, clockText.c_str()) + 16;
  const float w = Canvas::textWidth(gfont::SMALL, chipText.c_str()) + 46;
  c.fillRRect(x, 8, w, 30, 15, BORDER);
  c.fillRRect(x + 1, 9, w - 2, 28, 14, PANEL);
  c.strokeArc(x + 17, 23, 6.5f, 2.2f, 0, 360, TRACK);
  c.strokeArc(x + 17, 23, 6.5f, 2.2f, -90, 150, col);
  c.text(gfont::SMALL, chipText.c_str(), x + 32, 28, col);
}

void drawStatus(Canvas& c) {
  c.fillRect(0, tabs::LINE_Y, 800, 1, LINE);
  if (statusText.length() == 0) {
    if (!talkButton) return;
    const float x = 780 - TALK_W;
    c.fillRRect(x, 6, TALK_W, 34, 17, ACCENT);
    iconMic(c, x + 24, 24, PLAY_ICON);
    c.text(gfont::SMALL, "Talk", x + 42, 29, PLAY_ICON);
    return;
  }
  const String text = fit(gfont::SMALL, statusText, 190);
  const float tw = Canvas::textWidth(gfont::SMALL, text.c_str());
  const float w = tw + 52, x = 780 - w;
  c.fillRRect(x, 8, w, 32, 16, BORDER);
  c.fillRRect(x + 1, 9, w - 2, 30, 15, PANEL);
  iconMic(c, x + 21, 24, (statusPulse && !pulseOn) ? BORDER : statusColor);
  c.text(gfont::SMALL, text.c_str(), x + 38, 29, statusColor);
}

void drawFaceTalk(Canvas& c) {
  const bool down = pressed == Target::Talk;
  const char* label = faceTalkActive ? "Stop" : "Talk";
  if (faceTalkActive) {
    c.fillCircle(FT_CX, FT_CY, FT_R + 6, STOP_RED, 0.18f);
    c.fillCircle(FT_CX, FT_CY, FT_R, down ? gfx::rgb(0xF07A7E) : STOP_RED);
    c.fillRRect(FT_CX - 11, FT_CY - 11, 22, 22, 5, TEXT_TITLE);
  } else {
    c.fillCircle(FT_CX, FT_CY, FT_R + 6, ACCENT, 0.18f);
    c.fillCircle(FT_CX, FT_CY, FT_R, down ? ACCENT_PRESSED : ACCENT);
    // a big microphone
    c.fillCapsule(FT_CX, FT_CY - 13, FT_CX, FT_CY - 2, 7.5f, PLAY_ICON);
    c.strokeArc(FT_CX, FT_CY - 3, 14, 3.2f, 15, 165, PLAY_ICON);
    c.fillCapsule(FT_CX, FT_CY + 11, FT_CX, FT_CY + 17, 1.8f, PLAY_ICON);
    c.fillCapsule(FT_CX - 7, FT_CY + 18, FT_CX + 7, FT_CY + 18, 1.8f, PLAY_ICON);
  }
  c.text(gfont::SMALL, label, FT_CX - Canvas::textWidth(gfont::SMALL, label) * 0.5f, (int)(FT_CY + FT_R + 28),
         TEXT_MUTED);
}

void drawTabs(Canvas& c) {
  tabs::draw(c, tabIcons, tabCount, tabCurrent, tabBadges);
  panel::drawHandle(c, noticeDot);
}

void drawCall(Canvas& c) {
  if (callVisible) tabs::drawCallButton(c, pressed == Target::CallMute);
}

void drawSubtitle(Canvas& c) {
  if (!subtitleVisible) return;
  if (progress >= 0) {
    const float w = Canvas::textWidth(gfont::SUBTITLE, subtitle.c_str());
    c.text(gfont::SUBTITLE, subtitle.c_str(), SUB_X + (SUB_W - w) * 0.5f, SUB_Y + 24, TEXT);
    const float bx = SUB_X + (SUB_W - 400) * 0.5f, by = SUB_Y + 38;
    c.fillRRect(bx, by, 400, 10, 5, TRACK);
    if (progress > 0) c.fillRRect(bx, by, 400 * gfx::imin(progress, 100) / 100.0f, 10, 5, ACCENT);
    return;
  }
  // Word wrap the whole text into lines. Two lines show at a time; while Gorgi speaks, the words
  // appear as they are said (subtitleReveal = words shown) and a long reply turns to its next two lines.
  const float maxW = 700;
  constexpr int MAX_LINES = 8;
  String lines[MAX_LINES];
  int lineWords[MAX_LINES] = {0};  // words on each line
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
    if (Canvas::textWidth(gfont::SUBTITLE, candidate.c_str()) <= maxW || lines[line].length() == 0) {
      lines[line] = candidate;
      ++lineWords[line];
    } else if (line + 1 < MAX_LINES) {
      ++line;
      lines[line] = word;
      lineWords[line] = 1;
    } else {
      truncated = true;
      break;
    }
    word = "";
  }
  const int lineCount = line + 1;
  // which two lines: the ones with the newest spoken word
  int first = 0;
  if (subtitleReveal >= 0) {
    int words = 0, at = 0;
    for (int l = 0; l < lineCount; ++l) {
      words += lineWords[l];
      at = l;
      if (words >= subtitleReveal) break;
    }
    first = (at / 2) * 2;
  } else if (lineCount > 2) {
    truncated = true;  // (all of it at once: the first two lines)
  }
  String show[2] = {lines[first], first + 1 < lineCount ? lines[first + 1] : String("")};
  if (truncated && subtitleReveal < 0 && show[1].length()) show[1] = fit(gfont::SUBTITLE, show[1] + " ...", maxW);
  // words before this page, then cut each line to the words spoken so far
  int before = 0;
  for (int l = 0; l < first; ++l) before += lineWords[l];
  int visible[2] = {-1, -1};  // characters of each line to draw (-1 = all)
  if (subtitleReveal >= 0) {
    int left = subtitleReveal - before;
    for (int k = 0; k < 2; ++k) {
      const int n = first + k < lineCount ? lineWords[first + k] : 0;
      if (left >= n) {
        left -= n;
        continue;
      }
      // the first `left` words of this line
      int chars = 0, seen = 0;
      while (seen < left && chars < (int)show[k].length()) {
        while (chars < (int)show[k].length() && show[k][chars] != ' ') ++chars;
        ++seen;
        if (seen < left) ++chars;  // the space
      }
      visible[k] = left > 0 ? chars : 0;
      left = 0;
    }
  }

  auto centered = [&](const String& s, int baseline, int chars) {
    const float w = Canvas::textWidth(gfont::SUBTITLE, s.c_str());
    if (chars == 0) return;
    const String part = chars < 0 ? s : s.substring(0, chars);
    c.text(gfont::SUBTITLE, part.c_str(), SUB_X + (SUB_W - w) * 0.5f, baseline, TEXT);
  };
  if (show[1].length() == 0) {
    centered(show[0], SUB_Y + 38, visible[0]);
  } else {
    centered(show[0], SUB_Y + 26, visible[0]);
    centered(show[1], SUB_Y + 53, visible[1]);
  }
}

void drawBar(Canvas& c) {
  c.fillRRect(BAR_X, BAR_Y, BAR_W, BAR_H, 20, BORDER);
  c.fillRRect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, 19, PANEL);

  // album tile
  c.fillRRect(30, BAR_CY - 30, 60, 60, 13, TILE);
  iconNote(c, 60, BAR_CY, ACCENT);

  // song
  c.textIn(gfont::TITLE, fit(gfont::TITLE, media.title, TEXT_W - 4).c_str(), TEXT_X, TEXT_W - 4, BAR_CY - 3, TEXT_TITLE);
  c.textIn(gfont::SMALL, fit(gfont::SMALL, media.artist, TEXT_W - 4).c_str(), TEXT_X, TEXT_W - 4, BAR_CY + 21, TEXT_MUTED);

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

void drawDeck(Canvas& c) {
  c.fillRRect(BAR_X, BAR_Y, BAR_W, BAR_H, 20, BORDER);
  c.fillRRect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, 19, PANEL);

  // buttons (a little bigger than in the slim bar)
  c.fillCircle(DECK_PREV_X, BAR_CY, 28, pressed == Target::Prev ? BTN_PRESSED : BTN);
  iconPrev(c, DECK_PREV_X, BAR_CY, TEXT);
  c.fillCircle(DECK_PLAY_X, BAR_CY, 32, pressed == Target::PlayPause ? ACCENT_PRESSED : ACCENT);
  if (media.playing)
    iconPause(c, DECK_PLAY_X, BAR_CY, PLAY_ICON);
  else
    iconPlay(c, DECK_PLAY_X + 2, BAR_CY, PLAY_ICON);
  c.fillCircle(DECK_NEXT_X, BAR_CY, 28, pressed == Target::Next ? BTN_PRESSED : BTN);
  iconNext(c, DECK_NEXT_X, BAR_CY, TEXT);

  // thumbs up (teal when the song is in your Liked songs) and down
  const bool liked = media.liked == 1;
  c.fillCircle(DECK_LIKE_X, BAR_CY, 26,
               pressed == Target::Like ? (liked ? ACCENT_PRESSED : BTN_PRESSED) : (liked ? ACCENT : BTN));
  iconThumb(c, DECK_LIKE_X, BAR_CY, true, liked ? PLAY_ICON : TEXT);
  c.fillCircle(DECK_DISLIKE_X, BAR_CY, 26, pressed == Target::Dislike ? BTN_PRESSED : BTN);
  iconThumb(c, DECK_DISLIKE_X, BAR_CY, false, TEXT);

  // volume: wide slider, big knob, and the number
  iconSpeaker(c, 418, BAR_CY, TEXT_MUTED, false);
  const int v = constrain(media.volume, 0, 100);
  const float kx = DECK_VOL_X0 + (DECK_VOL_X1 - DECK_VOL_X0) * v / 100.0f;
  c.fillRRect(DECK_VOL_X0, BAR_CY - 5, DECK_VOL_X1 - DECK_VOL_X0, 10, 5, TRACK);
  if (kx > DECK_VOL_X0) c.fillRRect(DECK_VOL_X0, BAR_CY - 5, kx - DECK_VOL_X0, 10, 5, ACCENT);
  c.fillCircle(kx, BAR_CY + 1, 16, BG, 0.35f);  // soft shadow
  c.fillCircle(kx, BAR_CY, 14, TEXT);
  iconSpeaker(c, 714, BAR_CY, TEXT_MUTED, true);
  char pct[8];
  snprintf(pct, sizeof pct, "%d%%", v);
  c.text(gfont::SMALL, pct, 734, BAR_CY + 7, TEXT_MUTED);
}

void drawMini(Canvas& c) { tabs::drawMiniPlayer(c, media.title.c_str(), media.artist.c_str(), media.playing); }

void redraw(const Rect& area, const Color& bg, void (*draw)(Canvas&)) {
  display::render(area, bg, [&](Canvas& c) { draw(c); });
}

}  // namespace

void begin(LGFX&) {
  display::render(Rect{0, 0, SCREEN_W, SCREEN_H}, BG, [](Canvas&) {});
  dirtyClock = dirtyStatus = dirtySub = dirtyBar = dirtyText = dirtyButtons = dirtyVolume = dirtyTabs = true;
}

void setTabs(const uint8_t* icons, int count) {
  tabCount = count < tabs::MAX_TABS ? count : tabs::MAX_TABS;
  for (int i = 0; i < tabCount; ++i) tabIcons[i] = icons[i];
  dirtyTabs = true;
}

void setTab(int current) {
  if (current == tabCurrent) return;
  tabCurrent = current;
  dirtyTabs = true;
}

void setTabBadges(uint32_t badges) {
  if (badges == tabBadges) return;
  tabBadges = badges;
  dirtyTabs = true;
}

int tabAt(int x, int y) { return tabs::at(x, y, tabCount); }

void setNoticeDot(bool on) {
  if (on == noticeDot) return;
  noticeDot = on;
  dirtyTabs = true;
}

void openMusicBar() {
  barOpenUntil = millis() + BAR_OPEN_MS;
  if (barOpen) return;
  barOpen = true;
  dirtyBar = true;
}

bool musicBarOpen() { return barOpen || deck; }

void setMusicDeck(bool on) {
  if (on == deck) return;
  deck = on;
  dirtyBar = true;
}

void redrawAll() {
  display::render(Rect{0, 0, SCREEN_W, TOP_H}, BG, [](Canvas&) {});
  display::render(Rect{0, subtitleVisible ? SUB_Y + SUB_H : 376, SCREEN_W, SCREEN_H}, BG, [](Canvas&) {});
  dirtyClock = dirtyStatus = dirtyTabs = dirtySub = dirtyBar = dirtyCall = dirtyFaceTalk = true;
}

void setCallButton(bool visible) {
  if (visible == callVisible) return;
  callVisible = visible;
  dirtyCall = true;
}

void drawSubtitleNow() {
  const bool was = subtitleVisible;
  subtitleVisible = true;
  redraw(SUB_AREA, BG, drawSubtitle);
  subtitleVisible = was;
}

void setSubtitleVisible(bool visible) {
  if (visible == subtitleVisible) return;
  subtitleVisible = visible;
  if (visible) dirtySub = dirtyCall = dirtyFaceTalk = true;  // when hidden, the other screen draws over it
}

void setClock(const String& text) {
  if (text == clockText) return;
  clockText = text;
  dirtyClock = true;
}

void setTimerChip(const String& text, bool focus) {
  if (text == chipText && focus == chipFocus) return;
  chipText = text;
  chipFocus = focus;
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

void setBottomHidden(bool hidden) {
  if (hidden == bottomHidden) return;
  bottomHidden = hidden;
  if (!hidden) dirtyBar = true;
}

void setFaceTalkActive(bool active) {
  if (active == faceTalkActive) return;
  faceTalkActive = active;
  dirtyFaceTalk = true;
}

void setTalkButton(bool on) {
  if (on == talkButton) return;
  talkButton = on;
  dirtyStatus = true;
}

void setSubtitle(const String& text) {
  String t = toAscii(text);
  if (t == subtitle && progress < 0 && subtitleReveal < 0) return;
  subtitle = t;
  progress = -1;
  subtitleReveal = -1;
  dirtySub = true;
}

void setSubtitleReveal(int words) {
  if (words == subtitleReveal) return;
  subtitleReveal = words;
  dirtySub = true;
}

void showProgress(const String& text, int percent) {
  subtitle = toAscii(text);
  progress = percent < 0 ? 0 : percent;
  redraw(SUB_AREA, BG, drawSubtitle);
  dirtySub = false;
}

void setMedia(const MediaInfo& info) {
  String title = toAscii(info.title, true), artist = toAscii(info.artist, true);  // song names may be Hebrew
  if (title != media.title || artist != media.artist) dirtyText = true;
  if (info.playing != media.playing) dirtyButtons = true;
  if (info.volume != media.volume) dirtyVolume = true;
  if (info.liked != media.liked) dirtyButtons = true;
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

void setLiked(int8_t liked) {
  if (liked == media.liked) return;
  media.liked = liked;
  dirtyButtons = true;
}

void showPressed(Target t) {
  if (t != Target::Prev && t != Target::PlayPause && t != Target::Next && t != Target::CallMute &&
      !(deck && (t == Target::Like || t == Target::Dislike)) &&
      !(t == Target::Talk && subtitleVisible))
    return;
  pressed = t;
  pressedUntil = millis() + 180;
  if (t == Target::CallMute) dirtyCall = true;
  else if (t == Target::Talk) dirtyFaceTalk = true;
  else dirtyButtons = true;
}

void update(uint32_t now) {
  if (pressed != Target::None && (int32_t)(now - pressedUntil) >= 0) {
    if (pressed == Target::CallMute) dirtyCall = true;
    else if (pressed == Target::Talk) dirtyFaceTalk = true;
    else dirtyButtons = true;
    pressed = Target::None;
  }
  if (statusPulse && (int32_t)(now - nextPulseMs) >= 0) {
    pulseOn = !pulseOn;
    nextPulseMs = now + 450;
    dirtyStatus = true;
  }
  if (dirtyClock) { redraw(CLOCK_AREA, BG, drawClock); dirtyClock = false; }
  if (dirtyStatus) { redraw(STATUS_AREA, BG, drawStatus); dirtyStatus = false; }
  if (dirtyTabs) { redraw(tabs::AREA, BG, drawTabs); dirtyTabs = false; }
  // the call button sits next to the face, so only on the face screen
  if (dirtyCall && subtitleVisible && (callVisible || callShown)) {
    redraw(tabs::CALL_AREA, BG, drawCall);
    callShown = callVisible;
  }
  if (subtitleVisible) dirtyCall = false;
  if (dirtyFaceTalk && subtitleVisible) { redraw(FACE_TALK_AREA, BG, drawFaceTalk); dirtyFaceTalk = false; }
  if (dirtySub && subtitleVisible) { redraw(SUB_AREA, BG, drawSubtitle); dirtySub = false; }
  if (bottomHidden) {  // the screen draws down there itself
    dirtyText = dirtyButtons = dirtyVolume = false;
    return;
  }
  if (deck) {  // the Music screen's own bar: never folds away
    if (dirtyBar) redraw(BOTTOM_AREA, BG, drawDeck);
    if (!dirtyBar && dirtyButtons) redraw(DECK_BUTTON_AREA, BG, drawDeck);
    if (!dirtyBar && dirtyVolume) redraw(DECK_VOLUME_AREA, BG, drawDeck);
    dirtyBar = dirtyText = dirtyButtons = dirtyVolume = false;
    return;
  }
  if (barOpen && (int32_t)(now - barOpenUntil) >= 0) {  // fold the bar away again
    barOpen = false;
    dirtyBar = true;
  }
  if (dirtyBar) {  // the whole bar, or the slim line: it also covers the parts below
    redraw(BOTTOM_AREA, BG, barOpen ? drawBar : drawMini);
    dirtyBar = dirtyText = dirtyButtons = dirtyVolume = false;
  }
  if (!barOpen) {  // the slim line shows the song and play / pause only
    if (dirtyText || dirtyButtons) redraw(tabs::MINI, BG, drawMini);
    dirtyText = dirtyButtons = dirtyVolume = false;
  }
  if (dirtyText) { redraw(TEXT_AREA, BG, drawBar); dirtyText = false; }
  if (dirtyButtons) { redraw(BUTTON_AREA, BG, drawBar); dirtyButtons = false; }
  if (dirtyVolume) { redraw(VOLUME_AREA, BG, drawBar); dirtyVolume = false; }
}

Target hitTest(int x, int y) {
  if (bottomHidden) {
    if (y < TOP_H && x < 240) return Target::Clock;
    if (y < TOP_H && tabAt(x, y) >= 0) return Target::Tab;
    if (y < TOP_H && talkButton && x >= 780 - TALK_W - 8) return Target::Talk;
    return y >= TOP_H ? Target::Face : Target::None;
  }
  if (deck && y >= BAR_Y - 8) {
    auto near = [&](int cx, int r) { return abs(x - cx) <= r && abs(y - BAR_CY) <= r; };
    if (near(DECK_PREV_X, 34)) return Target::Prev;
    if (near(DECK_PLAY_X, 34)) return Target::PlayPause;
    if (near(DECK_NEXT_X, 34)) return Target::Next;
    if (near(DECK_LIKE_X, 30)) return Target::Like;
    if (near(DECK_DISLIKE_X, 30)) return Target::Dislike;
    if (x >= DECK_VOL_X0 - 40 && x <= DECK_VOL_X1 + 30) return Target::Volume;
    return Target::None;
  }
  if (!deck && !barOpen && y >= tabs::MINI.y - 10) {
    const Rect& m = tabs::MINI;
    return x >= m.x - 10 && x < m.right() + 10 ? Target::Mini : Target::None;
  }
  if (barOpen && y >= BAR_Y - 8) {
    auto near = [&](int cx, int r) { return abs(x - cx) <= r && abs(y - BAR_CY) <= r; };
    if (near(PREV_X, 32)) return Target::Prev;
    if (near(PLAY_X, 34)) return Target::PlayPause;
    if (near(NEXT_X, 32)) return Target::Next;
    if (x >= VOL_X0 - 20 && x <= VOL_X1 + 20) return Target::Volume;
    return Target::None;
  }
  if (y < TOP_H && x < 240) return Target::Clock;
  if (y < TOP_H && tabAt(x, y) >= 0) return Target::Tab;
  if (y < TOP_H && talkButton && statusText.length() == 0 && x >= 780 - TALK_W - 8) return Target::Talk;
  if (callVisible && subtitleVisible && tabs::onCallButton(x, y)) return Target::CallMute;
  if (subtitleVisible) {  // the round Talk button left of the face (a generous touch area)
    const float dx = x - FT_CX, dy = y - FT_CY;
    if (dx * dx + dy * dy <= (FT_R + 16) * (FT_R + 16)) return Target::Talk;
  }
  if (y >= TOP_H && y < SUB_Y + SUB_H) return Target::Face;
  return Target::None;
}

int volumeFromX(int x) {
  if (deck) return constrain((x - DECK_VOL_X0) * 100 / (DECK_VOL_X1 - DECK_VOL_X0), 0, 100);
  return constrain((x - VOL_X0) * 100 / (VOL_X1 - VOL_X0), 0, 100);
}

String toAscii(const String& in, bool keepHebrew) {
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
    else if (keepHebrew && cp == 0x05BE) out += '-';   // Hebrew hyphen (maqaf)
    else if (keepHebrew && cp == 0x05F3) out += '\'';  // geresh
    else if (keepHebrew && cp == 0x05F4) out += '"';   // gershayim
    else if (keepHebrew && cp >= 0x05D0 && cp <= 0x05EA) {  // Hebrew letters, kept as UTF-8
      out += (char)(0xC0 | (cp >> 6));
      out += (char)(0x80 | (cp & 0x3F));
    }
    // anything else (emoji, accents, Hebrew vowel marks) is dropped
  }
  return out;
}

}  // namespace ui
