// Gorgi's animated face: eyes, eyelids and mouth, with smooth transitions
// between moods, blinking, looking around and a talking mouth.
#pragma once

#include "board.h"

enum class Mood : uint8_t {
  Neutral,
  Happy,
  Sad,
  Angry,
  Surprised,
  Thinking,
  Sleepy,
  Love,
  Confused,
  Excited,
  Listening,
  Asleep,
  Count
};

const char* moodName(Mood m);
bool moodFromName(const char* name, Mood& out);

class Face {
 public:
  // Size of the face area (drawn in an off-screen buffer, then copied to the screen)
  static constexpr int WIDTH = 600;
  static constexpr int HEIGHT = 274;

  bool begin(LGFX& lcd);
  void setMood(Mood m);
  Mood mood() const { return mood_; }
  void setTalking(bool on) { talking_ = on; }
  bool talking() const { return talking_; }

  // Advance the animation and draw the face at (x, y) on the screen
  void render(LGFX& lcd, int x, int y, uint32_t nowMs);

 private:
  // Every field is a float so the whole struct can be blended smoothly
  struct Params {
    float w, h, radius;        // eye size
    float top;                 // top eyelid: 0 = open, 1 = closed
    float slantL, slantR;      // top eyelid angle in degrees per eye
    float bottom;              // bottom eyelid (happy eyes): 0..1
    float dx, dy;              // whole-face offset
    float scaleL, scaleR;      // per-eye size
    float mouthCurve;          // -1 frown .. +1 smile
    float mouthWidth;          // px
    float mouthOpen;           // 0 closed .. 1 open
    float r, g, b;             // eye colour
  };

  void update(uint32_t nowMs);
  void drawEye(float cx, float cy, float scale, float slant, uint16_t col, uint16_t glow);
  void drawHeart(float cx, float cy, float size, uint16_t col, uint16_t glow);
  void drawMouth(float cx, float cy, uint16_t col);
  void drawZzz(uint32_t nowMs, uint16_t col);

  LGFX_Sprite sprite_;
  Mood mood_ = Mood::Neutral;
  Params cur_{};
  Params target_{};
  bool talking_ = false;
  float talkOpen_ = 0;

  uint32_t lastMs_ = 0;
  // Blinking
  uint32_t nextBlinkMs_ = 0;
  uint32_t blinkStartMs_ = 0;
  bool blinking_ = false;
  bool doubleBlink_ = false;
  float blink_ = 1.0f;  // eye height multiplier
  // Looking around
  uint32_t nextGazeMs_ = 0;
  float gazeX_ = 0, gazeY_ = 0;
  float gazeTX_ = 0, gazeTY_ = 0;
};
