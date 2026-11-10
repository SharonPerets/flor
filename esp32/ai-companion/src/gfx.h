// A tiny 2D drawing toolkit with smooth (anti-aliased) edges.
//
// Everything is drawn into a small RGB565 buffer called a Canvas. The Canvas
// stands for a rectangle of the real screen, and all shapes are given in
// screen coordinates (decimals allowed, so edges can sit between pixels).
// Shapes outside the Canvas are skipped, so the same drawing code can be run
// over any part of the screen. See display.h for putting a Canvas on screen.
//
// Pure C++ with no hardware parts, so it also runs on a PC for previews.
#pragma once

#include <math.h>
#include <stdint.h>

#include "gfont.h"

namespace gfx {

struct Rect {
  int x, y, w, h;
  constexpr Rect() : x(0), y(0), w(0), h(0) {}
  constexpr Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
  bool empty() const { return w <= 0 || h <= 0; }
  int right() const { return x + w; }
  int bottom() const { return y + h; }
};

inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }
inline float fminf_(float a, float b) { return a < b ? a : b; }
inline float fmaxf_(float a, float b) { return a > b ? a : b; }
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

Rect unite(const Rect& a, const Rect& b);      // smallest rectangle holding both (empty ones are ignored)
Rect intersect(const Rect& a, const Rect& b);  // overlap, possibly empty
Rect inflate(const Rect& r, int margin);

struct Color {
  uint8_t r, g, b;
  uint16_t c;  // the same colour as RGB565
};

constexpr Color rgb(uint32_t hex) {
  return Color{(uint8_t)(hex >> 16), (uint8_t)(hex >> 8), (uint8_t)hex,
               (uint16_t)(((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F))};
}

struct Pt {
  float x, y;
};

// Mixes src over dst. a8 = 0 (keep dst) .. 255 (src).
inline uint16_t blend565(uint16_t dst, uint16_t src, uint32_t a8) {
  uint32_t a = (a8 + 4) >> 3;  // 0..32
  uint32_t d = (dst | ((uint32_t)dst << 16)) & 0x07E0F81Fu;
  uint32_t s = (src | ((uint32_t)src << 16)) & 0x07E0F81Fu;
  uint32_t r = ((((s - d) * a) >> 5) + d) & 0x07E0F81Fu;
  return (uint16_t)(r | (r >> 16));
}

// Mixes a colour into a pixel with ordered dithering, so soft glows fade
// without visible steps (RGB565 only has 32 levels of red and blue).
uint16_t blendDither(uint16_t dst, const Color& src, float alpha, int x, int y);

class Canvas {
 public:
  // The buffer holds the screen rectangle (ox, oy, w, h), rows packed one after another.
  void begin(uint16_t* buf, int w, int h, int ox, int oy) {
    buf_ = buf;
    w_ = w;
    h_ = h;
    ox_ = ox;
    oy_ = oy;
  }
  Rect area() const { return Rect{ox_, oy_, w_, h_}; }

  void clear(const Color& c);

  // alpha (0..1) fades a whole shape.
  void fillRect(float x, float y, float w, float h, const Color& c, float alpha = 1.0f);
  void fillRRect(float x, float y, float w, float h, float r, const Color& c, float alpha = 1.0f);
  void fillCircle(float cx, float cy, float r, const Color& c, float alpha = 1.0f);
  // Convex polygon, up to 8 corners, any winding. cornerRadius rounds the corners.
  void fillPolygon(const Pt* pts, int n, const Color& c, float alpha = 1.0f, float cornerRadius = 0.0f);
  // A line with round ends.
  void fillCapsule(float x0, float y0, float x1, float y1, float r, const Color& c, float alpha = 1.0f);
  // A curved line (part of a circle): from angle a0 to a1 in degrees, 0 = right, 90 = down.
  void strokeArc(float cx, float cy, float radius, float thickness, float a0, float a1, const Color& c,
                 float alpha = 1.0f);

  // Text. x is the left edge, baseline the y of the line the letters sit on.
  void text(const gfont::Font& f, const char* s, float x, int baseline, const Color& c, float alpha = 1.0f);
  // Like text(), in a box from x to x + w: Hebrew lines sit at the right edge, others at the left.
  void textIn(const gfont::Font& f, const char* s, float x, float w, int baseline, const Color& c, float alpha = 1.0f);
  static float textWidth(const gfont::Font& f, const char* s);

  // Generic shape: sdf(x, y) must return the distance from the pixel centre (x, y)
  // to the shape edge: negative inside, positive outside, in pixels. Pixels more
  // than half a pixel inside are filled, the edge is smoothed.
  template <class F>
  void shade(float fx0, float fy0, float fx1, float fy1, const Color& c, float alpha, F&& sdf) {
    int x0 = imax(ox_, (int)floorf(fx0)), x1 = imin(ox_ + w_, (int)ceilf(fx1));
    int y0 = imax(oy_, (int)floorf(fy0)), y1 = imin(oy_ + h_, (int)ceilf(fy1));
    const float a255 = alpha * 255.0f;
    for (int y = y0; y < y1; ++y) {
      uint16_t* p = row(y) + (x0 - ox_);
      for (int x = x0; x < x1; ++x, ++p) {
        float a = 0.5f - sdf(x + 0.5f, y + 0.5f);
        if (a <= 0.0f) continue;
        if (a >= 1.0f) a = 1.0f;
        uint32_t a8 = (uint32_t)(a * a255 + 0.5f);
        *p = a8 >= 252 ? c.c : blend565(*p, c.c, a8);
      }
    }
  }

  uint16_t* row(int y) { return buf_ + (size_t)(y - oy_) * w_; }  // pixel at screen x is row(y)[x - origin x]
  int originX() const { return ox_; }
  int originY() const { return oy_; }
  int width() const { return w_; }
  int height() const { return h_; }

 private:
  void fillSpan(uint16_t* rowp, int x0, int x1, const Color& c, uint32_t a8);
  void blitGlyph(const uint8_t* alphaBase, const gfont::Glyph& g, int x, int y, const Color& c, float alpha);

  uint16_t* buf_ = nullptr;
  int w_ = 0, h_ = 0, ox_ = 0, oy_ = 0;
};

}  // namespace gfx
