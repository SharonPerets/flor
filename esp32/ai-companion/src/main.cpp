// Gorgi - an AI desk companion for the Waveshare ESP32-S3-Touch-LCD-7
//
// Step 1: face, touch and media bar. No Wi-Fi yet: tapping the face plays a
// short demo conversation, and the media buttons only make Gorgi react.
// Step 2 connects the media bar to the Gorgi Helper app on the PC.
//
// Touch controls:
//   tap face        -> demo: listening, thinking, talking
//   hold face 1 s   -> show the next expression (handy for testing)
//   media buttons   -> Gorgi reacts (real PC control comes in step 2)

#include <Arduino.h>

#include "board.h"
#include "face.h"
#include "ui.h"

namespace {

LGFX lcd;
Face face;

// How long before Gorgi gets sleepy / falls asleep with nobody around
constexpr uint32_t SLEEPY_AFTER_MS = 60 * 1000;
constexpr uint32_t ASLEEP_AFTER_MS = 3 * 60 * 1000;
constexpr uint32_t LONG_PRESS_MS = 900;
constexpr uint32_t FRAME_MS = 33;  // ~30 frames per second

enum class Phase { Idle, Listening, Thinking, Talking, Sleepy, Asleep };
Phase phase = Phase::Idle;
uint32_t phaseUntil = 0;     // when the current phase ends (0 = never)
uint32_t lastActivity = 0;   // last touch
uint32_t moodHoldUntil = 0;  // keep the current mood until then, then go neutral

ui::MediaInfo media;

// Touch tracking
bool touching = false;
uint32_t touchStart = 0;
ui::Target touchTarget = ui::Target::None;
bool longPressDone = false;
int testMoodIndex = 0;

struct Line {
  Mood mood;
  const char* text;
};

const Line DEMO_ANSWERS[] = {
  {Mood::Happy, "I'm doing great! Want me to put on something upbeat?"},
  {Mood::Excited, "Ooh, I love chatting with you! Soon I'll really hear you through your PC mic."},
  {Mood::Thinking, "Hmm... I don't have a brain yet. Step 3 gives me one!"},
  {Mood::Love, "You tapped me! That's my favorite thing."},
  {Mood::Surprised, "Whoa, hi! You scared me a little."},
};

void showIdleStatus() { ui::setStatus("Tap me to talk", ui::COLOR_TEXT_DIM, false); }

void setPhase(Phase p, uint32_t durationMs = 0) {
  phase = p;
  phaseUntil = durationMs ? millis() + durationMs : 0;
}

// Make Gorgi say something with a mood. The mouth moves for roughly as long
// as it would take to read the text out loud.
void say(Mood mood, const String& text) {
  face.setMood(mood);
  face.setTalking(true);
  ui::setSubtitle(text);
  showIdleStatus();
  setPhase(Phase::Talking, 900 + text.length() * 55);
}

// A short reaction without the full talking animation
void react(Mood mood, const String& text) {
  if (phase == Phase::Listening || phase == Phase::Thinking) return;
  face.setMood(mood);
  face.setTalking(false);
  ui::setSubtitle(text);
  showIdleStatus();
  setPhase(Phase::Idle);
  moodHoldUntil = millis() + 3500;
}

void wakeUp() {
  face.setMood(Mood::Surprised);
  face.setTalking(false);
  ui::setSubtitle("Oh! You're back!");
  showIdleStatus();
  setPhase(Phase::Idle);
  moodHoldUntil = millis() + 1500;
}

void startDemoConversation() {
  face.setMood(Mood::Listening);
  face.setTalking(false);
  ui::setSubtitle("I'm listening...");
  ui::setStatus("Listening...", ui::COLOR_LISTEN, true);
  setPhase(Phase::Listening, 2200);
}

void onTap(ui::Target target) {
  switch (target) {
    case ui::Target::Face:
      if (phase == Phase::Listening || phase == Phase::Thinking) return;
      startDemoConversation();
      break;
    case ui::Target::PlayPause:
      media.playing = !media.playing;
      ui::setPlaying(media.playing);
      react(media.playing ? Mood::Happy : Mood::Neutral, media.playing ? "Music on!" : "Paused. Enjoy the quiet.");
      break;
    case ui::Target::Next:
      react(Mood::Happy, "Next song! (I'll really skip it once my PC helper is ready.)");
      break;
    case ui::Target::Prev:
      react(Mood::Surprised, "Back to the last one? Good choice!");
      break;
    default:
      break;
  }
}

void onVolumeReleased() {
  int v = media.volume;
  if (v < 15)
    react(Mood::Sad, "So quiet... I can barely hear it.");
  else if (v > 85)
    react(Mood::Surprised, "Whoa, that's loud!");
  else
    react(Mood::Happy, String("Volume ") + v + "%");
}

void onLongPressFace() {
  testMoodIndex = (testMoodIndex + 1) % (int)Mood::Count;
  Mood m = (Mood)testMoodIndex;
  face.setMood(m);
  face.setTalking(false);
  ui::setSubtitle(String("Expression: ") + moodName(m));
  setPhase(Phase::Idle);
  moodHoldUntil = millis() + 8000;
}

void handleTouch(uint32_t now) {
  TouchPoint tp = board::readTouch();

  if (tp.pressed && !touching) {
    touching = true;
    touchStart = now;
    longPressDone = false;
    touchTarget = ui::hitTest(tp.x, tp.y);
    ui::showPressed(touchTarget);

    bool wasAsleep = phase == Phase::Asleep || phase == Phase::Sleepy;
    lastActivity = now;
    if (wasAsleep) {
      wakeUp();
      touchTarget = ui::Target::None;  // this touch only wakes Gorgi up
    }
  }

  if (tp.pressed && touching) {
    lastActivity = now;
    if (touchTarget == ui::Target::Volume) {
      media.volume = ui::volumeFromX(tp.x);
      ui::setVolume(media.volume);
    }
    if (touchTarget == ui::Target::Face && !longPressDone && now - touchStart >= LONG_PRESS_MS) {
      longPressDone = true;
      onLongPressFace();
    }
  }

  if (!tp.pressed && touching) {
    touching = false;
    if (touchTarget == ui::Target::Volume)
      onVolumeReleased();
    else if (!longPressDone && now - touchStart < LONG_PRESS_MS)
      onTap(touchTarget);
  }
}

void updatePhase(uint32_t now) {
  bool timeUp = phaseUntil && now >= phaseUntil;

  switch (phase) {
    case Phase::Listening:
      if (timeUp) {
        face.setMood(Mood::Thinking);
        ui::setSubtitle("Hmm, let me think...");
        ui::setStatus("Thinking...", ui::COLOR_THINK, false);
        setPhase(Phase::Thinking, 1600);
      }
      break;
    case Phase::Thinking:
      if (timeUp) {
        const Line& l = DEMO_ANSWERS[random(sizeof(DEMO_ANSWERS) / sizeof(DEMO_ANSWERS[0]))];
        say(l.mood, l.text);
      }
      break;
    case Phase::Talking:
      if (timeUp) {
        face.setTalking(false);
        setPhase(Phase::Idle);
        moodHoldUntil = now + 4000;
      }
      break;
    case Phase::Idle:
      if (moodHoldUntil && now >= moodHoldUntil) {
        moodHoldUntil = 0;
        face.setMood(Mood::Neutral);
        ui::setSubtitle("");
      }
      if (now - lastActivity > SLEEPY_AFTER_MS) {
        face.setMood(Mood::Sleepy);
        ui::setSubtitle("Getting sleepy...");
        setPhase(Phase::Sleepy);
      }
      break;
    case Phase::Sleepy:
      if (now - lastActivity > ASLEEP_AFTER_MS) {
        face.setMood(Mood::Asleep);
        ui::setSubtitle("");
        ui::setStatus("Tap to wake me", ui::COLOR_TEXT_DIM, false);
        setPhase(Phase::Asleep);
      }
      break;
    case Phase::Asleep:
      break;
  }
}

void fatal(const char* msg) {
  Serial.println(msg);
  lcd.setTextColor(TFT_RED);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.drawString(msg, 20, 20);
  while (true) delay(1000);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Gorgi starting...");

  board::begin();
  lcd.init();
  lcd.fillScreen(TFT_BLACK);

  if (!psramFound()) fatal("PSRAM not found - check board settings");
  if (!face.begin(lcd)) fatal("Not enough memory for the face");

  ui::begin(lcd);
  media.title = "No music info yet";
  media.artist = "The PC helper connects in step 2";
  media.playing = false;
  media.volume = 50;
  ui::setMedia(media);
  ui::setClock("Gorgi");
  showIdleStatus();

  randomSeed(esp_random());
  lastActivity = millis();
  say(Mood::Happy, "Hi, I'm Gorgi! Tap me to say hello.");
  Serial.println("Gorgi ready");
}

void loop() {
  static uint32_t lastFrame = 0;
  static uint32_t frames = 0, fpsTimer = 0;
  uint32_t now = millis();

  handleTouch(now);
  updatePhase(now);

  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    face.render(lcd, ui::FACE_X, ui::FACE_Y, now);
    ui::update(now);
    frames++;
  }

  if (now - fpsTimer >= 10000) {
    Serial.printf("fps: %.1f  free heap: %u  free psram: %u\n", frames / ((now - fpsTimer) / 1000.0f),
                  ESP.getFreeHeap(), ESP.getFreePsram());
    frames = 0;
    fpsTimer = now;
  }
  delay(2);
}
