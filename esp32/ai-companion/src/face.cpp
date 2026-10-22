#include "face.h"

#include <math.h>
#include <string.h>

#include "display.h"
#include "gfont.h"

namespace {

using gfx::Color;
using gfx::Rect;

const Color BG = gfx::rgb(0x05070A);

// Layout inside the face area
constexpr float EYE_GAP = 120.0f;      // space between the eyes
constexpr float EYE_CENTER_Y = 112.0f;
constexpr float MOUTH_CENTER_Y = 228.0f;
constexpr float MOUTH_DEPTH = 20.0f;   // px of curve at mouthCurve = 1
constexpr float MOUTH_OPEN_H = 40.0f;  // px of opening at mouthOpen = 1
constexpr float MOUTH_THICK = 4.0f;    // half thickness of the mouth line

constexpr float GLOW_SIZE = 18.0f;      // how far the soft glow around the eyes reaches
constexpr float GLOW_STRENGTH = 0.26f;  // how bright it is right at the eye edge (0..1)
constexpr float LID_ROUND = 5.0f;       // rounding of the corners the top eyelid makes
constexpr float LOW_LID_ROUND = 11.0f;  // and the bottom eyelid (its corners are sharper, so rounder)
constexpr float DEG2RAD = 0.017453292519943295f;
constexpr float TWO_PI_F = 6.283185307179586f;

struct MoodDef {
  const char* name;
  float w, h, radius, top, slantL, slantR, bottom, dx, dy, scaleL, scaleR;
  float mouthCurve, mouthWidth, mouthOpen;
  uint32_t color;
  bool hearts;   // heart-shaped eyes
  bool bounce;   // bouncy
  bool wander;   // eyes look around on their own
  bool blink;    // blinks
  bool zzz;      // floating Z's
};

constexpr uint32_t TEAL = 0x4FE3D6;
constexpr uint32_t BLUE = 0x6FB7FF;
constexpr uint32_t ORANGE = 0xFF8A5B;
constexpr uint32_t PINK = 0xFF7EB3;
constexpr uint32_t DIM_TEAL = 0x2A8F87;

// Same order as the Mood enum
const MoodDef MOODS[] = {
  //  name        w    h    rad  top   slL  slR  bot   dx  dy   scL   scR  curve  mW   open  color     hearts bounce wander blink  zzz
  {"neutral",   130, 150,  44, 0.00,   0,   0, 0.00,  0,   0, 1.00, 1.00,  0.50, 60, 0.00, TEAL,     false, false, true,  true,  false},
  {"happy",     130, 150,  44, 0.00,   0,   0, 0.44,  0,   0, 1.00, 1.00,  1.00, 88, 0.25, TEAL,     false, false, true,  true,  false},
  {"sad",       130, 140,  44, 0.36, -14,  14, 0.00,  0,  10, 1.00, 1.00, -0.60, 64, 0.00, BLUE,     false, false, false, true,  false},
  {"angry",     130, 145,  40, 0.34,  18, -18, 0.00,  0,   0, 1.00, 1.00, -0.60, 64, 0.00, ORANGE,   false, false, false, true,  false},
  {"surprised", 140, 175,  70, 0.00,   0,   0, 0.00,  0,  -6, 1.00, 1.00,  0.00, 38, 1.15, TEAL,     false, false, false, false, false},
  {"thinking",  125, 140,  44, 0.20,   0,   0, 0.00, 30, -16, 1.00, 1.00, -0.10, 44, 0.00, TEAL,     false, false, false, true,  false},
  {"sleepy",    130, 150,  44, 0.64,   0,   0, 0.00,  0,  12, 1.00, 1.00,  0.00, 40, 0.00, TEAL,     false, false, false, true,  false},
  {"love",      130, 150,  44, 0.00,   0,   0, 0.00,  0,   0, 1.00, 1.00,  1.00, 88, 0.25, PINK,     true,  false, false, false, false},
  {"confused",  130, 150,  44, 0.15,  10,   0, 0.00, 10,   0, 0.80, 1.05, -0.25, 50, 0.00, TEAL,     false, false, false, true,  false},
  {"excited",   140, 165,  50, 0.00,   0,   0, 0.40,  0,  -6, 1.00, 1.00,  1.00, 92, 0.45, TEAL,     false, true,  false, true,  false},
  {"listening", 138, 168,  52, 0.00,   0,   0, 0.00,  0,  -4, 1.00, 1.00,  0.50, 60, 0.00, TEAL,     false, false, false, true,  false},
  {"asleep",    140,  12,   6, 0.00,   0,   0, 0.00,  0,  14, 1.00, 1.00,  0.00, 36, 0.00, DIM_TEAL, false, false, false, false, true},
};
static_assert(sizeof(MOODS) / sizeof(MOODS[0]) == (size_t)Mood::Count, "MOODS table must match Mood enum");

const MoodDef& def(Mood m) { return MOODS[(int)m]; }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
int32_t q4(float v) { return (int32_t)lroundf(v * 4.0f); }  // to quarter pixels

// sin() of a slowly growing time, kept accurate over days of running
float wave(uint32_t nowMs, double rate) { return sinf((float)fmod((double)nowMs * rate, (double)TWO_PI_F)); }

Color colorOf(uint32_t rgb888) { return gfx::rgb(rgb888); }

// Distance to the overlap of two shapes (a and b are the distances to each),
// with the sharp corner where their edges meet rounded off.
inline float roundedMax(float a, float b, float rho) {
  const float ua = a + rho, ub = b + rho;
  if (ua > 0.0f && ub > 0.0f) return sqrtf(ua * ua + ub * ub) - rho;
  return gfx::fmaxf_(a, b);
}

// ---------------------------------------------------------------- drawing

// One pixel of a shape with a soft glow around it. d = distance to the shape edge.
inline void glowPixel(uint16_t* p, int x, int y, float d, const Color& col) {
  if (d >= GLOW_SIZE) return;
  if (d <= -0.5f) {
    *p = col.c;
    return;
  }
  const float gd = d > 0 ? d : 0;
  const float t = 1.0f - gd / GLOW_SIZE;
  const float ga = GLOW_STRENGTH * t * t;
  uint16_t base = ga > 0.004f ? gfx::blendDither(*p, col, ga, x, y) : *p;
  if (d < 0.5f) base = gfx::blend565(base, col.c, (uint32_t)((0.5f - d) * 255.0f + 0.5f));
  *p = base;
}

// Fills a shape with a soft glow around it. sdf(x, y) = distance to the shape edge.
template <class F>
void shadeGlow(gfx::Canvas& c, float fx0, float fy0, float fx1, float fy1, const Color& col, F&& sdf) {
  const Rect a = c.area();
  const int x0 = gfx::imax(a.x, (int)floorf(fx0)), x1 = gfx::imin(a.right(), (int)ceilf(fx1));
  const int y0 = gfx::imax(a.y, (int)floorf(fy0)), y1 = gfx::imin(a.bottom(), (int)ceilf(fy1));
  for (int y = y0; y < y1; ++y) {
    uint16_t* p = c.row(y) + (x0 - a.x);
    for (int x = x0; x < x1; ++x, ++p) glowPixel(p, x, y, sdf(x + 0.5f, y + 0.5f), col);
  }
}

// The eye: a rounded rectangle, with an optional slanted top eyelid and a bottom eyelid
// (an ellipse pushing up from below). Most of it is solid, so each row fills its solid
// middle in one go and only measures the pixels near the edge.
void drawRoundEye(gfx::Canvas& c, const Face::EyeKey& k) {
  const float cx = k.cx * 0.25f, cy = k.cy * 0.25f;
  const float hw = k.w * 0.125f, hh = k.h * 0.125f;
  const float r = gfx::fminf_(k.r * 0.25f, gfx::fminf_(hw, hh));
  const float ex = hw - r, ey = hh - r;  // corner centres
  const float h = hh * 2, y0f = cy - hh;
  const float top = k.top * (1.0f / 256.0f), bottom = k.bottom * (1.0f / 256.0f);
  const bool lid = top > 0.004f, low = bottom > 0.004f;

  // top eyelid: a slanted line; everything above it is cut away
  const float lineY = y0f + top * h;
  const float t = tanf(k.slant * (1.0f / 16.0f) * DEG2RAD);
  const float inv = 1.0f / sqrtf(1.0f + t * t);
  // bottom eyelid
  const float rx = hw * 2.0f * 0.75f, ry = h * 0.6f;
  const float ecy = y0f + (1.0f - bottom) * h + ry;

  const Color col = colorOf(k.color);
  auto sdf = [&](float px, float py) {
    const float qx = fabsf(px - cx) - ex, qy = fabsf(py - cy) - ey;
    float d = (qx > 0 && qy > 0) ? sqrtf(qx * qx + qy * qy) - r : gfx::fmaxf_(qx, qy) - r;
    if (lid) d = roundedMax(d, (lineY + t * (px - cx) - py) * inv, LID_ROUND);
    if (low) {
      const float ux = (px - cx) / rx, uy = (py - ecy) / ry;
      const float g = ux * ux + uy * uy;
      if (g < 1.5f) {
        if (g < 0.5f) return 1000.0f;  // well inside the cut-out
        const float k0 = sqrtf(g);
        const float vx = ux / rx, vy = uy / ry;
        const float de = k0 * (k0 - 1.0f) / sqrtf(vx * vx + vy * vy);  // < 0 inside the ellipse
        d = roundedMax(d, -de, LOW_LID_ROUND);
      }
    }
    return d;
  };

  const Rect a = c.area();
  const int x0 = gfx::imax(a.x, (int)floorf(cx - hw - GLOW_SIZE)), x1 = gfx::imin(a.right(), (int)ceilf(cx + hw + GLOW_SIZE));
  const int y0 = gfx::imax(a.y, (int)floorf(cy - hh - GLOW_SIZE)), y1 = gfx::imin(a.bottom(), (int)ceilf(cy + hh + GLOW_SIZE));
  const float M = 2.4f;  // safety margin for what counts as surely solid (the rounded corners eat a little)
  const float tiny = 1e-5f;

  for (int y = y0; y < y1; ++y) {
    uint16_t* rowp = c.row(y) - a.x;  // index with the screen x
    const float py = y + 0.5f;

    // Solid middle, as a range of pixel centres [lo, hi]
    float lo = 1.0f, hi = 0.0f;  // empty
    const float ady = fabsf(py - cy);
    float dyc = ady + 0.5f - ey;
    if (dyc < 0) dyc = 0;
    bool any = false;
    if (r > M) {
      if (dyc <= r - M) {
        const float inner = ex + sqrtf((r - M) * (r - M) - dyc * dyc);
        lo = cx - inner;
        hi = cx + inner;
        any = true;
      }
    } else if (ady <= hh - M) {
      lo = cx - (hw - M);
      hi = cx + (hw - M);
      any = true;
    }
    if (any && lid) {
      const float rhs = py - lineY - M / inv;  // t * (x - cx) must be <= rhs
      if (fabsf(t) < tiny) {
        if (rhs < 0) any = false;
      } else if (t > 0) {
        hi = gfx::fminf_(hi, cx + rhs / t);
      } else {
        lo = gfx::fmaxf_(lo, cx + rhs / t);
      }
    }
    float lo2 = 1.0f, hi2 = 0.0f;  // a second solid piece, when the bottom eyelid splits the middle
    if (any && low) {
      const float uy = (py - ecy) / (ry + 4.0f);
      if (fabsf(uy) < 1.0f) {
        const float hc = (rx + 4.0f) * sqrtf(1.0f - uy * uy);  // half width of the cut-out on this row
        lo2 = gfx::fmaxf_(lo, cx + hc);
        hi2 = hi;
        hi = gfx::fminf_(hi, cx - hc);
      }
    }

    // pixel ranges [a1, b1) and [a2, b2) that are solid
    int a1 = x1, b1 = x1, a2 = x1, b2 = x1;
    if (any && lo <= hi) {
      a1 = gfx::imax(x0, (int)ceilf(lo - 0.5f));
      b1 = gfx::imin(x1, (int)floorf(hi - 0.5f) + 1);
      if (a1 >= b1) a1 = b1 = x1;
    }
    if (any && lo2 <= hi2) {
      a2 = gfx::imax(x0, (int)ceilf(lo2 - 0.5f));
      b2 = gfx::imin(x1, (int)floorf(hi2 - 0.5f) + 1);
      if (a2 >= b2) a2 = b2 = x1;
    }
    if (a1 == x1 && a2 < x1) {  // keep them in order
      a1 = a2;
      b1 = b2;
      a2 = b2 = x1;
    }

    int x = x0;
    auto measure = [&](int from, int to) {
      for (int px = from; px < to; ++px) glowPixel(rowp + px, px, y, sdf(px + 0.5f, py), col);
    };
    if (a1 < b1) {
      measure(x, a1);
      for (int px = a1; px < b1; ++px) rowp[px] = col.c;
      x = b1;
      if (a2 < b2) {
        measure(x, a2);
        for (int px = a2; px < b2; ++px) rowp[px] = col.c;
        x = b2;
      }
    }
    measure(x, x1);
  }
}

// Heart with its point at the bottom. w = total width.
void drawHeart(gfx::Canvas& c, const Face::EyeKey& k) {
  const float cx = k.cx * 0.25f, cy = k.cy * 0.25f;
  const float W = k.w * 0.25f;
  const float s = W / 1.2071f;           // pixels per heart unit
  const float tipY = cy + 0.5518f * s;  // the heart is 1.1036 units tall
  const Color col = colorOf(k.color);
  shadeGlow(c, cx - W * 0.5f - GLOW_SIZE, cy - 0.5518f * s - GLOW_SIZE, cx + W * 0.5f + GLOW_SIZE,
            cy + 0.5518f * s + GLOW_SIZE, col, [&](float px, float py) {
              const float x = fabsf(px - cx) / s, y = (tipY - py) / s;
              float d;
              if (y + x > 1.0f) {
                const float dx = x - 0.25f, dy = y - 0.75f;
                d = sqrtf(dx * dx + dy * dy) - 0.35355339f;
              } else {
                const float a = x * x + (y - 1.0f) * (y - 1.0f);
                const float m = 0.5f * gfx::fmaxf_(x + y, 0.0f);
                const float b = (x - m) * (x - m) + (y - m) * (y - m);
                d = sqrtf(gfx::fminf_(a, b));
                if (x < y) d = -d;
              }
              return d * s;
            });
}

void drawEye(gfx::Canvas& c, const Face::EyeKey& k) {
  if (k.kind == 1) drawRoundEye(c, k);
  else if (k.kind == 2) drawHeart(c, k);
}

// The mouth is a curved band that can open into a lens shape. It is drawn column by column.
void drawMouth(gfx::Canvas& c, const Face::MouthKey& k) {
  if (!k.on) return;
  const float cx = k.cx * 0.25f, cy = k.cy * 0.25f, hw = k.halfW * 0.25f;
  const float depth = k.depth * 0.25f, openH = k.openH * 0.25f;
  // A closed mouth is a line with round ends. As it opens, the line thins out
  // until only a clean oval is left.
  const float T = MOUTH_THICK * (1.0f - clampf((openH - 14.0f) / 14.0f, 0.0f, 1.0f));
  const Color col = colorOf(k.color);
  const Rect a = c.area();
  const int xa = gfx::imax(a.x, (int)floorf(cx - hw - T - 2)), xb = gfx::imin(a.right(), (int)ceilf(cx + hw + T + 2));

  for (int px = xa; px < xb; ++px) {
    const float u = (px + 0.5f - cx) / hw;  // -1 at the left end, 1 at the right end
    const float k2 = 1.0f - u * u;
    if (k2 <= 0.0f) continue;  // the round caps below cover the very ends
    const float yc = cy + depth * k2 - depth * 0.5f;
    const float dyc = -2.0f * depth * u / hw;              // slope of the middle line
    const float sq = sqrtf(k2);
    const float o = 0.5f * openH * sq;                     // how far the lens opens above and below
    float dob = 0.5f * openH * (-u / hw) / sq;             // its slope
    dob = clampf(dob, -6.0f, 6.0f);
    const float yTop = yc - T - o, yBot = yc + T + o;
    const float sT = dyc - dob, sB = dyc + dob;
    const float invT = 1.0f / sqrtf(1.0f + sT * sT), invB = 1.0f / sqrtf(1.0f + sB * sB);
    const int ya = gfx::imax(a.y, (int)floorf(yTop - 1.5f / invT - 1)),
              yb = gfx::imin(a.bottom(), (int)ceilf(yBot + 1.5f / invB + 1));
    for (int py = ya; py < yb; ++py) {
      const float fy = py + 0.5f;
      const float d = gfx::fmaxf_((yTop - fy) * invT, (fy - yBot) * invB);
      float al = 0.5f - d;
      if (al <= 0.0f) continue;
      if (al > 1.0f) al = 1.0f;
      uint16_t* p = c.row(py) + (px - a.x);
      const uint32_t a8 = (uint32_t)(al * 255.0f + 0.5f);
      *p = a8 >= 252 ? col.c : gfx::blend565(*p, col.c, a8);
    }
  }
  if (T > 0.6f) {
    const float yEnd = cy - depth * 0.5f;
    c.fillCircle(cx - hw, yEnd, T, col);
    c.fillCircle(cx + hw, yEnd, T, col);
  }
}

void drawZ(gfx::Canvas& c, const Face::ZKey& k) {
  if (!k.on) return;
  const Color col = colorOf(k.color);
  for (int i = 0; i < 3; ++i) {
    if (!k.a[i]) continue;
    const gfont::Font& f = i == 0 ? gfont::Z_BIG : gfont::Z_SMALL;
    const gfont::Glyph& g = f.glyphs[0];
    const float w = gfx::Canvas::textWidth(f, "Z");
    // centre the letter on the point (x, y)
    c.text(f, "Z", k.x[i] - w * 0.5f, (int)lroundf(k.y[i] - g.yoff - g.h * 0.5f), col, k.a[i] * (1.0f / 15.0f));
  }
}

// ---------------------------------------------------------------- what area an item covers

Rect eyeBox(const Face::EyeKey& k) {
  if (!k.kind) return Rect{};
  const float cx = k.cx * 0.25f, cy = k.cy * 0.25f;
  const float hw = k.w * 0.125f + GLOW_SIZE + 2;
  const float hh = (k.kind == 2 ? k.w * 0.125f : k.h * 0.125f) + GLOW_SIZE + 2;
  return Rect{(int)floorf(cx - hw), (int)floorf(cy - hh), (int)ceilf(2 * hw) + 1, (int)ceilf(2 * hh) + 1};
}

Rect mouthBox(const Face::MouthKey& k) {
  if (!k.on) return Rect{};
  const float cx = k.cx * 0.25f, cy = k.cy * 0.25f;
  const float hw = k.halfW * 0.25f + MOUTH_THICK + 3;
  const float hh = fabsf(k.depth * 0.25f) * 0.5f + k.openH * 0.125f + MOUTH_THICK + 3;
  return Rect{(int)floorf(cx - hw), (int)floorf(cy - hh), (int)ceilf(2 * hw) + 1, (int)ceilf(2 * hh) + 1};
}

Rect zBox(const Face::ZKey& k) {
  Rect r;
  if (!k.on) return r;
  for (int i = 0; i < 3; ++i) {
    if (!k.a[i]) continue;
    r = gfx::unite(r, Rect{k.x[i] - 30, k.y[i] - 34, 60, 68});
  }
  return r;
}

bool same(const Face::EyeKey& a, const Face::EyeKey& b) {
  return a.kind == b.kind && a.cx == b.cx && a.cy == b.cy && a.w == b.w && a.h == b.h && a.r == b.r &&
         a.top == b.top && a.slant == b.slant && a.bottom == b.bottom && a.color == b.color;
}

bool same(const Face::MouthKey& a, const Face::MouthKey& b) {
  return a.on == b.on && a.cx == b.cx && a.cy == b.cy && a.halfW == b.halfW && a.depth == b.depth &&
         a.openH == b.openH && a.color == b.color;
}

bool same(const Face::ZKey& a, const Face::ZKey& b) {
  if (a.on != b.on || a.color != b.color) return false;
  for (int i = 0; i < 3; ++i)
    if (a.x[i] != b.x[i] || a.y[i] != b.y[i] || a.a[i] != b.a[i]) return false;
  return true;
}

}  // namespace

const char* moodName(Mood m) {
  return (int)m < (int)Mood::Count ? MOODS[(int)m].name : "neutral";
}

bool moodFromName(const char* name, Mood& out) {
  for (int i = 0; i < (int)Mood::Count; i++) {
    if (strcasecmp(name, MOODS[i].name) == 0) {
      out = (Mood)i;
      return true;
    }
  }
  return false;
}

bool Face::begin() {
  setMood(Mood::Neutral);
  cur_ = target_;
  haveLast_ = false;
  return true;
}

void Face::setMood(Mood m) {
  mood_ = m;
  const MoodDef& d = def(m);
  target_ = {d.w,      d.h,        d.radius,     d.top,        d.slantL,
             d.slantR, d.bottom,   d.dx,         d.dy,         d.scaleL,
             d.scaleR, d.mouthCurve, d.mouthWidth, d.mouthOpen,
             (float)((d.color >> 16) & 0xFF), (float)((d.color >> 8) & 0xFF), (float)(d.color & 0xFF)};
}

void Face::update(uint32_t now) {
  float dt = lastMs_ ? (now - lastMs_) / 1000.0f : 0.0f;
  lastMs_ = now;
  dt = clampf(dt, 0.0f, 0.1f);
  const MoodDef& d = def(mood_);

  // Ease every parameter towards the target mood
  float k = 1.0f - expf(-dt * 10.0f);
  float* c = reinterpret_cast<float*>(&cur_);
  const float* t = reinterpret_cast<const float*>(&target_);
  for (size_t i = 0; i < sizeof(Params) / sizeof(float); i++) c[i] += (t[i] - c[i]) * k;

  // Blinking
  if (d.blink) {
    if (!blinking_ && now >= nextBlinkMs_) {
      blinking_ = true;
      blinkStartMs_ = now;
    }
    if (blinking_) {
      const uint32_t dur = mood_ == Mood::Sleepy ? 450 : 160;
      uint32_t e = now - blinkStartMs_;
      if (e >= dur) {
        blinking_ = false;
        blink_ = 1.0f;
        if (!doubleBlink_ && random(100) < 20) {
          doubleBlink_ = true;
          nextBlinkMs_ = now + 120;
        } else {
          doubleBlink_ = false;
          nextBlinkMs_ = now + random(2200, 6000);
        }
      } else {
        blink_ = 1.0f - 0.92f * sinf(PI * e / dur);
      }
    }
  } else {
    blinking_ = false;
    blink_ = 1.0f;
    nextBlinkMs_ = now + 1500;
  }

  // Looking around
  if (d.wander) {
    if (now >= nextGazeMs_) {
      gazeTX_ = random(-22, 23);
      gazeTY_ = random(-10, 11);
      if (random(100) < 35) gazeTX_ = gazeTY_ = 0;  // often look straight ahead
      nextGazeMs_ = now + random(1200, 4500);
    }
  } else if (mood_ == Mood::Thinking) {
    gazeTX_ = wave(now, 0.0015) * 10.0f;
    gazeTY_ = 0;
  } else {
    gazeTX_ = gazeTY_ = 0;
  }
  float kg = 1.0f - expf(-dt * 14.0f);
  gazeX_ += (gazeTX_ - gazeX_) * kg;
  gazeY_ += (gazeTY_ - gazeY_) * kg;

  // Talking mouth: two mixed waves so it doesn't look mechanical
  float talkTarget = 0;
  if (talking_ && mood_ != Mood::Asleep) {
    const int32_t age = (int32_t)(now - talkLevelMs_);  // can be a little negative: the level arrived just now
    if (talkLevelMs_ && age < 400) {
      talkTarget = clampf(talkLevel_, 0.0f, 1.0f) * 0.75f;  // follow the real voice
    } else {
      float a = 0.5f + 0.5f * wave(now, 0.021);
      float b = 0.6f + 0.4f * wave(now, 0.0073);
      talkTarget = 0.12f + 0.45f * a * b;
    }
  }
  float kt = 1.0f - expf(-dt * 25.0f);
  talkOpen_ += (talkTarget - talkOpen_) * kt;
}

Face::Pose Face::buildPose(uint32_t now, int fx, int fy) const {
  Pose p;
  memset(&p, 0, sizeof(p));
  const MoodDef& d = def(mood_);

  float bob = wave(now, 0.0016) * 1.5f;  // gentle breathing
  if (d.bounce) bob -= fabsf(wave(now, 0.008)) * 10.0f;

  const float gx = cur_.dx + gazeX_, gy = cur_.dy + gazeY_ + bob;
  const float eyeY = EYE_CENTER_Y + gy;
  const float half = EYE_GAP / 2 + cur_.w / 2;
  const uint32_t color = ((uint32_t)clampf(cur_.r + 0.5f, 0, 255) << 16) |
                         ((uint32_t)clampf(cur_.g + 0.5f, 0, 255) << 8) | (uint32_t)clampf(cur_.b + 0.5f, 0, 255);

  for (int i = 0; i < 2; ++i) {
    EyeKey& e = p.eye[i];
    const float cx = WIDTH / 2.0f + (i == 0 ? -half : half) + gx + fx;
    const float cy = eyeY + fy;
    e.color = color;
    e.cx = q4(cx);
    e.cy = q4(cy);
    if (d.hearts) {
      e.kind = 2;
      e.w = q4(140.0f + wave(now, 0.009) * 8.0f);  // heartbeat
      continue;
    }
    const float scale = i == 0 ? cur_.scaleL : cur_.scaleR;
    const float slant = i == 0 ? cur_.slantL : cur_.slantR;
    const float w = cur_.w * scale;
    const float h = gfx::fmaxf_(6.0f, cur_.h * scale * blink_);
    e.kind = 1;
    e.w = q4(w);
    e.h = q4(h);
    e.r = q4(gfx::fminf_(cur_.radius * scale, gfx::fminf_(w, h) / 2));
    e.top = (int16_t)lroundf(clampf(cur_.top, 0, 1) * 256.0f);
    e.slant = (int16_t)lroundf(slant * 16.0f);
    e.bottom = (int16_t)lroundf(clampf(cur_.bottom, 0, 1) * 256.0f);
  }

  MouthKey& m = p.mouth;
  m.on = 1;
  m.color = color;
  m.cx = q4(WIDTH / 2.0f + gx * 0.5f + fx);
  m.cy = q4(MOUTH_CENTER_Y + cur_.dy + bob + fy);
  m.halfW = q4(cur_.mouthWidth * 0.5f);
  m.depth = q4(cur_.mouthCurve * MOUTH_DEPTH);
  m.openH = q4(clampf(cur_.mouthOpen + talkOpen_, 0.0f, 1.3f) * MOUTH_OPEN_H);

  if (d.zzz) {
    ZKey& z = p.z;
    z.on = 1;
    z.color = color;
    for (int i = 0; i < 3; ++i) {
      // Each Z floats up and fades in and out on a 3 second loop
      const float phase = fmodf((float)(now % 3000) / 3000.0f + i / 3.0f, 1.0f);
      z.x[i] = (int16_t)lroundf(WIDTH / 2.0f + 245 + phase * 40 + fx);
      z.y[i] = (int16_t)lroundf(85 - phase * 70 + fy);
      const float fade = clampf(phase / 0.08f, 0, 1) * clampf((0.92f - phase) / 0.3f, 0, 1);
      z.a[i] = (uint8_t)lroundf(fade * 15.0f);
    }
  }
  return p;
}

bool Face::render(int x, int y, uint32_t now) {
  update(now);
  const Rect face{x, y, WIDTH, HEIGHT};
  const Pose p = buildPose(now, x, y);

  // The parts that changed: each old and new position must be redrawn. Parts
  // that are far apart are redrawn separately, which moves less data.
  Rect boxes[4];
  int count = 0;
  auto add = [&](const Rect& r) {
    const Rect clipped = gfx::intersect(r, face);
    if (!clipped.empty()) boxes[count++] = clipped;
  };
  if (!haveLast_) {
    add(face);
  } else {
    for (int i = 0; i < 2; ++i)
      if (!same(p.eye[i], last_.eye[i])) add(gfx::unite(eyeBox(last_.eye[i]), eyeBox(p.eye[i])));
    if (!same(p.mouth, last_.mouth)) add(gfx::unite(mouthBox(last_.mouth), mouthBox(p.mouth)));
    if (!same(p.z, last_.z)) add(gfx::unite(zBox(last_.z), zBox(p.z)));
  }
  if (count == 0) return false;

  // boxes that touch each other become one
  for (bool merged = true; merged;) {
    merged = false;
    for (int i = 0; i < count && !merged; ++i)
      for (int j = i + 1; j < count && !merged; ++j)
        if (!gfx::intersect(boxes[i], boxes[j]).empty()) {
          boxes[i] = gfx::unite(boxes[i], boxes[j]);
          boxes[j] = boxes[--count];
          merged = true;
        }
  }

  for (int i = 0; i < count; ++i) {
    display::render(boxes[i], BG, [&](gfx::Canvas& c) {
      drawEye(c, p.eye[0]);
      drawEye(c, p.eye[1]);
      drawMouth(c, p.mouth);
      drawZ(c, p.z);
    });
  }
  last_ = p;
  haveLast_ = true;
  return true;
}
