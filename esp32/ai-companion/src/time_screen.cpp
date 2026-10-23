#include "time_screen.h"

#include <stdio.h>
#include <string.h>

#include "gfont.h"

namespace timescreen {
namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::Pt;

const Color BG = gfx::rgb(0x05070A);
const Color PANEL = gfx::rgb(0x10151C);
const Color BORDER = gfx::rgb(0x1F2935);
const Color TRACK = gfx::rgb(0x222C38);
const Color TEXT = gfx::rgb(0xE8EEF4);
const Color MUTED = gfx::rgb(0x9AA7B5);
const Color ACCENT = gfx::rgb(0x4FE3D6);   // timers
const Color FOCUS_C = gfx::rgb(0xB69CFF);  // focus sessions
const Color DARK_TEXT = gfx::rgb(0x05201E);
const Color DANGER = gfx::rgb(0xFF7A7A);
const Color NOTE_BG = gfx::rgb(0x2A3646);
const Color PAUSED_C = gfx::rgb(0x4A5868);

// Left side: rings and buttons
constexpr float LEFT_CX = 272;
constexpr float BTN_Y = 312, BTN_H = 52;
constexpr float QUICK_X = 16, QUICK_W = 80, BTN_GAP = 8;
constexpr float FLEN_X = QUICK_X + 3 * (QUICK_W + BTN_GAP) + 8, FLEN_W = 98;
constexpr float FSTART_X = FLEN_X + FLEN_W + BTN_GAP, FSTART_W = 528 - FSTART_X;
// Right side: reminders
constexpr float PANEL_X = 544, PANEL_Y = 56, PANEL_W = 240, PANEL_H = 308;
constexpr float ROW_Y = 102, ROW_H = 56;

struct Geo {
  float cx, cy, r, thick;
};

Geo ringGeo(int i, int n) {
  switch (n) {
    case 1: return {LEFT_CX, 168, 104, 12};
    case 2: return {LEFT_CX + (i ? 128.0f : -128.0f), 172, 94, 11};
    case 3: return {LEFT_CX + (i - 1) * 172.0f, 178, 74, 10};
    default: return {LEFT_CX + (i * 2 - 3) * 64.0f, 188, 56, 8};
  }
}

void closeCentre(const Geo& g, float& x, float& y, float& r) {
  x = g.cx + g.r * 0.74f;
  y = g.cy - g.r * 0.74f;
  r = g.r > 70 ? 16 : 13;
}

float textW(const gfont::Font& f, const char* s) { return Canvas::textWidth(f, s); }

void centered(Canvas& c, const gfont::Font& f, const char* s, float cx, int baseline, const Color& col,
              float alpha = 1.0f) {
  c.text(f, s, cx - textW(f, s) * 0.5f, baseline, col, alpha);
}

// Copies s into out, cut with "..." so it fits maxW
void fit(const gfont::Font& f, const char* s, float maxW, char* out, int size) {
  snprintf(out, size, "%s", s);
  if (textW(f, out) <= maxW) return;
  int n = (int)strlen(out);
  while (n > 0) {
    out[--n] = 0;
    char tmp[96];
    snprintf(tmp, sizeof tmp, "%s...", out);
    if (textW(f, tmp) <= maxW) {
      snprintf(out, size, "%s", tmp);
      return;
    }
  }
}

// The big font only has digits: the ":" is drawn as two dots.
bool hasColon(const gfont::Font& f) { return f.first <= ':' && f.last >= ':'; }

float digitsWidth(const gfont::Font& f, const char* s) {
  if (hasColon(f)) return textW(f, s);
  float w = 0;
  char one[2] = {0, 0};
  for (const char* p = s; *p; ++p) {
    if (*p == ':') {
      w += f.ascent * 0.32f;
    } else {
      one[0] = *p;
      w += textW(f, one);
    }
  }
  return w;
}

void digits(Canvas& c, const gfont::Font& f, const char* s, float cx, int baseline, const Color& col) {
  if (hasColon(f)) {
    centered(c, f, s, cx, baseline, col);
    return;
  }
  float x = cx - digitsWidth(f, s) * 0.5f;
  char one[2] = {0, 0};
  for (const char* p = s; *p; ++p) {
    if (*p == ':') {
      const float w = f.ascent * 0.32f, dr = f.ascent * 0.065f;
      c.fillCircle(x + w * 0.5f, baseline - f.ascent * 0.52f, dr, col);
      c.fillCircle(x + w * 0.5f, baseline - f.ascent * 0.16f, dr, col);
      x += w;
    } else {
      one[0] = *p;
      c.text(f, one, x, baseline, col);
      x += textW(f, one);
    }
  }
}

void xMark(Canvas& c, float x, float y, float s, float thick, const Color& col) {
  c.fillCapsule(x - s, y - s, x + s, y + s, thick, col);
  c.fillCapsule(x - s, y + s, x + s, y - s, thick, col);
}

void clockIcon(Canvas& c, float cx, float cy, float r, const Color& col) {
  c.strokeArc(cx, cy, r, 4, 0, 360, col);
  c.fillCapsule(cx, cy, cx, cy - r * 0.55f, 2.2f, col);
  c.fillCapsule(cx, cy, cx + r * 0.4f, cy + r * 0.2f, 2.2f, col);
}

// Two arrows going round: "tap to change"
void cycleIcon(Canvas& c, float cx, float cy, float r, const Color& col) {
  c.strokeArc(cx, cy, r, 2.2f, 200, 330, col);
  c.strokeArc(cx, cy, r, 2.2f, 20, 150, col);
  const Pt a[3] = {{cx + r * 0.62f - 3.5f, cy - r * 0.95f}, {cx + r * 0.95f + 3.5f, cy - r * 0.62f}, {cx + r * 1.2f, cy - r * 1.2f}};
  const Pt b[3] = {{cx - r * 0.62f + 3.5f, cy + r * 0.95f}, {cx - r * 0.95f - 3.5f, cy + r * 0.62f}, {cx - r * 1.2f, cy + r * 1.2f}};
  c.fillPolygon(a, 3, col);
  c.fillPolygon(b, 3, col);
}

void ring(Canvas& c, const Ring& rg, const Geo& g) {
  const Color& col = rg.paused ? PAUSED_C : (rg.kind == FOCUS ? FOCUS_C : ACCENT);
  c.strokeArc(g.cx, g.cy, g.r, g.thick, 0, 360, TRACK);
  const float frac = rg.total ? (float)rg.left / rg.total : 0.0f;
  if (frac > 0.002f) {
    // what's left, going round from the top
    c.strokeArc(g.cx, g.cy, g.r, g.thick, -90, -90 + 360 * gfx::clamp01(frac), col);
  }

  char buf[16];
  formatLeft(rg.left, buf, sizeof buf);
  const float room = (g.r - g.thick) * 2 - 18;
  const gfont::Font* f = &gfont::BIG;
  if (digitsWidth(*f, buf) > room) f = &gfont::HEADLINE;
  if (digitsWidth(*f, buf) > room) f = &gfont::TITLE;
  const int base = (int)(g.cy + f->ascent * 0.36f);
  digits(c, *f, buf, g.cx, base, TEXT);
  if (rg.paused) centered(c, gfont::SMALL, "Paused", g.cx, base + 26, MUTED);

  // the label under the ring
  char label[40];
  fit(gfont::SMALL, rg.label, g.r * 2 + 30, label, sizeof label);
  centered(c, gfont::SMALL, label, g.cx, (int)(g.cy + g.r + g.thick * 0.5f + 22), rg.kind == FOCUS ? FOCUS_C : MUTED);

  // cancel button
  float x, y, r;
  closeCentre(g, x, y, r);
  c.fillCircle(x, y, r + 2, BG);
  c.fillCircle(x, y, r, BORDER);
  xMark(c, x, y, r * 0.36f, 1.5f, TEXT);
}

void emptyRings(Canvas& c) {
  clockIcon(c, LEFT_CX, 112, 30, MUTED);
  centered(c, gfont::HEADLINE, "No timers running", LEFT_CX, 186, TEXT);
  centered(c, gfont::SMALL, "Tap a button below, or ask me:", LEFT_CX, 220, MUTED);
  centered(c, gfont::SMALL, "\"Set a timer for 10 minutes\"", LEFT_CX, 244, MUTED);
}

void button(Canvas& c, float x, float w, const Color& fill, bool outline) {
  if (outline) {
    c.fillRRect(x, BTN_Y, w, BTN_H, 16, BORDER);
    c.fillRRect(x + 1, BTN_Y + 1, w - 2, BTN_H - 2, 15, fill);
  } else {
    c.fillRRect(x, BTN_Y, w, BTN_H, 16, fill);
  }
}

void buttons(Canvas& c, const View& v) {
  char buf[24];
  const int textBase = (int)(BTN_Y + 34);
  for (int i = 0; i < 3; ++i) {
    const float x = QUICK_X + i * (QUICK_W + BTN_GAP);
    button(c, x, QUICK_W, PANEL, true);
    snprintf(buf, sizeof buf, "%d min", QUICK[i]);
    centered(c, gfont::TITLE, buf, x + QUICK_W * 0.5f, textBase, ACCENT, v.timersFull ? 0.35f : 1.0f);
  }
  // focus length (tap to change) and start
  button(c, FLEN_X, FLEN_W, PANEL, true);
  snprintf(buf, sizeof buf, "%d min", v.focusMinutes);
  const float tw = textW(gfont::TITLE, buf);
  const float left = FLEN_X + (FLEN_W - tw - 24) * 0.5f;
  cycleIcon(c, left + 8, BTN_Y + BTN_H * 0.5f, 6.5f, FOCUS_C);
  c.text(gfont::TITLE, buf, left + 24, textBase, TEXT);

  if (v.focusRunning) {
    button(c, FSTART_X, FSTART_W, PANEL, true);
    centered(c, gfont::TITLE, "Focusing...", FSTART_X + FSTART_W * 0.5f, textBase, FOCUS_C);
  } else {
    button(c, FSTART_X, FSTART_W, FOCUS_C, false);
    centered(c, gfont::TITLE, "Start focus", FSTART_X + FSTART_W * 0.5f, textBase, DARK_TEXT);
  }
}

void note(Canvas& c, const char* text) {
  if (!text || !text[0]) return;
  const float w = textW(gfont::SMALL, text) + 36;
  c.fillRRect(LEFT_CX - w * 0.5f, 52, w, 32, 16, NOTE_BG);
  centered(c, gfont::SMALL, text, LEFT_CX, 73, TEXT);
}

void reminders(Canvas& c, const View& v) {
  c.fillRRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 20, BORDER);
  c.fillRRect(PANEL_X + 1, PANEL_Y + 1, PANEL_W - 2, PANEL_H - 2, 19, PANEL);
  const float x = PANEL_X + 18, w = PANEL_W - 36;
  c.text(gfont::TITLE, "Coming up", x, (int)PANEL_Y + 34, TEXT);

  char buf[72];
  if (!v.clockKnown) {
    c.text(gfont::SMALL, "Waiting for the time...", x, (int)ROW_Y + 30, MUTED);
  } else if (v.rowCount == 0) {
    const char* lines[] = {"No reminders yet.", "Ask me, like:", "\"Remind me at 5", "to call mom\""};
    for (int i = 0; i < 4; ++i) c.text(gfont::SMALL, lines[i], x, (int)ROW_Y + 26 + i * 24, MUTED);
  }
  for (int i = 0; i < v.rowCount; ++i) {
    const Row& r = v.rows[i];
    const float y = ROW_Y + i * ROW_H;
    if (i > 0) c.fillRect(x, y - 1, w, 1, BORDER);
    if (i == v.confirmRow) {
      fit(gfont::SMALL, r.text, w, buf, sizeof buf);
      c.text(gfont::SMALL, "Delete this one?", x, (int)y + 19, DANGER);
      c.fillRRect(x, y + 25, 96, 26, 13, DANGER);
      centered(c, gfont::SMALL, "Delete", x + 48, (int)y + 43, DARK_TEXT);
      c.fillRRect(x + 108, y + 25, 96, 26, 13, BORDER);
      centered(c, gfont::SMALL, "Keep", x + 156, (int)y + 43, TEXT);
      continue;
    }
    c.text(gfont::SMALL, r.when, x, (int)y + 20, ACCENT);
    fit(gfont::TITLE, r.text, w, buf, sizeof buf);
    c.text(gfont::TITLE, buf, x, (int)y + 46, TEXT);
  }

  // footer
  char foot[48] = "";
  if (v.moreRows > 0 && v.focusToday > 0)
    snprintf(foot, sizeof foot, "+%d more  -  Focus today: %d", v.moreRows, v.focusToday);
  else if (v.moreRows > 0)
    snprintf(foot, sizeof foot, "+%d more", v.moreRows);
  else if (v.focusToday > 0)
    snprintf(foot, sizeof foot, "Focus sessions today: %d", v.focusToday);
  if (foot[0]) c.text(gfont::SMALL, foot, x, (int)(PANEL_Y + PANEL_H - 16), MUTED);
}

bool inside(int x, int y, float bx, float by, float bw, float bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

}  // namespace

void formatLeft(uint32_t s, char* out, int size) {
  if (s >= 3600)
    snprintf(out, size, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
  else
    snprintf(out, size, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

void draw(Canvas& c, const View& v) {
  const int n = v.ringCount < MAX_RINGS ? v.ringCount : MAX_RINGS;
  if (n == 0) emptyRings(c);
  for (int i = 0; i < n; ++i) ring(c, v.rings[i], ringGeo(i, n));
  buttons(c, v);
  reminders(c, v);
  note(c, v.note);
}

TapResult tap(int x, int y, const View& v) {
  TapResult t;
  // reminders
  if (x >= PANEL_X - 8) {
    for (int i = 0; i < v.rowCount; ++i) {
      const float ry = ROW_Y + i * ROW_H;
      if (i == v.confirmRow) {
        const float bx = PANEL_X + 18;
        if (inside(x, y, bx - 8, ry + 17, 112, 42)) return {Tap::Delete, i};
        if (inside(x, y, bx + 100, ry + 17, 112, 42)) return {Tap::Keep, i};
        continue;
      }
      if (y >= ry && y < ry + ROW_H) return {Tap::Row, i};
    }
    return v.confirmRow >= 0 ? TapResult{Tap::Keep, v.confirmRow} : t;
  }
  if (v.confirmRow >= 0) return {Tap::Keep, v.confirmRow};  // a tap elsewhere: never mind
  // buttons
  if (y >= BTN_Y - 4) {
    for (int i = 0; i < 3; ++i)
      if (inside(x, y, QUICK_X + i * (QUICK_W + BTN_GAP) - 4, BTN_Y - 4, QUICK_W + 8, BTN_H + 12))
        return {Tap::QuickTimer, QUICK[i]};
    if (inside(x, y, FLEN_X - 4, BTN_Y - 4, FLEN_W + 8, BTN_H + 12)) return {Tap::FocusLength, 0};
    if (inside(x, y, FSTART_X - 4, BTN_Y - 4, FSTART_W + 8, BTN_H + 12)) return {Tap::FocusStart, 0};
    return t;
  }
  // rings: the small x first, then the ring itself
  const int n = v.ringCount < MAX_RINGS ? v.ringCount : MAX_RINGS;
  for (int i = 0; i < n; ++i) {
    const Geo g = ringGeo(i, n);
    float cx, cy, r;
    closeCentre(g, cx, cy, r);
    const float dx = x - cx, dy = y - cy;
    if (dx * dx + dy * dy <= (r + 12) * (r + 12)) return {Tap::RingClose, i};
  }
  for (int i = 0; i < n; ++i) {
    const Geo g = ringGeo(i, n);
    const float dx = x - g.cx, dy = y - g.cy;
    if (dx * dx + dy * dy <= (g.r + 10) * (g.r + 10)) return {Tap::Ring, i};
  }
  return t;
}

}  // namespace timescreen
