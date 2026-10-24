#include "music_screen.h"

#include <math.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

#include "gfont.h"

namespace music {
namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::Pt;

const Color TILE = gfx::rgb(0x1C2733);
const Color TILE_EDGE = gfx::rgb(0x263241);
const Color TEXT = gfx::rgb(0xF2F6FA);
const Color MUTED = gfx::rgb(0xAEB9C5);
const Color DIM = gfx::rgb(0x7D8996);
const Color TRACK = gfx::rgb(0x2A3441);
const Color ACCENT = gfx::rgb(0x4FE3D6);

constexpr int ART_X = 48, ART_Y = 90;  // album art, top left
constexpr float ART_R = 18;            // corner roundness
constexpr int TEXT_X = 336, TEXT_R = 764;
constexpr int BAR_Y = 300;  // progress bar centre

uint16_t* art = nullptr;
uint32_t artReceiving = 0, artReceived = 0, artDone = 0;

// Album art with rounded corners
void drawArt(Canvas& c) {
  const gfx::Rect box = gfx::intersect(c.area(), gfx::Rect{ART_X, ART_Y, ART, ART});
  if (box.empty()) return;
  for (int y = box.y; y < box.bottom(); ++y) {
    uint16_t* out = c.row(y) + (box.x - c.originX());
    const uint16_t* in = art + (size_t)(y - ART_Y) * ART + (box.x - ART_X);
    const float py = y - ART_Y + 0.5f;
    const float dy = fmaxf(fmaxf(ART_R - py, py - (ART - ART_R)), 0.0f);
    for (int x = box.x; x < box.right(); ++x, ++out, ++in) {
      const float px = x - ART_X + 0.5f;
      const float dx = fmaxf(fmaxf(ART_R - px, px - (ART - ART_R)), 0.0f);
      if (dx > 0 && dy > 0) {  // in a corner
        const float a = 0.5f - (sqrtf(dx * dx + dy * dy) - ART_R);
        if (a <= 0) continue;
        if (a < 1) {
          *out = gfx::blend565(*out, *in, (uint32_t)(a * 255));
          continue;
        }
      }
      *out = *in;
    }
  }
}

// A big music note, for songs without a picture
void drawNote(Canvas& c, float cx, float cy, float s, const Color& col) {
  c.fillCircle(cx - 8.0f * s, cy + 9.0f * s, 5.2f * s, col);
  c.fillCircle(cx + 8.0f * s, cy + 6.0f * s, 5.2f * s, col);
  c.fillCapsule(cx - 3.0f * s, cy - 10.0f * s, cx - 3.0f * s, cy + 9.0f * s, 1.4f * s, col);
  c.fillCapsule(cx + 13.0f * s, cy - 13.0f * s, cx + 13.0f * s, cy + 6.0f * s, 1.4f * s, col);
  const Pt beam[4] = {{cx - 3.0f * s, cy - 10.0f * s},
                      {cx + 13.0f * s, cy - 13.0f * s},
                      {cx + 13.0f * s, cy - 7.5f * s},
                      {cx - 3.0f * s, cy - 4.5f * s}};
  c.fillPolygon(beam, 4, col, 1.0f, 0.8f * s);
}

void drawPlaceholder(Canvas& c) {
  c.fillRRect(ART_X, ART_Y, ART, ART, ART_R, TILE_EDGE);
  c.fillRRect(ART_X + 1, ART_Y + 1, ART - 2, ART - 2, ART_R - 1, TILE);
  drawNote(c, ART_X + ART / 2 - 4, ART_Y + ART / 2, 4.2f, ACCENT);
}

// Cuts text to fit, adding "..."
String fit(const gfont::Font& f, String s, float maxW) {
  if (Canvas::textWidth(f, s.c_str()) <= maxW) return s;
  while (s.length() > 0 && Canvas::textWidth(f, (s + "...").c_str()) > maxW) s.remove(s.length() - 1);
  s.trim();
  return s + "...";
}

// Splits text into up to two lines that fit
int wrap(const gfont::Font& f, const String& text, float maxW, String lines[2]) {
  lines[0] = lines[1] = "";
  if (Canvas::textWidth(f, text.c_str()) <= maxW) {
    lines[0] = text;
    return 1;
  }
  // the last space that still fits on line 1
  int cut = -1;
  for (int i = 0; i < (int)text.length(); ++i)
    if (text[i] == ' ' && Canvas::textWidth(f, text.substring(0, i).c_str()) <= maxW) cut = i;
  if (cut <= 0) {
    lines[0] = fit(f, text, maxW);
    return 1;
  }
  lines[0] = text.substring(0, cut);
  lines[1] = fit(f, text.substring(cut + 1), maxW);
  return 2;
}

String clockText(float seconds) {
  int s = (int)seconds;
  if (s < 0) s = 0;
  char buf[16];
  if (s >= 3600)
    snprintf(buf, sizeof buf, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
  else
    snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
  return buf;
}

float positionNow(const ui::MediaInfo& s, uint32_t now) {
  float p = s.pos;
  if (s.playing) p += (int32_t)(now - s.posAtMs) / 1000.0f;
  if (p < 0) p = 0;
  if (s.dur > 0 && p > s.dur) p = s.dur;
  return p;
}

bool showsProgress(const ui::MediaInfo& s) { return s.title.length() && s.hasPos && s.dur > 1; }

void drawProgress(Canvas& c, const ui::MediaInfo& s, uint32_t now) {
  if (!showsProgress(s)) return;
  const float p = positionNow(s, now);
  const float w = TEXT_R - TEXT_X;
  const float kx = TEXT_X + w * (p / s.dur);
  c.fillRRect(TEXT_X, BAR_Y - 4, w, 8, 4, TRACK);
  if (kx > TEXT_X + 1) c.fillRRect(TEXT_X, BAR_Y - 4, kx - TEXT_X, 8, 4, ACCENT);
  c.fillCircle(kx, BAR_Y, 8, TEXT);
  c.text(gfont::SMALL, clockText(p).c_str(), TEXT_X, BAR_Y + 32, MUTED);
  const String total = clockText(s.dur);
  c.text(gfont::SMALL, total.c_str(), TEXT_R - Canvas::textWidth(gfont::SMALL, total.c_str()), BAR_Y + 32, MUTED);
}

}  // namespace

bool begin() {
#if defined(ESP_PLATFORM)
  art = (uint16_t*)heap_caps_malloc(ART * ART * 2, MALLOC_CAP_SPIRAM);
#else
  art = (uint16_t*)malloc(ART * ART * 2);
#endif
  return art != nullptr;
}

bool artChunk(uint32_t id, uint32_t total, uint32_t offset, const uint8_t* data, size_t len) {
  if (!art || total != (uint32_t)ART * ART * 2 || offset + len > total) return false;
  if (id != artReceiving) {  // a new picture: the old one is being overwritten
    artReceiving = id;
    artReceived = 0;
    artDone = 0;
  }
  memcpy((uint8_t*)art + offset, data, len);
  artReceived += len;
  if (artReceived >= total) {
    artDone = id;
    return true;
  }
  return false;
}

bool artReady(uint32_t id) { return id != 0 && id == artDone; }

void draw(Canvas& c, const ui::MediaInfo& s, uint32_t now, const char* emptyTitle, const char* emptyHint) {
  const float maxW = TEXT_R - TEXT_X;
  if (artReady(s.artId) && s.title.length())
    drawArt(c);
  else
    drawPlaceholder(c);

  if (s.title.length() == 0) {
    c.text(gfont::SMALL, "MUSIC", TEXT_X, 150, ACCENT);
    c.text(gfont::HEADLINE, fit(gfont::HEADLINE, emptyTitle, maxW).c_str(), TEXT_X, 196, TEXT);
    c.text(gfont::SUBTITLE, fit(gfont::SUBTITLE, emptyHint, maxW).c_str(), TEXT_X, 236, MUTED);
    return;
  }

  String lines[2];
  const int n = wrap(gfont::HEADLINE, s.title, maxW, lines);
  // Keep the text block in the middle of the art's height
  int y = n == 2 ? 118 : 138;
  c.text(gfont::SMALL, s.playing ? "NOW PLAYING" : "PAUSED", TEXT_X, y, s.playing ? ACCENT : DIM);
  y += 44;
  for (int i = 0; i < n; ++i, y += 40) c.text(gfont::HEADLINE, lines[i].c_str(), TEXT_X, y, TEXT);
  y -= 2;
  if (s.artist.length()) {
    c.text(gfont::SUBTITLE, fit(gfont::SUBTITLE, s.artist, maxW).c_str(), TEXT_X, y, MUTED);
    y += 30;
  }
  if (s.album.length() && s.album != s.title) c.text(gfont::SMALL, fit(gfont::SMALL, s.album, maxW).c_str(), TEXT_X, y, DIM);
  drawProgress(c, s, now);
}

int progressKey(const ui::MediaInfo& s, uint32_t now) {
  if (!showsProgress(s)) return -1;
  return (int)positionNow(s, now) * 2 + (s.playing ? 1 : 0);
}

}  // namespace music
