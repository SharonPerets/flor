// Gorgi's animated face: eyes, eyelids and mouth, with smooth transitions
// between moods, blinking, looking around and a talking mouth.
//
// Drawing is smooth-edged (see gfx.h) and only the parts of the face that
// changed since the last frame are redrawn (see display.h).
#pragma once

#include "board.h"
#include "gfx.h"

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
  // The face is drawn inside this area of the screen
  static constexpr int WIDTH = 600;
  static constexpr int HEIGHT = 274;

  bool begin();
  void setMood(Mood m);
  Mood mood() const { return mood_; }
  void setTalking(bool on) { talking_ = on; }
  bool talking() const { return talking_; }
  // Loudness of Gorgi's voice right now (0..1), sent by the PC while it speaks.
  // Makes the mouth follow the real voice instead of a made-up movement.
  void setTalkLevel(float level, uint32_t nowMs) {
    talkLevel_ = level;
    talkLevelMs_ = nowMs ? nowMs : 1;
  }

  // Forget what is on screen, so the whole face is drawn again next time.
  void invalidate() { haveLast_ = false; }

  // Advance the animation and draw what changed. (x, y) is where the face
  // area starts on the screen. Returns true if anything was drawn.
  bool render(int x, int y, uint32_t nowMs);

  // What gets drawn, in screen coordinates rounded to quarter pixels. Two
  // frames with the same Pose look exactly the same, so nothing is redrawn.
  // (Public only so the drawing helpers in face.cpp can use them.)
  struct EyeKey {
    uint8_t kind;  // 0 none, 1 round eye, 2 heart
    int32_t cx, cy, w, h, r;
    int16_t top, slant, bottom;
    uint32_t color;
  };
  struct MouthKey {
    uint8_t on;
    int32_t cx, cy, halfW, depth, openH;
    uint32_t color;
  };
  struct ZKey {
    uint8_t on;
    int16_t x[3], y[3];
    uint8_t a[3];
    uint32_t color;
  };
  struct Pose {
    EyeKey eye[2];
    MouthKey mouth;
    ZKey z;
  };

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
  Pose buildPose(uint32_t nowMs, int faceX, int faceY) const;

  Mood mood_ = Mood::Neutral;
  Params cur_{};
  Params target_{};
  bool talking_ = false;
  float talkOpen_ = 0;
  float talkLevel_ = 0;
  uint32_t talkLevelMs_ = 0;

  Pose last_{};
  bool haveLast_ = false;

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
