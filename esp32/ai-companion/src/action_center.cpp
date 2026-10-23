#include "action_center.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfont.h"

namespace actioncenter {
namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::Pt;

const Color BG = gfx::rgb(0x05070A);
const Color CARD = gfx::rgb(0x10151C);
const Color CARD_EDGE = gfx::rgb(0x1C2430);
const Color ROW = gfx::rgb(0x161D27);
const Color TEXT = gfx::rgb(0xE8EEF4);
const Color MUTED = gfx::rgb(0x8D9AA8);
const Color FAINT = gfx::rgb(0x55616E);
const Color ACCENT = gfx::rgb(0x4FE3D6);
const Color DARK_TEXT = gfx::rgb(0x05201E);
const Color MOON = gfx::rgb(0xB69CFF);
const Color ORANGE = gfx::rgb(0xFF9F43);
const Color CLAUDE_C = gfx::rgb(0xD97757);
const Color HANDLE = gfx::rgb(0x3A4656);

// Layout
constexpr float PAD = 32;
constexpr float SLEEP_X = 612, SLEEP_Y = 34, SLEEP_W = 156, SLEEP_H = 56;
constexpr float CARD_Y = 124, CARD_H = 292;
constexpr float W_X = PAD, W_W = 318;                  // weather card
constexpr float N_X = W_X + W_W + 20, N_W = 800 - PAD - N_X;  // notifications card
constexpr float ROW_Y = CARD_Y + 56, ROW_H = 64, ROW_GAP = 10;

void card(Canvas& c, float x, float y, float w, float h) {
  c.fillRRect(x, y, w, h, 24, CARD_EDGE);
  c.fillRRect(x + 1, y + 1, w - 2, h - 2, 23, CARD);
}

void fit(const gfont::Font& f, const char* s, float maxW, char* out, int size) {
  snprintf(out, size, "%s", s);
  if (Canvas::textWidth(f, out) <= maxW) return;
  int n = (int)strlen(out);
  while (n > 0) {
    out[--n] = 0;
    char tmp[96];
    snprintf(tmp, sizeof tmp, "%s...", out);
    if (Canvas::textWidth(f, tmp) <= maxW) {
      snprintf(out, size, "%s", tmp);
      return;
    }
  }
}

// Big digits: the big font has no ":" or "°", they're drawn
float bigText(Canvas& c, const char* s, float x, int baseline, const Color& col, bool degree = false) {
  const gfont::Font& f = gfont::BIG;
  char one[2] = {0, 0};
  for (const char* p = s; *p; ++p) {
    if (*p == ':') {
      const float w = f.ascent * 0.32f, r = f.ascent * 0.065f;
      c.fillCircle(x + w * 0.5f, baseline - f.ascent * 0.52f, r, col);
      c.fillCircle(x + w * 0.5f, baseline - f.ascent * 0.16f, r, col);
      x += w;
    } else {
      one[0] = *p;
      c.text(f, one, x, baseline, col);
      x += Canvas::textWidth(f, one);
    }
  }
  if (degree) {
    c.strokeArc(x + 10, baseline - f.ascent * 0.62f, 7, 4, 0, 360, col);
    x += 22;
  }
  return x;
}

void moonIcon(Canvas& c, float cx, float cy, const Color& col, const Color& bg) {
  c.fillCircle(cx, cy, 11, col);
  c.fillCircle(cx + 6, cy - 4, 9.5f, bg);
}

void bellIcon(Canvas& c, float cx, float cy, const Color& col) {
  c.fillCircle(cx, cy - 2, 13, col);
  c.fillRect(cx - 13, cy - 2, 26, 12, col);
  c.fillRRect(cx - 17, cy + 8, 34, 5, 2.5f, col);
  c.fillCircle(cx, cy + 17, 4, col);
}

void noticeIcon(Canvas& c, uint8_t kind, float cx, float cy) {
  switch (kind) {
    case UPDATE: {
      c.fillCircle(cx, cy, 18, ACCENT, 0.18f);
      c.fillCapsule(cx, cy - 9, cx, cy + 4, 2, ACCENT);
      c.fillCapsule(cx - 6, cy - 2, cx, cy + 4, 2, ACCENT);
      c.fillCapsule(cx + 6, cy - 2, cx, cy + 4, 2, ACCENT);
      c.fillCapsule(cx - 8, cy + 10, cx + 8, cy + 10, 2, ACCENT);
      break;
    }
    case MISSED:
      c.fillCircle(cx, cy, 18, ORANGE, 0.18f);
      c.strokeArc(cx, cy, 9, 2.4f, 0, 360, ORANGE);
      c.fillCapsule(cx, cy, cx, cy - 5, 1.6f, ORANGE);
      c.fillCapsule(cx, cy, cx + 4, cy + 2, 1.6f, ORANGE);
      break;
    case CLAUDE:
      c.fillCircle(cx, cy, 18, CLAUDE_C, 0.18f);
      for (int i = 0; i < 6; ++i) {
        const float a = i * 0.5236f + 0.26f;
        c.fillCapsule(cx - cosf(a) * 9, cy - sinf(a) * 9, cx + cosf(a) * 9, cy + sinf(a) * 9, 1.4f, CLAUDE_C);
      }
      break;
    default:
      c.fillCircle(cx, cy, 18, MUTED, 0.18f);
      c.fillCircle(cx, cy - 6, 2, MUTED);
      c.fillCapsule(cx, cy - 1, cx, cy + 7, 1.8f, MUTED);
      break;
  }
}

float actionW(const Notice& n) { return Canvas::textWidth(gfont::SMALL, n.action) + 32; }

void weatherCard(Canvas& c, const View& v) {
  card(c, W_X, CARD_Y, W_W, CARD_H);
  const float x = W_X + 24;
  const char* city = (v.city >= 0 && v.city < weather::CITY_COUNT) ? weather::CITIES[v.city].name : "";
  c.text(gfont::SMALL, "WEATHER", x, (int)CARD_Y + 34, MUTED);
  // the city: tap to change
  const float tw = Canvas::textWidth(gfont::SMALL, city);
  const float cx = W_X + W_W - 24 - tw - 18;
  c.text(gfont::SMALL, city, cx, (int)CARD_Y + 34, ACCENT);
  c.fillCapsule(cx + tw + 6, CARD_Y + 25, cx + tw + 10, CARD_Y + 29, 1.3f, ACCENT);
  c.fillCapsule(cx + tw + 10, CARD_Y + 29, cx + tw + 14, CARD_Y + 25, 1.3f, ACCENT);

  const weather::Data* d = v.wx;
  if (!d || !d->valid || d->city != v.city) {
    c.text(gfont::SMALL, v.wxStatus && v.wxStatus[0] ? v.wxStatus : "Getting the weather...", x, (int)CARD_Y + 130,
           MUTED);
    return;
  }
  weather::drawIcon(c, d->code, d->isDay, x + 46, CARD_Y + 104, 92);
  char buf[16];
  snprintf(buf, sizeof buf, "%d", (int)lroundf(d->temp));
  bigText(c, buf, x + 104, (int)CARD_Y + 128, TEXT, true);
  char desc[40];
  fit(gfont::SMALL, weather::describe(d->code), W_W - 150, desc, sizeof desc);
  c.text(gfont::SMALL, desc, x + 108, (int)CARD_Y + 154, MUTED);

  // the next days
  const int n = d->dayCount < 5 ? d->dayCount : 5;
  const float colW = (W_W - 48) / 5.0f, y = CARD_Y + 200;
  c.fillRect(x, y - 10, W_W - 48, 1, CARD_EDGE);
  for (int i = 0; i < n; ++i) {
    const weather::Day& day = d->days[i];
    const float mx = x + colW * (i + 0.5f);
    const char* name = i == 0 ? "Today" : day.name;
    c.text(gfont::SMALL, name, mx - Canvas::textWidth(gfont::SMALL, name) * 0.5f, (int)y + 14, MUTED);
    weather::drawIcon(c, day.code, true, mx, y + 38, 30);
    snprintf(buf, sizeof buf, "%d/%d", day.hi, day.lo);
    c.text(gfont::SMALL, buf, mx - Canvas::textWidth(gfont::SMALL, buf) * 0.5f, (int)y + 72, TEXT);
  }
}

void noticesCard(Canvas& c, const View& v) {
  card(c, N_X, CARD_Y, N_W, CARD_H);
  const float x = N_X + 24;
  c.text(gfont::SMALL, "NOTIFICATIONS", x, (int)CARD_Y + 34, MUTED);
  if (v.noticeCount == 0) {
    const float mx = N_X + N_W / 2;
    bellIcon(c, mx, CARD_Y + 128, FAINT);
    const char* t = "You're all caught up";
    c.text(gfont::TITLE, t, mx - Canvas::textWidth(gfont::TITLE, t) * 0.5f, (int)CARD_Y + 186, TEXT);
    const char* s = "Updates and things you missed show up here";
    c.text(gfont::SMALL, s, mx - Canvas::textWidth(gfont::SMALL, s) * 0.5f, (int)CARD_Y + 212, MUTED);
    return;
  }
  for (int i = 0; i < v.noticeCount && i < 3; ++i) {
    const Notice& n = v.notices[i];
    const float y = ROW_Y + i * (ROW_H + ROW_GAP), rx = N_X + 14, rw = N_W - 28;
    c.fillRRect(rx, y, rw, ROW_H, 16, ROW);
    noticeIcon(c, n.kind, rx + 34, y + ROW_H / 2);
    const bool hasAction = n.action[0] != 0;
    const char* label = (hasAction && v.busy && n.kind == UPDATE) ? "Updating..." : n.action;
    const float aw = hasAction ? Canvas::textWidth(gfont::SMALL, label) + 32 : 0;
    const float textW = rw - 72 - (hasAction ? aw + 12 : 40);
    char buf[72];
    fit(gfont::TITLE, n.title, textW, buf, sizeof buf);
    c.text(gfont::TITLE, buf, rx + 64, (int)y + 30, TEXT);
    fit(gfont::SMALL, n.text, textW, buf, sizeof buf);
    c.text(gfont::SMALL, buf, rx + 64, (int)y + 52, MUTED);
    if (hasAction) {
      const float bx = rx + rw - 14 - aw;
      const bool busy = v.busy && n.kind == UPDATE;
      c.fillRRect(bx, y + 15, aw, 36, 18, busy ? CARD_EDGE : ACCENT);
      c.text(gfont::SMALL, label, bx + 16, (int)y + 39, busy ? MUTED : DARK_TEXT);
    } else {  // a small x to clear it
      const float bx = rx + rw - 26, by = y + ROW_H / 2;
      c.fillCapsule(bx - 5, by - 5, bx + 5, by + 5, 1.5f, MUTED);
      c.fillCapsule(bx - 5, by + 5, bx + 5, by - 5, 1.5f, MUTED);
    }
  }
  if (v.noticeCount > 3) {
    char more[24];
    snprintf(more, sizeof more, "+%d more", v.noticeCount - 3);
    c.text(gfont::SMALL, more, x, (int)(CARD_Y + CARD_H - 14), MUTED);
  }
}

bool inside(int x, int y, float bx, float by, float bw, float bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

}  // namespace

void draw(Canvas& c, const View& v) {
  if (!v.cityPicker) {  // header: the time, the date, Sleep
    const float tx = bigText(c, v.time, PAD, 96, TEXT);
    c.text(gfont::TITLE, v.date, tx + 22, 92, MUTED);
    c.fillRRect(SLEEP_X, SLEEP_Y, SLEEP_W, SLEEP_H, SLEEP_H / 2, CARD_EDGE);
    moonIcon(c, SLEEP_X + 34, SLEEP_Y + SLEEP_H / 2, MOON, CARD_EDGE);
    c.text(gfont::TITLE, "Sleep", SLEEP_X + 60, (int)(SLEEP_Y + 36), TEXT);
  }

  if (v.cityPicker) {  // choosing the city: the weather screen's own picker
    weather::Data none;
    weather::draw(c, none, v.city, true, "");
  } else {
    weatherCard(c, v);
    noticesCard(c, v);
  }

  // footer: status, and the handle to push it back up
  c.text(gfont::SMALL, v.status, PAD, 452, FAINT);
  c.fillRRect(400 - 30, 468, 60, 5, 2.5f, HANDLE);
}

TapResult tap(int x, int y, const View& v) {
  if (!v.cityPicker && inside(x, y, SLEEP_X - 8, SLEEP_Y - 8, SLEEP_W + 16, SLEEP_H + 16)) return TapResult(Tap::Sleep);
  if (v.cityPicker) {
    int city = v.city;
    switch (weather::tap(x, y, true, city)) {
      case weather::Tap::PickCity: return TapResult(Tap::PickCity, city);
      case weather::Tap::ClosePicker: return TapResult(Tap::ClosePicker);
      default: return TapResult();
    }
  }
  if (inside(x, y, W_X, CARD_Y, W_W, 52)) return TapResult(Tap::City);
  for (int i = 0; i < v.noticeCount && i < 3; ++i) {
    const Notice& n = v.notices[i];
    const float ry = ROW_Y + i * (ROW_H + ROW_GAP), rx = N_X + 14, rw = N_W - 28;
    if (!inside(x, y, rx, ry, rw, ROW_H)) continue;
    if (n.action[0]) return x >= rx + rw - actionW(n) - 30 ? TapResult(Tap::Action, i) : TapResult();
    return x >= rx + rw - 60 ? TapResult(Tap::Dismiss, i) : TapResult();
  }
  return TapResult();
}

}  // namespace actioncenter
