#include "face.h"

#include <math.h>

namespace {

constexpr uint16_t rgb565(uint32_t hex) {
  return ((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F);
}

constexpr uint16_t BG = rgb565(0x05070A);

// Layout inside the face area
constexpr float EYE_GAP = 120.0f;     // space between the eyes
constexpr float EYE_CENTER_Y = 112.0f;
constexpr float MOUTH_CENTER_Y = 228.0f;
constexpr float MOUTH_DEPTH = 20.0f;  // px of curve at mouthCurve = 1
constexpr float MOUTH_OPEN_H = 40.0f; // px of opening at mouthOpen = 1
constexpr float MOUTH_THICK = 4.0f;   // half thickness of the mouth line

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
  {"surprised", 140, 175,  70, 0.00,   0,   0, 0.00,  0,  -6, 1.00, 1.00,  0.00, 32, 1.00, TEAL,     false, false, false, false, false},
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

bool Face::begin(LGFX& lcd) {
  sprite_.setColorDepth(16);
  sprite_.setPsram(true);
  if (!sprite_.createSprite(WIDTH, HEIGHT)) return false;
  setMood(Mood::Neutral);
  cur_ = target_;
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
    gazeTX_ = sinf(now * 0.0015f) * 10.0f;
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
    float a = 0.5f + 0.5f * sinf(now * 0.021f);
    float b = 0.6f + 0.4f * sinf(now * 0.0073f);
    talkTarget = 0.12f + 0.45f * a * b;
  }
  float kt = 1.0f - expf(-dt * 25.0f);
  talkOpen_ += (talkTarget - talkOpen_) * kt;
}

void Face::render(LGFX& lcd, int x, int y, uint32_t now) {
  update(now);
  const MoodDef& d = def(mood_);

  sprite_.fillScreen(BG);
  uint16_t col = sprite_.color565(cur_.r, cur_.g, cur_.b);
  uint16_t glow = sprite_.color565(cur_.r * 0.18f, cur_.g * 0.18f, cur_.b * 0.18f);

  float bob = sinf(now * 0.0016f) * 2.0f;  // gentle breathing
  if (d.bounce) bob -= fabsf(sinf(now * 0.008f)) * 10.0f;

  float gx = cur_.dx + gazeX_;
  float gy = cur_.dy + gazeY_ + bob;
  float eyeY = EYE_CENTER_Y + gy;
  float half = EYE_GAP / 2 + cur_.w / 2;

  if (d.hearts) {
    float size = 140.0f + sinf(now * 0.009f) * 8.0f;  // heartbeat
    drawHeart(WIDTH / 2 - half + gx, eyeY, size, col, glow);
    drawHeart(WIDTH / 2 + half + gx, eyeY, size, col, glow);
  } else {
    drawEye(WIDTH / 2 - half + gx, eyeY, cur_.scaleL, cur_.slantL, col, glow);
    drawEye(WIDTH / 2 + half + gx, eyeY, cur_.scaleR, cur_.slantR, col, glow);
  }

  drawMouth(WIDTH / 2 + gx * 0.5f, MOUTH_CENTER_Y + cur_.dy + bob, col);
  if (d.zzz) drawZzz(now, col);

  sprite_.pushSprite(&lcd, x, y);
}

void Face::drawEye(float cx, float cy, float scale, float slant, uint16_t col, uint16_t glow) {
  float w = cur_.w * scale;
  float h = cur_.h * scale * blink_;
  if (h < 6) h = 6;
  float r = cur_.radius * scale;
  r = fminf(r, fminf(w, h) / 2);
  int x0 = (int)(cx - w / 2);
  int y0 = (int)(cy - h / 2);

  sprite_.fillRoundRect(x0 - 7, y0 - 7, (int)w + 14, (int)h + 14, (int)r + 7, glow);
  sprite_.fillRoundRect(x0, y0, (int)w, (int)h, (int)r, col);

  // Top eyelid: everything above a slanted line is painted with the background
  if (cur_.top > 0.01f) {
    float lineY = y0 + cur_.top * h;
    float t = tanf(slant * DEG_TO_RAD);
    float xl = x0 - 12, xr = x0 + w + 12;
    float yl = lineY + t * (xl - cx);
    float yr = lineY + t * (xr - cx);
    float yt = y0 - 14;
    sprite_.fillTriangle(xl, yt, xr, yt, xr, yr, BG);
    sprite_.fillTriangle(xl, yt, xr, yr, xl, yl, BG);
  }

  // Bottom eyelid: a big ellipse pushes up from below, giving "^ ^" happy eyes
  if (cur_.bottom > 0.01f) {
    float rx = w * 0.75f;
    float ry = h * 0.6f;
    float ecy = y0 + (1.0f - cur_.bottom) * h + ry;
    sprite_.fillEllipse(cx, ecy, rx, ry, BG);
  }
}

void Face::drawHeart(float cx, float cy, float s, uint16_t col, uint16_t glow) {
  auto heart = [&](float size, uint16_t c) {
    float r = size * 0.28f;
    sprite_.fillCircle(cx - size * 0.22f, cy - size * 0.12f, r, c);
    sprite_.fillCircle(cx + size * 0.22f, cy - size * 0.12f, r, c);
    sprite_.fillTriangle(cx - size * 0.49f, cy - size * 0.02f, cx + size * 0.49f, cy - size * 0.02f, cx,
                         cy + size * 0.46f, c);
  };
  heart(s * 1.1f, glow);
  heart(s, col);
}

void Face::drawMouth(float cx, float cy, uint16_t col) {
  float halfW = cur_.mouthWidth / 2;
  float depth = cur_.mouthCurve * MOUTH_DEPTH;
  float open = clampf(cur_.mouthOpen + talkOpen_, 0.0f, 1.3f);
  float openH = open * MOUTH_OPEN_H;

  // Draw column by column: a curved line that can open into a filled shape
  for (int i = (int)-halfW; i <= (int)halfW; i++) {
    float t = i / halfW;
    float k = 1.0f - t * t;
    float yc = cy + depth * k - depth * 0.5f;
    float o = openH * 0.5f * sqrtf(fmaxf(k, 0.0f));
    int top = (int)(yc - MOUTH_THICK - o);
    int bot = (int)(yc + MOUTH_THICK + o);
    sprite_.drawFastVLine((int)cx + i, top, bot - top + 1, col);
  }
  // Rounded ends
  sprite_.fillCircle(cx - halfW, cy - depth * 0.5f, MOUTH_THICK, col);
  sprite_.fillCircle(cx + halfW, cy - depth * 0.5f, MOUTH_THICK, col);
}

void Face::drawZzz(uint32_t now, uint16_t col) {
  sprite_.setTextColor(col);
  sprite_.setTextDatum(textdatum_t::middle_center);
  for (int i = 0; i < 3; i++) {
    // Each Z floats up and fades out on a 3 second loop
    float phase = fmodf(now / 3000.0f + i / 3.0f, 1.0f);
    float zx = WIDTH / 2 + 245 + phase * 40;
    float zy = 85 - phase * 70;
    sprite_.setFont(i == 0 ? &fonts::FreeSansBold18pt7b : &fonts::FreeSansBold12pt7b);
    if (phase < 0.85f) sprite_.drawString("Z", (int)zx, (int)zy);
  }
}
