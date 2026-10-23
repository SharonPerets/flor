#include "panel.h"

#include <stdio.h>

#include "gfont.h"

namespace panel {
namespace {

using gfx::Canvas;
using gfx::Color;

const Color BG = gfx::rgb(0x05070A);
const Color PANEL_C = gfx::rgb(0x161D27);
const Color BORDER = gfx::rgb(0x2A3646);
const Color BTN = gfx::rgb(0x222C38);
const Color TEXT = gfx::rgb(0xE8EEF4);
const Color MUTED = gfx::rgb(0x9AA7B5);
const Color ACCENT = gfx::rgb(0x4FE3D6);
const Color DARK_TEXT = gfx::rgb(0x05201E);
const Color MOON = gfx::rgb(0xB69CFF);
const Color DOT = gfx::rgb(0xFF9F43);
const Color HANDLE = gfx::rgb(0x3A4656);

constexpr float BTN_Y = 64, BTN_H = 76, BTN_W = 260;
constexpr float SLEEP_X = 128, UPDATE_X = 412;

void centered(Canvas& c, const gfont::Font& f, const char* s, float cx, int baseline, const Color& col) {
  c.text(f, s, cx - Canvas::textWidth(f, s) * 0.5f, baseline, col);
}

void moonIcon(Canvas& c, float cx, float cy, const Color& col, const Color& bg) {
  c.fillCircle(cx, cy, 13, col);
  c.fillCircle(cx + 7, cy - 5, 11, bg);
}

void downIcon(Canvas& c, float cx, float cy, const Color& col) {
  c.fillCapsule(cx, cy - 11, cx, cy + 5, 2.2f, col);
  c.fillCapsule(cx - 7, cy - 2, cx, cy + 5, 2.2f, col);
  c.fillCapsule(cx + 7, cy - 2, cx, cy + 5, 2.2f, col);
  c.fillCapsule(cx - 10, cy + 12, cx + 10, cy + 12, 2.2f, col);
}

bool inside(int x, int y, float bx, float by, float bw, float bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

}  // namespace

void draw(Canvas& c, const View& v) {
  const gfx::Rect& a = AREA;
  c.fillRRect(a.x, a.y - 20, a.w, a.h + 20, 24, BORDER);  // flat top: it hangs from the top bar
  c.fillRRect(a.x + 1, a.y - 20, a.w - 2, a.h + 19, 23, PANEL_C);

  // Sleep
  c.fillRRect(SLEEP_X, BTN_Y, BTN_W, BTN_H, 22, BTN);
  moonIcon(c, SLEEP_X + 46, BTN_Y + BTN_H / 2, MOON, BTN);
  c.text(gfont::HEADLINE, "Sleep", SLEEP_X + 80, (int)(BTN_Y + 38), TEXT);
  c.text(gfont::SMALL, "Touch to wake me", SLEEP_X + 82, (int)(BTN_Y + 60), MUTED);

  // Update
  const bool can = v.update == Update::Available;
  c.fillRRect(UPDATE_X, BTN_Y, BTN_W, BTN_H, 22, can ? ACCENT : BTN);
  const Color& fg = can ? DARK_TEXT : MUTED;
  downIcon(c, UPDATE_X + 44, BTN_Y + BTN_H / 2, fg);
  char line1[32], line2[48];
  switch (v.update) {
    case Update::Available:
      snprintf(line1, sizeof line1, "Update");
      snprintf(line2, sizeof line2, "to version %s", v.newVersion);
      break;
    case Update::Busy:
      snprintf(line1, sizeof line1, "Updating...");
      snprintf(line2, sizeof line2, "%s", v.busy);
      break;
    case Update::UpToDate:
      snprintf(line1, sizeof line1, "Up to date");
      snprintf(line2, sizeof line2, "No new version");
      break;
    default:
      snprintf(line1, sizeof line1, "Updates");
      snprintf(line2, sizeof line2, "Checking...");
      break;
  }
  c.text(gfont::HEADLINE, line1, UPDATE_X + 78, (int)(BTN_Y + 38), can ? DARK_TEXT : TEXT);
  c.text(gfont::SMALL, line2, UPDATE_X + 80, (int)(BTN_Y + 60), fg);

  // status line and the handle to push it back up
  centered(c, gfont::SMALL, v.status, a.x + a.w / 2.0f, (int)(BTN_Y + BTN_H + 36), MUTED);
  c.fillRRect(a.x + a.w / 2.0f - 24, a.bottom() - 16, 48, 5, 2.5f, HANDLE);
}

Tap tap(int x, int y) {
  if (!inside(x, y, AREA.x, 0, AREA.w, AREA.bottom())) return Tap::Outside;
  if (inside(x, y, SLEEP_X - 6, BTN_Y - 6, BTN_W + 12, BTN_H + 12)) return Tap::Sleep;
  if (inside(x, y, UPDATE_X - 6, BTN_Y - 6, BTN_W + 12, BTN_H + 12)) return Tap::Update;
  return Tap::None;
}

void drawHandle(Canvas& c, bool dot) {
  c.fillRRect(400 - 18, 1, 36, 4, 2, HANDLE);
  if (dot) {
    c.fillCircle(400 + 26, 3, 4, BG);
    c.fillCircle(400 + 26, 3, 3, DOT);
  }
}

}  // namespace panel
