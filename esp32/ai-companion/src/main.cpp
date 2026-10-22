// Gorgi - an AI desk companion for the Waveshare ESP32-S3-Touch-LCD-7
//
// Gorgi joins your Wi-Fi and talks to the Gorgi Helper app on your Windows PC.
// The helper does the listening (PC mic), thinking (Ollama Cloud) and talking
// (PC speakers); Gorgi shows it all with its face and subtitles. The media bar
// controls music on the PC.
//
// Touch controls:
//   tap face          -> talk to Gorgi (tap again to stop)
//   hold face 1 s     -> show the next expression (handy for testing)
//   media buttons     -> control music on the PC
//   hold clock 5 s    -> forget Wi-Fi and open the "Gorgi-Setup" network again

#include <Arduino.h>

#include "board.h"
#include "display.h"
#include "face.h"
#include "pclink.h"
#include "ui.h"

namespace {

LGFX lcd;
Face face;

// How long before Gorgi gets sleepy / falls asleep with nobody around
constexpr uint32_t SLEEPY_AFTER_MS = 60 * 1000;
constexpr uint32_t ASLEEP_AFTER_MS = 3 * 60 * 1000;
constexpr uint32_t LONG_PRESS_MS = 900;
constexpr uint32_t FORGET_WIFI_PRESS_MS = 5000;
constexpr uint32_t FRAME_MS = 33;        // ~30 frames per second
constexpr uint32_t SLEEP_FRAME_MS = 90;  // ~11 frames per second while asleep
constexpr uint32_t VOLUME_SEND_MS = 120;

enum class Phase { Idle, Listening, Thinking, Talking, Sleepy, Asleep };
Phase phase = Phase::Idle;
uint32_t phaseUntil = 0;     // when the current phase ends (0 = never)
uint32_t lastActivity = 0;   // last touch
uint32_t moodHoldUntil = 0;  // keep the current mood until then, then go neutral
bool inConversation = false;  // the helper is running a conversation
uint32_t lastVoiceMsg = 0;    // last conversation message from the helper
constexpr uint32_t CONVERSATION_TIMEOUT_MS = 60 * 1000;

ui::MediaInfo media;
String pcName;
bool gotMediaSinceConnect = false;

// Touch tracking
bool touching = false;
uint32_t touchStart = 0;
ui::Target touchTarget = ui::Target::None;
bool longPressDone = false;
int testMoodIndex = 0;

// Volume slider
uint32_t lastVolumeSent = 0;
int volumeSent = -1;
uint32_t ignorePcVolumeUntil = 0;  // don't let the PC move the slider while you drag it

// ---------- small helpers ----------

void showIdleStatus() { ui::setStatus("Tap me to talk", ui::COLOR_TEXT_DIM, false); }

// Text that stays under the face while nothing else is being said
String restingSubtitle() {
  if (pclink::wifiState() == pclink::WifiState::SetupPortal)
    return String("Set me up: on your phone, join the Wi-Fi \"") + pclink::SETUP_NETWORK + "\"";
  return "";
}

// What the media bar shows when there is no song info
void showMediaPlaceholder() {
  ui::MediaInfo m = media;
  switch (pclink::wifiState()) {
    case pclink::WifiState::Connecting:
      m.title = "Connecting to Wi-Fi...";
      m.artist = "";
      break;
    case pclink::WifiState::SetupPortal:
      m.title = "Wi-Fi setup needed";
      m.artist = String("Join \"") + pclink::SETUP_NETWORK + "\" from your phone";
      break;
    case pclink::WifiState::Online:
      if (pclink::helperConnected()) {
        m.title = "Nothing playing";
        m.artist = String("Connected to ") + pcName;
      } else {
        m.title = "PC not connected";
        m.artist = "Run Gorgi Helper on your PC";
      }
      break;
  }
  ui::setMedia(m);
}

void setPhase(Phase p, uint32_t durationMs = 0) {
  phase = p;
  phaseUntil = durationMs ? millis() + durationMs : 0;
}

bool isBusy() { return phase == Phase::Listening || phase == Phase::Thinking || phase == Phase::Talking; }
bool isSleeping() { return phase == Phase::Sleepy || phase == Phase::Asleep; }

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
  if (inConversation) return;  // don't interrupt a conversation
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

void showListening(bool followUp) {
  face.setMood(Mood::Listening);
  face.setTalking(false);
  ui::setSubtitle(followUp ? "Anything else?" : "I'm listening...");
  ui::setStatus("Listening...", ui::COLOR_LISTEN, true);
  setPhase(Phase::Listening);
}

void endConversation() {
  inConversation = false;
  face.setTalking(false);
  showIdleStatus();
  setPhase(Phase::Idle);
  moodHoldUntil = millis() + 4000;
}

// ---------- conversation messages from the helper ----------

void onVoice(const char* type, JsonObjectConst msg) {
  uint32_t now = millis();
  lastVoiceMsg = now;
  lastActivity = now;  // a conversation keeps Gorgi awake

  if (strcmp(type, "level") == 0) {
    face.setTalkLevel(msg["value"] | 0.0f, now);
    return;
  }
  if (strcmp(type, "voice") == 0) {
    const char* state = msg["state"] | "idle";
    if (strcmp(state, "listening") == 0) {
      inConversation = true;
      showListening(msg["followup"] | false);
    } else if (strcmp(state, "thinking") == 0) {
      inConversation = true;
      face.setMood(Mood::Thinking);
      face.setTalking(false);
      ui::setStatus("Thinking...", ui::COLOR_THINK, false);
      setPhase(Phase::Thinking);
    } else {  // idle
      if (inConversation) endConversation();
    }
    return;
  }
  if (strcmp(type, "heard") == 0) {
    ui::setSubtitle(String("You: ") + (const char*)(msg["text"] | ""));
    return;
  }
  if (strcmp(type, "say") == 0) {
    inConversation = true;
    Mood mood = Mood::Happy;
    moodFromName(msg["mood"] | "happy", mood);
    face.setMood(mood);
    face.setTalking(true);
    ui::setSubtitle(String((const char*)(msg["text"] | "")));
    showIdleStatus();
    setPhase(Phase::Talking);  // lasts until the helper says listening/idle
  }
}

// ---------- messages from the PC ----------

void onHelper(bool connected, const String& name) {
  gotMediaSinceConnect = false;
  if (inConversation) endConversation();
  if (connected) {
    pcName = name;
    showMediaPlaceholder();
    if (!isSleeping()) react(Mood::Happy, String("Connected to ") + name + "!");
  } else {
    ui::setClock("Gorgi");
    showMediaPlaceholder();
    if (!isSleeping()) react(Mood::Sad, "I lost my connection to the PC.");
  }
}

void onMedia(const ui::MediaInfo& m) {
  String oldTitle = media.title;
  bool first = !gotMediaSinceConnect;
  gotMediaSinceConnect = true;

  int keepVolume = media.volume;
  media = m;
  if (touchTarget == ui::Target::Volume || millis() < ignorePcVolumeUntil) media.volume = keepVolume;

  if (media.title.length() == 0) {
    showMediaPlaceholder();
  } else {
    ui::setMedia(media);
  }

  // A new song started: a little happy reaction (unless Gorgi is busy or asleep)
  if (!first && media.title.length() && media.title != oldTitle && !isBusy() && !isSleeping()) {
    react(Mood::Happy, String("Now playing: ") + media.title);
  }
}

void onClock(const String& text) { ui::setClock(text); }

void onWifi(pclink::WifiState s) {
  showMediaPlaceholder();
  switch (s) {
    case pclink::WifiState::SetupPortal:
      say(Mood::Confused, restingSubtitle());
      break;
    case pclink::WifiState::Online:
      if (!isSleeping()) react(Mood::Happy, "I'm online! Now start Gorgi Helper on your PC.");
      break;
    case pclink::WifiState::Connecting:
      break;
  }
}

// ---------- touch ----------

void sendVolume(bool force) {
  uint32_t now = millis();
  if (media.volume == volumeSent) return;
  if (!force && now - lastVolumeSent < VOLUME_SEND_MS) return;
  pclink::sendCommand("volume", media.volume);
  volumeSent = media.volume;
  lastVolumeSent = now;
}

void onTap(ui::Target target) {
  bool pc = pclink::helperConnected();
  bool mediaButton = target == ui::Target::PlayPause || target == ui::Target::Next || target == ui::Target::Prev;
  if (mediaButton && !pc) {
    react(Mood::Confused, "My PC helper isn't connected yet.");
    return;
  }

  switch (target) {
    case ui::Target::Face:
      if (!pc) {
        react(Mood::Confused, "Start Gorgi Helper on your PC so I can hear you.");
      } else if (inConversation) {
        pclink::sendSimple("cancel");
        endConversation();
        react(Mood::Neutral, "Okay, never mind.");
      } else {
        pclink::sendSimple("talk");
        inConversation = true;
        lastVoiceMsg = millis();
        showListening(false);
      }
      break;
    case ui::Target::PlayPause:
      pclink::sendCommand("playpause");
      media.playing = !media.playing;  // the PC confirms a moment later
      ui::setPlaying(media.playing);
      react(media.playing ? Mood::Happy : Mood::Neutral, media.playing ? "Music on!" : "Paused.");
      break;
    case ui::Target::Next:
      pclink::sendCommand("next");
      react(Mood::Happy, "Next song!");
      break;
    case ui::Target::Prev:
      pclink::sendCommand("prev");
      react(Mood::Happy, "Going back!");
      break;
    default:
      break;
  }
}

void onVolumeReleased() {
  ignorePcVolumeUntil = millis() + 1200;
  if (!pclink::helperConnected()) {
    react(Mood::Confused, "My PC helper isn't connected yet.");
    return;
  }
  sendVolume(true);
  int v = media.volume;
  if (v == 0)
    react(Mood::Sad, "Muted. So quiet...");
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
    Serial.printf("touch at %d,%d -> target %d\n", tp.x, tp.y, (int)touchTarget);

    bool wasAsleep = isSleeping();
    lastActivity = now;
    if (wasAsleep) {
      if (touchTarget == ui::Target::None || touchTarget == ui::Target::Clock) {
        wakeUp();
        touchTarget = ui::Target::None;  // a touch on nothing in particular just wakes Gorgi
      } else {
        // Wake up and do what was tapped (talk, next song...) in one go
        face.setMood(Mood::Neutral);
        showIdleStatus();
        setPhase(Phase::Idle);
      }
    }
  }

  if (tp.pressed && touching) {
    lastActivity = now;
    if (touchTarget == ui::Target::Volume) {
      media.volume = ui::volumeFromX(tp.x);
      ui::setVolume(media.volume);
      sendVolume(false);
    }
    if (touchTarget == ui::Target::Face && !longPressDone && now - touchStart >= LONG_PRESS_MS) {
      longPressDone = true;
      onLongPressFace();
    }
    if (touchTarget == ui::Target::Clock && !longPressDone) {
      if (now - touchStart >= FORGET_WIFI_PRESS_MS) {
        longPressDone = true;
        say(Mood::Surprised, "Forgetting Wi-Fi and restarting...");
        face.render(lcd, ui::FACE_X, ui::FACE_Y, now);
        ui::update(now);
        delay(1500);
        pclink::forgetWifi();
      } else if (now - touchStart >= 1500) {
        ui::setSubtitle("Keep holding to forget Wi-Fi...");
      }
    }
  }

  if (!tp.pressed && touching) {
    touching = false;
    ui::Target released = touchTarget;
    touchTarget = ui::Target::None;
    if (released == ui::Target::Volume)
      onVolumeReleased();
    else if (released == ui::Target::Clock && !longPressDone)
      ui::setSubtitle(restingSubtitle());
    else if (!longPressDone && now - touchStart < LONG_PRESS_MS)
      onTap(released);
  }
}

// ---------- behaviour over time ----------

// Milliseconds since `since`. Never "negative": if `since` was set a moment
// after `now` was read, the answer is 0 instead of a huge wrapped-around number.
uint32_t msSince(uint32_t now, uint32_t since) { return (int32_t)(now - since) > 0 ? now - since : 0; }

void updatePhase(uint32_t now) {
  bool timeUp = phaseUntil && now >= phaseUntil;

  // Safety net: if the helper goes quiet in the middle of a conversation
  if (inConversation && msSince(now, lastVoiceMsg) > CONVERSATION_TIMEOUT_MS) {
    Serial.println("conversation timed out");
    endConversation();
  }

  switch (phase) {
    case Phase::Listening:
    case Phase::Thinking:
      break;  // the helper moves the conversation along
    case Phase::Talking:
      if (inConversation) break;  // ends when the helper says so
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
        ui::setSubtitle(restingSubtitle());
      }
      if (msSince(now, lastActivity) > SLEEPY_AFTER_MS && pclink::wifiState() != pclink::WifiState::SetupPortal) {
        face.setMood(Mood::Sleepy);
        ui::setSubtitle("Getting sleepy...");
        setPhase(Phase::Sleepy);
      }
      break;
    case Phase::Sleepy:
      if (msSince(now, lastActivity) > ASLEEP_AFTER_MS) {
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

void drawFrame(uint32_t now) {
  face.render(lcd, ui::FACE_X, ui::FACE_Y, now);
  ui::update(now);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("Gorgi %s starting...\n", GORGI_VERSION);

  board::begin();
  lcd.init();
  lcd.fillScreen(TFT_BLACK);

  if (!psramFound()) fatal("PSRAM not found - check board settings");
  if (!display::begin(lcd)) fatal("Not enough memory for the drawing buffer");
  if (!face.begin(lcd)) fatal("Could not start the face");

  ui::begin(lcd);
  media.volume = 50;
  ui::setClock("Gorgi");
  showIdleStatus();
  randomSeed(esp_random());

  // Show something while Wi-Fi connects (can take a few seconds)
  face.setMood(Mood::Thinking);
  ui::setSubtitle("Connecting to Wi-Fi...");
  showMediaPlaceholder();
  drawFrame(millis());

  pclink::Handlers h;
  h.onMedia = onMedia;
  h.onClock = onClock;
  h.onHelper = onHelper;
  h.onWifi = onWifi;
  h.onVoice = onVoice;
  pclink::begin(h);

  lastActivity = millis();
  showMediaPlaceholder();
  if (!board::touchFound())
    say(Mood::Confused, "I can't feel my touch screen... (touch chip not found)");
  else if (pclink::wifiState() == pclink::WifiState::SetupPortal)
    say(Mood::Confused, restingSubtitle());
  else
    say(Mood::Happy, String("Hi, I'm Gorgi! (version ") + GORGI_VERSION + ") Tap me to talk.");
  Serial.println("Gorgi ready");
}

void loop() {
  static uint32_t lastFrame = 0;
  static uint32_t frames = 0, fpsTimer = 0, renderMicros = 0, renderMaxMicros = 0;
  uint32_t now = millis();

  pclink::loop();
  handleTouch(now);
  updatePhase(millis());

  // Gorgi barely moves while asleep, so it is redrawn less often (less load on the screen memory)
  const uint32_t interval = phase == Phase::Asleep ? SLEEP_FRAME_MS : FRAME_MS;
  if (now - lastFrame >= interval) {
    lastFrame = now;
    const uint32_t t0 = micros();
    drawFrame(now);
    const uint32_t spent = micros() - t0;
    renderMicros += spent;
    if (spent > renderMaxMicros) renderMaxMicros = spent;
    frames++;
  }

  if (now - fpsTimer >= 10000) {
    const display::Stats st = display::takeStats();
    Serial.printf("frames/s: %.1f  per frame: %.1f ms (max %.1f)  shapes %.0f ms, copy %.0f ms, %u px per s\n",
                  frames / ((now - fpsTimer) / 1000.0f), frames ? renderMicros / 1000.0f / frames : 0.0f,
                  renderMaxMicros / 1000.0f, st.drawUs / 10000.0f, st.pushUs / 10000.0f, st.pixels / 10);
    Serial.printf("free heap: %u  free psram: %u  wifi: %d  pc: %d\n", ESP.getFreeHeap(), ESP.getFreePsram(),
                  (int)pclink::wifiState(), (int)pclink::helperConnected());
    frames = 0;
    renderMicros = 0;
    renderMaxMicros = 0;
    fpsTimer = now;
  }
  delay(2);
}
