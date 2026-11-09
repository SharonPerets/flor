#include "gfx.h"

#include "bidi.h"

namespace gfx {

static const float DEG2RAD = 0.017453292519943295f;

Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  int x0 = imin(a.x, b.x), y0 = imin(a.y, b.y);
  int x1 = imax(a.right(), b.right()), y1 = imax(a.bottom(), b.bottom());
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

Rect intersect(const Rect& a, const Rect& b) {
  int x0 = imax(a.x, b.x), y0 = imax(a.y, b.y);
  int x1 = imin(a.right(), b.right()), y1 = imin(a.bottom(), b.bottom());
  if (x1 <= x0 || y1 <= y0) return Rect{};
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

Rect inflate(const Rect& r, int m) { return Rect{r.x - m, r.y - m, r.w + 2 * m, r.h + 2 * m}; }

uint16_t blendDither(uint16_t dst, const Color& src, float alpha, int x, int y) {
  static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const float t = (bayer[y & 3][x & 3] + 0.5f) * (1.0f / 16.0f);  // 0..1
  int r5 = (dst >> 11) & 31, g6 = (dst >> 5) & 63, b5 = dst & 31;
  float dr = (float)((r5 << 3) | (r5 >> 2)), dg = (float)((g6 << 2) | (g6 >> 4)), db = (float)((b5 << 3) | (b5 >> 2));
  float r = dr + (src.r - dr) * alpha + t * 8.0f;
  float g = dg + (src.g - dg) * alpha + t * 4.0f;
  float b = db + (src.b - db) * alpha + t * 8.0f;
  int ri = (int)r >> 3, gi = (int)g >> 2, bi = (int)b >> 3;
  if (ri > 31) ri = 31;
  if (gi > 63) gi = 63;
  if (bi > 31) bi = 31;
  return (uint16_t)((ri << 11) | (gi << 5) | bi);
}

void Canvas::clear(const Color& c) {
  const size_t n = (size_t)w_ * h_;
  for (size_t i = 0; i < n; ++i) buf_[i] = c.c;
}

void Canvas::fillSpan(uint16_t* rowp, int x0, int x1, const Color& c, uint32_t a8) {
  if (x1 <= x0) return;
  if (a8 >= 252) {
    for (int x = x0; x < x1; ++x) rowp[x] = c.c;
  } else if (a8 > 0) {
    for (int x = x0; x < x1; ++x) rowp[x] = blend565(rowp[x], c.c, a8);
  }
}

void Canvas::fillRect(float x, float y, float w, float h, const Color& c, float alpha) {
  fillRRect(x, y, w, h, 0.0f, c, alpha);
}

void Canvas::fillCircle(float cx, float cy, float r, const Color& c, float alpha) {
  fillRRect(cx - r, cy - r, 2 * r, 2 * r, r, c, alpha);
}

// Rounded rectangle. Rows are mostly solid, so the solid middle is filled in one go
// and only the pixels near the edge are measured.
void Canvas::fillRRect(float x, float y, float w, float h, float r, const Color& col, float alpha) {
  if (w <= 0 || h <= 0 || alpha <= 0) return;
  const float hx = w * 0.5f, hy = h * 0.5f, cx = x + hx, cy = y + hy;
  if (r > hx) r = hx;
  if (r > hy) r = hy;
  if (r < 0) r = 0;
  const float ex = hx - r, ey = hy - r;  // how far the corner centres are from the middle
  const int xa = imax(ox_, (int)floorf(x)), xb = imin(ox_ + w_, (int)ceilf(x + w));
  const int ya = imax(oy_, (int)floorf(y)), yb = imin(oy_ + h_, (int)ceilf(y + h));
  const float a255 = alpha * 255.0f;
  const uint32_t aFull = (uint32_t)(a255 + 0.5f);
  const float m = 1.0f;

  for (int py = ya; py < yb; ++py) {
    uint16_t* rowp = row(py) - ox_;  // index with the screen x
    const float ady = fabsf(py + 0.5f - cy);

    // Pixels that are certainly fully covered, even counting a one pixel margin.
    float inner = -1.0f;
    if (r > m) {
      float dyc = ady + 0.5f - ey;
      if (dyc < 0) dyc = 0;
      if (dyc <= r - m) inner = ex + sqrtf((r - m) * (r - m) - dyc * dyc);
    } else if (ady <= hy - m) {
      inner = hx - m;
    }
    int s0 = xb, s1 = xb - 1;  // solid span (empty)
    if (inner >= 0) {
      s0 = imax((int)ceilf(cx - inner - 0.5f), xa);
      s1 = imin((int)floorf(cx + inner - 0.5f), xb - 1);
    }
    const bool hasSolid = s0 <= s1;

    auto edge = [&](int px) {
      float qx = fabsf(px + 0.5f - cx) - ex, qy = ady - ey;
      float d = (qx > 0 && qy > 0) ? sqrtf(qx * qx + qy * qy) - r : fmaxf_(qx, qy) - r;
      float a = 0.5f - d;
      if (a <= 0) return;
      if (a > 1) a = 1;
      uint32_t a8 = (uint32_t)(a * a255 + 0.5f);
      rowp[px] = a8 >= 252 ? col.c : blend565(rowp[px], col.c, a8);
    };

    if (hasSolid) {
      for (int px = xa; px < s0; ++px) edge(px);
      fillSpan(rowp, s0, s1 + 1, col, aFull);
      for (int px = s1 + 1; px < xb; ++px) edge(px);
    } else {
      for (int px = xa; px < xb; ++px) edge(px);
    }
  }
}

void Canvas::fillPolygon(const Pt* p, int n, const Color& col, float alpha, float cr) {
  if (n < 3 || n > 8) return;
  float nx[8], ny[8], nc[8];
  float area2 = 0;
  for (int i = 0; i < n; ++i) {
    const Pt& a = p[i];
    const Pt& b = p[(i + 1) % n];
    area2 += a.x * b.y - b.x * a.y;
  }
  const float sgn = area2 > 0 ? 1.0f : -1.0f;
  float minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
  for (int i = 0; i < n; ++i) {
    const Pt& a = p[i];
    const Pt& b = p[(i + 1) % n];
    float ex = b.x - a.x, ey = b.y - a.y;
    float len = sqrtf(ex * ex + ey * ey);
    if (len < 1e-6f) len = 1e-6f;
    nx[i] = sgn * ey / len;  // outward normal
    ny[i] = -sgn * ex / len;
    nc[i] = -(nx[i] * a.x + ny[i] * a.y);
    minx = fminf_(minx, a.x);
    maxx = fmaxf_(maxx, a.x);
    miny = fminf_(miny, a.y);
    maxy = fmaxf_(maxy, a.y);
  }

  if (cr <= 0.0f) {
    shade(minx - 1, miny - 1, maxx + 1, maxy + 1, col, alpha, [&](float px, float py) {
      float d = nx[0] * px + ny[0] * py + nc[0];
      for (int i = 1; i < n; ++i) {
        float di = nx[i] * px + ny[i] * py + nc[i];
        if (di > d) d = di;
      }
      return d;
    });
    return;
  }

  // Rounded corners: shrink the polygon by cr, then grow the distance by cr again.
  float vx[8], vy[8];  // corners of the shrunken polygon: vx[i] is where edge i starts
  for (int i = 0; i < n; ++i) {
    const int j = (i + n - 1) % n;
    const float c1 = nc[j] + cr, c2 = nc[i] + cr;
    float det = nx[j] * ny[i] - nx[i] * ny[j];
    if (fabsf(det) < 1e-6f) det = det < 0 ? -1e-6f : 1e-6f;
    vx[i] = (-c1 * ny[i] + c2 * ny[j]) / det;
    vy[i] = (-nx[j] * c2 + nx[i] * c1) / det;
  }
  shade(minx - 1, miny - 1, maxx + 1, maxy + 1, col, alpha, [&](float px, float py) {
    float dmax = nx[0] * px + ny[0] * py + nc[0] + cr;
    for (int i = 1; i < n; ++i) {
      float di = nx[i] * px + ny[i] * py + nc[i] + cr;
      if (di > dmax) dmax = di;
    }
    if (dmax <= 0.0f) return dmax - cr;
    float best = 1e9f;  // outside the shrunken polygon: distance to its nearest side
    for (int i = 0; i < n; ++i) {
      const int k = (i + 1) % n;
      const float abx = vx[k] - vx[i], aby = vy[k] - vy[i];
      const float pax = px - vx[i], pay = py - vy[i];
      const float ab2 = abx * abx + aby * aby;
      float t = ab2 > 1e-9f ? (pax * abx + pay * aby) / ab2 : 0.0f;
      t = t < 0 ? 0 : (t > 1 ? 1 : t);
      const float dx = pax - abx * t, dy = pay - aby * t;
      const float dd = dx * dx + dy * dy;
      if (dd < best) best = dd;
    }
    return sqrtf(best) - cr;
  });
}

void Canvas::fillCapsule(float x0, float y0, float x1, float y1, float r, const Color& col, float alpha) {
  const float abx = x1 - x0, aby = y1 - y0;
  const float ab2 = abx * abx + aby * aby;
  const float inv = ab2 > 1e-9f ? 1.0f / ab2 : 0.0f;
  shade(fminf_(x0, x1) - r - 1, fminf_(y0, y1) - r - 1, fmaxf_(x0, x1) + r + 1, fmaxf_(y0, y1) + r + 1, col, alpha,
        [&](float px, float py) {
          float pax = px - x0, pay = py - y0;
          float t = (pax * abx + pay * aby) * inv;
          t = t < 0 ? 0 : (t > 1 ? 1 : t);
          float dx = pax - abx * t, dy = pay - aby * t;
          return sqrtf(dx * dx + dy * dy) - r;
        });
}

void Canvas::strokeArc(float cx, float cy, float R, float thick, float a0, float a1, const Color& col, float alpha) {
  const float mid = (a0 + a1) * 0.5f * DEG2RAD, half = (a1 - a0) * 0.5f * DEG2RAD;
  const float ux = cosf(mid), uy = sinf(mid), cosH = cosf(half);
  const float t2 = thick * 0.5f;
  const float ext = R + t2 + 1;
  shade(cx - ext, cy - ext, cx + ext, cy + ext, col, alpha, [&](float px, float py) {
    float dx = px - cx, dy = py - cy;
    float len = sqrtf(dx * dx + dy * dy);
    float d = fabsf(len - R) - t2;
    if (d > 1.0f) return d;
    if (dx * ux + dy * uy < cosH * len) return 99.0f;  // outside the angle; the round caps below finish the ends
    return d;
  });
  fillCircle(cx + R * cosf(a0 * DEG2RAD), cy + R * sinf(a0 * DEG2RAD), t2, col, alpha);
  fillCircle(cx + R * cosf(a1 * DEG2RAD), cy + R * sinf(a1 * DEG2RAD), t2, col, alpha);
}

namespace {

// The glyph for one letter, and where its pixels are. nullptr = the font can't draw it (skipped).
const gfont::Glyph* glyphFor(const gfont::Font& f, const gfont::Extra* ex, uint32_t cp, const uint8_t** alpha) {
  switch (cp) {  // stand-ins for letters the fonts don't have
    case 0x2018: case 0x2019: case 0x05F3: cp = '\''; break;
    case 0x201C: case 0x201D: case 0x05F4: cp = '"'; break;
    case 0x2013: case 0x2014: case 0x05BE: cp = '-'; break;
    case 0xA0: case '\t': cp = ' '; break;
    default: break;
  }
  if (cp >= f.first && cp <= f.last) {
    *alpha = f.alpha;
    return &f.glyphs[cp - f.first];
  }
  if (ex && cp >= ex->first && cp <= ex->last) {
    *alpha = ex->alpha;
    return &ex->glyphs[cp - ex->first];
  }
  return nullptr;
}

}  // namespace

float Canvas::textWidth(const gfont::Font& f, const char* s) {
  const gfont::Extra* ex = gfont::extraFor(f);
  float w = 0;
  const uint8_t* alpha;
  while (uint32_t cp = bidi::next(s)) {
    const gfont::Glyph* g = glyphFor(f, ex, cp, &alpha);
    if (g) w += g->adv16 * (1.0f / 16.0f);
  }
  return w;
}

void Canvas::blitGlyph(const uint8_t* alphaBase, const gfont::Glyph& g, int gx, int gy, const Color& col, float alpha) {
  const int x0 = imax(gx, ox_), x1 = imin(gx + g.w, ox_ + w_);
  const int y0 = imax(gy, oy_), y1 = imin(gy + g.h, oy_ + h_);
  if (x0 >= x1 || y0 >= y1) return;
  const uint32_t scale = (uint32_t)(alpha * 256.0f + 0.5f);
  const uint8_t* src = alphaBase + g.offset;
  for (int y = y0; y < y1; ++y) {
    const uint8_t* s = src + (size_t)(y - gy) * g.w + (x0 - gx);
    uint16_t* d = row(y) + (x0 - ox_);
    for (int x = x0; x < x1; ++x, ++s, ++d) {
      uint32_t a = (*s * scale) >> 8;
      if (a == 0) continue;
      *d = a >= 252 ? col.c : blend565(*d, col.c, a);
    }
  }
}

void Canvas::text(const gfont::Font& f, const char* s, float x, int baseline, const Color& col, float alpha) {
  char ordered[400];
  if (bidi::hasRtl(s)) s = bidi::visual(s, ordered, sizeof ordered);  // Hebrew: put the line in drawing order
  const gfont::Extra* ex = gfont::extraFor(f);
  float pen = x;
  const uint8_t* alphaBase;
  while (uint32_t cp = bidi::next(s)) {
    const gfont::Glyph* g = glyphFor(f, ex, cp, &alphaBase);
    if (!g) continue;
    if (g->w) blitGlyph(alphaBase, *g, (int)floorf(pen + 0.5f) + g->xoff, baseline + g->yoff, col, alpha);
    pen += g->adv16 * (1.0f / 16.0f);
  }
}

void Canvas::textIn(const gfont::Font& f, const char* s, float x, float w, int baseline, const Color& c, float alpha) {
  text(f, s, bidi::isRtl(s) ? x + w - textWidth(f, s) : x, baseline, c, alpha);
}

}  // namespace gfx
