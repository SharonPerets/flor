#include "tabs.h"

#include <math.h>

#include "gfont.h"

namespace tabs {
namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::Pt;

const Color BG = gfx::rgb(0x05070A);
const Color DIM = gfx::rgb(0x6B7886);
const Color ON_BG = gfx::rgb(0x1C2733);
const Color ACCENT = gfx::rgb(0x4FE3D6);
const Color BADGE = gfx::rgb(0xFF9F43);
const Color CALL = gfx::rgb(0xE5484D);
const Color CALL_PRESSED = gfx::rgb(0xFF7A7A);
const Color WHITE = gfx::rgb(0xF2F6FA);
const Color MUTED = gfx::rgb(0x9AA7B5);

float centerX(int i, int count) { return AREA.x + AREA.w * 0.5f + (i - (count - 1) * 0.5f) * GAP; }

void note(Canvas& c, float cx, float cy, const Color& col) {
  c.fillCircle(cx - 5.5f, cy + 6, 3.4f, col);
  c.fillCircle(cx + 5.5f, cy + 4, 3.4f, col);
  c.fillCapsule(cx - 2.4f, cy - 7, cx - 2.4f, cy + 6, 1.1f, col);
  c.fillCapsule(cx + 8.6f, cy - 9, cx + 8.6f, cy + 4, 1.1f, col);
  const Pt beam[4] = {{cx - 2.4f, cy - 7}, {cx + 8.6f, cy - 9}, {cx + 8.6f, cy - 5}, {cx - 2.4f, cy - 3}};
  c.fillPolygon(beam, 4, col);
}

void face(Canvas& c, float cx, float cy, const Color& col) {
  c.fillRRect(cx - 7, cy - 6, 4.5f, 8, 2.2f, col);
  c.fillRRect(cx + 2.5f, cy - 6, 4.5f, 8, 2.2f, col);
  c.strokeArc(cx, cy + 1, 7, 2, 30, 150, col);
}

void sun(Canvas& c, float cx, float cy, const Color& col) {
  c.fillCircle(cx, cy, 4.5f, col);
  for (int i = 0; i < 8; ++i) {
    const float a = i * 0.785398f;
    c.fillCapsule(cx + cosf(a) * 7.5f, cy + sinf(a) * 7.5f, cx + cosf(a) * 9.5f, cy + sinf(a) * 9.5f, 1.1f, col);
  }
}

void clockIcon(Canvas& c, float cx, float cy, const Color& col) {
  c.strokeArc(cx, cy, 8, 2, 0, 360, col);
  c.fillCapsule(cx, cy, cx, cy - 5, 1.1f, col);
  c.fillCapsule(cx, cy, cx + 3.5f, cy + 2, 1.1f, col);
}

void pencil(Canvas& c, float cx, float cy, const Color& col) {
  c.fillCapsule(cx - 5, cy + 5, cx + 6, cy - 6, 2.6f, col);
  const Pt tip[3] = {{cx - 7.5f, cy + 3.5f}, {cx - 3.5f, cy + 7.5f}, {cx - 9, cy + 9}};
  c.fillPolygon(tip, 3, col);
}

// Claude's spark: thin rays from the middle
void spark(Canvas& c, float cx, float cy, const Color& col) {
  for (int i = 0; i < 6; ++i) {
    const float a = i * 0.5236f + 0.26f;
    c.fillCapsule(cx - cosf(a) * 9, cy - sinf(a) * 9, cx + cosf(a) * 9, cy + sinf(a) * 9, 1.5f, col);
  }
  c.fillCircle(cx, cy, 2.6f, col);
}

void micSlash(Canvas& c, float cx, float cy, float s, const Color& col) {
  c.fillCapsule(cx, cy - 9 * s, cx, cy - 1 * s, 5 * s, col);
  c.strokeArc(cx, cy - 2 * s, 10 * s, 2.6f * s, 15, 165, col);
  c.fillCapsule(cx, cy + 8.5f * s, cx, cy + 12 * s, 1.4f * s, col);
  c.fillCapsule(cx - 12 * s, cy - 13 * s, cx + 12 * s, cy + 11 * s, 1.8f * s, col);
}

}  // namespace

void draw(Canvas& c, const uint8_t* icons, int count, int current, uint32_t badges) {
  for (int i = 0; i < count && i < MAX_TABS; ++i) {
    const float x = centerX(i, count), y = 22;
    const bool on = i == current;
    if (on) c.fillRRect(x - 19, y - 16, 38, 32, 12, ON_BG);
    const Color& col = on ? ACCENT : DIM;
    switch (icons[i]) {
      case MUSIC: note(c, x, y, col); break;
      case FACE: face(c, x, y, col); break;
      case WEATHER: sun(c, x, y, col); break;
      case TIME: clockIcon(c, x, y, col); break;
      case SKETCH: pencil(c, x, y, col); break;
      case CLAUDE: spark(c, x, y, col); break;
    }
    if (badges & (1u << i)) {
      c.fillCircle(x + 12, y - 10, 5, BG);
      c.fillCircle(x + 12, y - 10, 3.8f, BADGE);
    }
  }
}

int at(int x, int y, int count) {
  if (y >= AREA.bottom() + 6) return -1;
  for (int i = 0; i < count && i < MAX_TABS; ++i)
    if (fabsf(x - centerX(i, count)) <= GAP * 0.5f) return i;
  return -1;
}

void drawCallButton(Canvas& c, bool pressed) {
  c.fillCircle(CALL_CX, CALL_CY, CALL_R + 6, CALL, 0.18f);
  c.fillCircle(CALL_CX, CALL_CY, CALL_R, pressed ? CALL_PRESSED : CALL);
  micSlash(c, CALL_CX, CALL_CY + 1, 1.15f, WHITE);
  const char* l1 = "Mute /";
  const char* l2 = "unmute";
  c.text(gfont::SMALL, l1, CALL_CX - Canvas::textWidth(gfont::SMALL, l1) * 0.5f, (int)(CALL_CY + CALL_R + 26), MUTED);
  c.text(gfont::SMALL, l2, CALL_CX - Canvas::textWidth(gfont::SMALL, l2) * 0.5f, (int)(CALL_CY + CALL_R + 46), MUTED);
}

bool onCallButton(int x, int y) {
  const float dx = x - CALL_CX, dy = y - CALL_CY;
  return dx * dx + dy * dy <= (CALL_R + 14) * (CALL_R + 14);
}

}  // namespace tabs
