#include "weather.h"

#include <stdio.h>
#include <string.h>

#include "gfont.h"

namespace weather {

const City CITIES[CITY_COUNT] = {
    {"Nahariya", 33.006f, 35.094f},  {"Akko", 32.927f, 35.083f},        {"Haifa", 32.794f, 34.990f},
    {"Karmiel", 32.919f, 35.296f},   {"Tiberias", 32.795f, 35.531f},    {"Safed", 32.965f, 35.496f},
    {"Nazareth", 32.700f, 35.303f},  {"Afula", 32.608f, 35.289f},       {"Hadera", 32.434f, 34.919f},
    {"Netanya", 32.332f, 34.860f},   {"Tel Aviv", 32.085f, 34.782f},    {"Jerusalem", 31.769f, 35.216f},
    {"Rishon LeZion", 31.964f, 34.804f}, {"Ashdod", 31.804f, 34.655f}, {"Be'er Sheva", 31.252f, 34.791f},
    {"Eilat", 29.558f, 34.952f},
};

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::Pt;

const Color BG = gfx::rgb(0x05070A);
const Color PANEL = gfx::rgb(0x10151C);
const Color BORDER = gfx::rgb(0x1F2935);
const Color TEXT = gfx::rgb(0xE8EEF4);
const Color MUTED = gfx::rgb(0x9AA7B5);
const Color ACCENT = gfx::rgb(0x4FE3D6);
const Color DARK_TEXT = gfx::rgb(0x05201E);
const Color SUN = gfx::rgb(0xFFD166);
const Color MOON = gfx::rgb(0xE8EEF4);
const Color CLOUD = gfx::rgb(0xD5DEE7);
const Color CLOUD_DARK = gfx::rgb(0x8E9BAA);
const Color RAIN = gfx::rgb(0x6FB7FF);
const Color SNOW = gfx::rgb(0xF2F6FA);

// layout
const float CITY_X = 24, CITY_Y = 58, CITY_H = 40;
const float CARD_Y = 252, CARD_H = 118, CARD_W = 102, CARD_GAP = 6;
const float PICK_X = 22, PICK_Y = 104, PICK_W = 180, PICK_H = 56, PICK_GAP = 12;
const float CLOSE_X = 756, CLOSE_Y = 74, CLOSE_R = 22;

enum Kind { CLEAR, MAINLY_CLEAR, PARTLY, OVERCAST, FOG, DRIZZLE, RAIN_K, SNOW_K, THUNDER };

Kind kindOf(int code) {
  if (code == 0) return CLEAR;
  if (code == 1) return MAINLY_CLEAR;
  if (code == 2) return PARTLY;
  if (code == 3) return OVERCAST;
  if (code == 45 || code == 48) return FOG;
  if (code >= 51 && code <= 57) return DRIZZLE;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return RAIN_K;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return SNOW_K;
  if (code >= 95) return THUNDER;
  return OVERCAST;
}

// ---------------------------------------------------------------- icon parts (s = size)

void sun(Canvas& c, float cx, float cy, float s) {
  c.fillCircle(cx, cy, 0.21f * s, SUN);
  for (int i = 0; i < 8; ++i) {
    const float a = i * 0.785398f;
    const float ca = cosf(a), sa = sinf(a);
    c.fillCapsule(cx + ca * 0.31f * s, cy + sa * 0.31f * s, cx + ca * 0.41f * s, cy + sa * 0.41f * s, 0.035f * s, SUN);
  }
}

void moon(Canvas& c, float cx, float cy, float s, const Color& bg) {
  c.fillCircle(cx, cy, 0.27f * s, MOON);
  c.fillCircle(cx + 0.13f * s, cy - 0.09f * s, 0.23f * s, bg);
}

void cloud(Canvas& c, float cx, float cy, float s, const Color& col) {
  c.fillCircle(cx - 0.16f * s, cy + 0.03f * s, 0.19f * s, col);
  c.fillCircle(cx + 0.05f * s, cy - 0.07f * s, 0.25f * s, col);
  c.fillCircle(cx + 0.23f * s, cy + 0.07f * s, 0.16f * s, col);
  c.fillRRect(cx - 0.36f * s, cy + 0.02f * s, 0.75f * s, 0.21f * s, 0.105f * s, col);
}

void degree(Canvas& c, float x, float y, float r, float thick, const Color& col) {
  c.strokeArc(x, y, r, thick, 0, 360, col);
}

// Draws "27°". align: 0 left, 1 centre, 2 right of x. Returns the width.
float temperature(Canvas& c, const gfont::Font& f, int value, float x, int baseline, const Color& col, int align) {
  char buf[8];
  snprintf(buf, sizeof buf, "%d", value);
  const float r = f.ascent * 0.11f + 1.0f, thick = f.ascent * 0.045f + 1.0f;
  const float tw = Canvas::textWidth(f, buf);
  const float w = tw + r * 2 + thick + 2;
  const float left = align == 0 ? x : (align == 1 ? x - w * 0.5f : x - w);
  c.text(f, buf, left, baseline, col);
  const float capTop = baseline - f.ascent * 0.72f;
  degree(c, left + tw + r + thick * 0.5f + 1.5f, capTop + r, r, thick, col);
  return w;
}

void centered(Canvas& c, const gfont::Font& f, const char* s, float cx, int baseline, const Color& col) {
  c.text(f, s, cx - Canvas::textWidth(f, s) * 0.5f, baseline, col);
}

// map pin: round head, point at the bottom
void pin(Canvas& c, float cx, float cy, float s, const Color& col) {
  c.fillCircle(cx, cy - 0.12f * s, 0.34f * s, col);
  const Pt tip[3] = {{cx - 0.3f * s, cy - 0.0f * s}, {cx + 0.3f * s, cy - 0.0f * s}, {cx, cy + 0.5f * s}};
  c.fillPolygon(tip, 3, col);
}

void raindrop(Canvas& c, float cx, float cy, float s, const Color& col) {
  c.fillCircle(cx, cy + 0.15f * s, 0.32f * s, col);
  const Pt tip[3] = {{cx - 0.29f * s, cy + 0.02f * s}, {cx + 0.29f * s, cy + 0.02f * s}, {cx, cy - 0.5f * s}};
  c.fillPolygon(tip, 3, col);
}

// ---------------------------------------------------------------- parts of the screen

void cityButton(Canvas& c, const char* name) {
  const float tw = Canvas::textWidth(gfont::TITLE, name);
  const float w = tw + 66;
  c.fillRRect(CITY_X, CITY_Y, w, CITY_H, CITY_H / 2, BORDER);
  c.fillRRect(CITY_X + 1, CITY_Y + 1, w - 2, CITY_H - 2, CITY_H / 2 - 1, PANEL);
  // map pin
  const float px = CITY_X + 20, py = CITY_Y + CITY_H / 2;
  pin(c, px, py, 18, ACCENT);
  c.fillCircle(px, py - 2.2f, 2.6f, PANEL);
  c.text(gfont::TITLE, name, CITY_X + 34, (int)(CITY_Y + 28), TEXT);
  // small "v" to show it opens a list
  const float vx = CITY_X + 48 + tw, vy = CITY_Y + CITY_H / 2;
  c.fillCapsule(vx - 4, vy - 2, vx, vy + 2, 1.3f, MUTED);
  c.fillCapsule(vx, vy + 2, vx + 4, vy - 2, 1.3f, MUTED);
}

void detailRow(Canvas& c, const char* label, const char* value, int baseline) {
  c.text(gfont::SMALL, label, 520, baseline, MUTED);
  c.text(gfont::TITLE, value, 776 - Canvas::textWidth(gfont::TITLE, value), baseline, TEXT);
}

void current(Canvas& c, const Data& d) {
  drawIcon(c, d.code, d.isDay, 128, 176, 124);
  const int t = (int)lroundf(d.temp);
  temperature(c, gfont::BIG, t, 222, 196, TEXT, 0);
  c.text(gfont::TITLE, describe(d.code), 226, 232, MUTED);

  char buf[24];
  const int feels = (int)lroundf(d.feels);
  c.text(gfont::SMALL, "Feels like", 520, 122, MUTED);
  temperature(c, gfont::TITLE, feels, 776, 122, TEXT, 2);
  snprintf(buf, sizeof buf, "%d%%", d.humidity);
  detailRow(c, "Humidity", buf, 154);
  snprintf(buf, sizeof buf, "%d km/h", (int)lroundf(d.wind));
  detailRow(c, "Wind", buf, 186);
  if (d.dayCount > 0) {
    c.text(gfont::SMALL, "Today", 520, 218, MUTED);
    const float w = temperature(c, gfont::TITLE, d.days[0].lo, 776, 218, MUTED, 2);
    c.text(gfont::TITLE, "/", 776 - w - 14, 218, MUTED);
    temperature(c, gfont::TITLE, d.days[0].hi, 776 - w - 20, 218, TEXT, 2);
  }
  if (d.updated[0]) {
    char up[20];
    snprintf(up, sizeof up, "Updated %s", d.updated);
    c.text(gfont::SMALL, up, 776 - Canvas::textWidth(gfont::SMALL, up), 242, MUTED, 0.6f);
  }
}

void dayCard(Canvas& c, const Day& day, float x) {
  c.fillRRect(x, CARD_Y, CARD_W, CARD_H, 16, BORDER);
  c.fillRRect(x + 1, CARD_Y + 1, CARD_W - 2, CARD_H - 2, 15, PANEL);
  const float cx = x + CARD_W / 2;
  centered(c, gfont::SMALL, day.name, cx, (int)CARD_Y + 22, TEXT);
  drawIcon(c, day.code, true, cx, CARD_Y + 52, 46);
  // "27° 19°": high bright, low dim
  temperature(c, gfont::TITLE, day.hi, cx - 2, (int)CARD_Y + 92, TEXT, 2);
  temperature(c, gfont::SMALL, day.lo, cx + 4, (int)CARD_Y + 92, MUTED, 0);
  if (day.rain >= 20) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d%%", day.rain);
    const float tw = Canvas::textWidth(gfont::SMALL, buf);
    const float left = cx - (tw + 12) * 0.5f;
    raindrop(c, left + 4, CARD_Y + 106, 9, RAIN);
    c.text(gfont::SMALL, buf, left + 12, (int)CARD_Y + 111, RAIN);
  }
}

void picker(Canvas& c, int city) {
  c.text(gfont::TITLE, "Choose a city", 24, 82, TEXT);
  c.fillCircle(CLOSE_X, CLOSE_Y, CLOSE_R, PANEL);
  c.fillCapsule(CLOSE_X - 7, CLOSE_Y - 7, CLOSE_X + 7, CLOSE_Y + 7, 1.6f, TEXT);
  c.fillCapsule(CLOSE_X - 7, CLOSE_Y + 7, CLOSE_X + 7, CLOSE_Y - 7, 1.6f, TEXT);
  for (int i = 0; i < CITY_COUNT; ++i) {
    const float x = PICK_X + (i % 4) * (PICK_W + PICK_GAP), y = PICK_Y + (i / 4) * (PICK_H + PICK_GAP);
    const bool on = i == city;
    if (!on) c.fillRRect(x, y, PICK_W, PICK_H, 16, BORDER);
    c.fillRRect(x + 1, y + 1, PICK_W - 2, PICK_H - 2, 15, on ? ACCENT : PANEL);
    centered(c, gfont::TITLE, CITIES[i].name, x + PICK_W / 2, (int)(y + 36), on ? DARK_TEXT : TEXT);
  }
}

}  // namespace

const char* describe(int code) {
  switch (code) {
    case 0: return "Clear sky";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Foggy";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: return "Snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm, hail";
    default: return "Cloudy";
  }
}

void drawIcon(Canvas& c, int code, bool isDay, float cx, float cy, float s) {
  const Color& bg = BG;
  switch (kindOf(code)) {
    case CLEAR:
      if (isDay) sun(c, cx, cy, s);
      else moon(c, cx, cy, s, bg);
      break;
    case MAINLY_CLEAR:
      if (isDay) sun(c, cx - 0.06f * s, cy - 0.06f * s, s);
      else moon(c, cx - 0.06f * s, cy - 0.06f * s, s, bg);
      cloud(c, cx + 0.16f * s, cy + 0.16f * s, s * 0.55f, CLOUD);
      break;
    case PARTLY:
      if (isDay) sun(c, cx - 0.14f * s, cy - 0.14f * s, s * 0.8f);
      else moon(c, cx - 0.14f * s, cy - 0.14f * s, s * 0.8f, bg);
      cloud(c, cx + 0.06f * s, cy + 0.08f * s, s * 0.85f, CLOUD);
      break;
    case OVERCAST:
      cloud(c, cx + 0.12f * s, cy - 0.1f * s, s * 0.7f, CLOUD_DARK);
      cloud(c, cx - 0.04f * s, cy + 0.06f * s, s * 0.9f, CLOUD);
      break;
    case FOG:
      c.fillCapsule(cx - 0.3f * s, cy - 0.16f * s, cx + 0.3f * s, cy - 0.16f * s, 0.045f * s, CLOUD);
      c.fillCapsule(cx - 0.38f * s, cy, cx + 0.32f * s, cy, 0.045f * s, CLOUD_DARK);
      c.fillCapsule(cx - 0.26f * s, cy + 0.16f * s, cx + 0.38f * s, cy + 0.16f * s, 0.045f * s, CLOUD);
      break;
    case DRIZZLE:
    case RAIN_K: {
      cloud(c, cx, cy - 0.12f * s, s, CLOUD_DARK);
      const bool heavy = kindOf(code) == RAIN_K;
      for (int i = -1; i <= 1; ++i) {
        const float x = cx + i * 0.16f * s;
        if (heavy) {
          c.fillCapsule(x + 0.03f * s, cy + 0.2f * s, x - 0.03f * s, cy + 0.36f * s, 0.032f * s, RAIN);
        } else {
          c.fillCircle(x, cy + 0.24f * s, 0.035f * s, RAIN);
          c.fillCircle(x - 0.04f * s, cy + 0.36f * s, 0.035f * s, RAIN);
        }
      }
      break;
    }
    case SNOW_K:
      cloud(c, cx, cy - 0.12f * s, s, CLOUD);
      for (int i = -1; i <= 1; ++i) {
        c.fillCircle(cx + i * 0.16f * s, cy + 0.24f * s, 0.045f * s, SNOW);
        if (i < 1) c.fillCircle(cx + (i + 0.5f) * 0.16f * s, cy + 0.37f * s, 0.045f * s, SNOW);
      }
      break;
    case THUNDER: {
      cloud(c, cx, cy - 0.14f * s, s, CLOUD_DARK);
      const Pt a[3] = {{cx + 0.06f * s, cy + 0.04f * s}, {cx - 0.12f * s, cy + 0.27f * s}, {cx + 0.02f * s, cy + 0.27f * s}};
      const Pt b[3] = {{cx - 0.03f * s, cy + 0.22f * s}, {cx + 0.11f * s, cy + 0.22f * s}, {cx - 0.08f * s, cy + 0.48f * s}};
      c.fillPolygon(a, 3, SUN, 1.0f, 0.6f);
      c.fillPolygon(b, 3, SUN, 1.0f, 0.6f);
      break;
    }
  }
}

void draw(Canvas& c, const Data& d, int city, bool showPicker, const char* status) {
  if (city < 0 || city >= CITY_COUNT) city = DEFAULT_CITY;
  if (showPicker) {
    picker(c, city);
    return;
  }
  cityButton(c, CITIES[city].name);
  if (!d.valid || d.city != city) {
    centered(c, gfont::SUBTITLE, status && status[0] ? status : "Getting the weather...", 400, 220, MUTED);
    return;
  }
  current(c, d);
  const int n = d.dayCount < 7 ? d.dayCount : 7;
  const float total = n * CARD_W + (n - 1) * CARD_GAP;
  for (int i = 0; i < n; ++i) dayCard(c, d.days[i], (800 - total) / 2 + i * (CARD_W + CARD_GAP));
}

Tap tap(int x, int y, bool showPicker, int& city) {
  if (!showPicker) {
    if (x >= 10 && x <= 330 && y >= 48 && y <= 108) return Tap::OpenPicker;
    return Tap::None;
  }
  const float dx = x - CLOSE_X, dy = y - CLOSE_Y;
  if (dx * dx + dy * dy <= (CLOSE_R + 12) * (CLOSE_R + 12)) return Tap::ClosePicker;
  for (int i = 0; i < CITY_COUNT; ++i) {
    const float bx = PICK_X + (i % 4) * (PICK_W + PICK_GAP), by = PICK_Y + (i / 4) * (PICK_H + PICK_GAP);
    if (x >= bx && x < bx + PICK_W && y >= by && y < by + PICK_H) {
      city = i;
      return Tap::PickCity;
    }
  }
  return Tap::None;
}

}  // namespace weather
