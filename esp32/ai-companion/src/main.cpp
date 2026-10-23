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
//   swipe sideways    -> other screens: music (swipe right), weather, time, sketch (swipe left)
//                        (on the sketch screen: swipe on the toolbar, the paper is for drawing)
//   when a timer or reminder goes off: tap anywhere to stop it

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "alarms.h"
#include "board.h"
#include "display.h"
#include "face.h"
#include "music_screen.h"
#include "pclink.h"
#include "sketch.h"
#include "sketch_store.h"
#include "time_screen.h"
#include "ui.h"
#include "weather.h"
#include "wallclock.h"
#include "weather_net.h"

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

enum class Phase { Idle, Listening, Thinking, Talking, Sleepy, Asleep, Alert };
Phase phase = Phase::Idle;
uint32_t phaseUntil = 0;     // when the current phase ends (0 = never)
uint32_t lastActivity = 0;   // last touch
uint32_t moodHoldUntil = 0;  // keep the current mood until then, then go neutral
bool inConversation = false;  // the helper is running a conversation
uint32_t lastVoiceMsg = 0;    // last conversation message from the helper
constexpr uint32_t CONVERSATION_TIMEOUT_MS = 60 * 1000;

// ---------- screens ----------
// Swipe left/right in the middle of the screen to switch, in this order.
// More screens can be added to this list later.
enum class Page { Music, Face, Weather, Time, Sketch, Count };
Page page = Page::Face;
bool musicDirty = true;      // the music screen needs drawing
int musicProgressKey = -2;   // what the progress bar showed last

constexpr int SWIPE_MIN_PX = 80;  // a sideways drag at least this long changes screens
constexpr gfx::Rect MIDDLE{0, 44, 800, 332};  // the part of the screen that changes

// Weather
weather::Data wx;
int wxCity = weather::DEFAULT_CITY;
bool wxPicker = false;
bool wxDirty = true;
String wxStatus;
uint32_t wxNextFetch = 0;  // 0 = as soon as Wi-Fi is up
bool wxFetching = false;
constexpr uint32_t WX_REFRESH_MS = 30 * 60 * 1000;
constexpr uint32_t WX_RETRY_MS = 2 * 60 * 1000;
Preferences wxPrefs;

// Time screen: timers, focus sessions, reminders
timescreen::View timeView;
bool timeDirty = true;
uint32_t timeRingsKey = 0, timeListKey = 0;
uint32_t timeVersionSeen = 0;
int timeConfirm = -1;        // reminder row asking "Delete?"
String timeNote;             // short message at the top of the Time screen
uint32_t timeNoteUntil = 0;

// Something is due: Gorgi shows it until tapped (or for 10 minutes)
bool alertOn = false;
uint32_t alertStart = 0, alertNudge = 0, alertMoodBack = 0;
Mood alertMood = Mood::Happy;
constexpr uint32_t ALERT_NUDGE_MS = 30 * 1000;
constexpr uint32_t ALERT_GIVE_UP_MS = 10 * 60 * 1000;

ui::MediaInfo media;
String pcName;
bool gotMediaSinceConnect = false;

// Touch tracking
bool touching = false;
uint32_t touchStart = 0;
ui::Target touchTarget = ui::Target::None;
bool longPressDone = false;
int touchX0 = 0, touchY0 = 0, touchEndX = 0;
bool swiping = false;  // the finger moved sideways: no tap, no long press
bool touchUsed = false;  // this touch already did its job (it stopped an alert)
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

// What the music screen says when nothing is playing
const char* musicEmptyTitle() {
  if (pclink::wifiState() != pclink::WifiState::Online) return "No Wi-Fi yet";
  return pclink::helperConnected() ? "Nothing playing" : "PC not connected";
}
const char* musicEmptyHint() {
  return pclink::helperConnected() ? "Play some music on your PC" : "Run Gorgi Helper on your PC";
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
  if (inConversation || alertOn) return;  // don't interrupt a conversation or an alert
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

// ---------- screens ----------

// The screen's content will be drawn again in full on the next frame
void markPageDirty(Page p) {
  if (p == Page::Face) face.invalidate();
  if (p == Page::Weather) wxDirty = true;
  if (p == Page::Music) musicDirty = true;
  if (p == Page::Time) timeDirty = true;
  if (p == Page::Sketch) sketchFull = true;
}

void drawFrame(uint32_t now);
void sendTimeList();
void sketchShown();
void sketchHidden();
bool sketchFull = true;  // the sketch screen needs drawing in full

// Switches screens right away (no animation)
void showPage(Page p) {
  if (p == page) return;
  if (page == Page::Sketch) sketchHidden();
  page = p;
  ui::setPagePosition((float)(int)p);
  ui::setSubtitleVisible(p == Page::Face);
  if (p == Page::Face) {
    // clear around the face (the face and subtitle draw their own background)
    display::render(gfx::Rect{0, 44, ui::FACE_X, 274}, ui::BACKGROUND, [](gfx::Canvas&) {});
    display::render(gfx::Rect{ui::FACE_X + 600, 44, 800 - ui::FACE_X - 600, 274}, ui::BACKGROUND,
                    [](gfx::Canvas&) {});
  } else if (p == Page::Weather) {
    wxPicker = false;
  } else if (p == Page::Time) {
    timeConfirm = -1;
  } else if (p == Page::Sketch) {
    sketchShown();
  }
  markPageDirty(p);
  drawFrame(millis());  // the new screen right away, in one go
}


// Gives the forecast to the helper, so Gorgi can talk about it
void sendWeather() {
  if (!wx.valid) return;
  JsonDocument doc;
  doc["type"] = "weather";
  doc["city"] = weather::CITIES[wx.city].name;
  JsonObject now = doc["now"].to<JsonObject>();
  now["temp"] = roundf(wx.temp * 10) / 10;
  now["feels"] = roundf(wx.feels * 10) / 10;
  now["humidity"] = wx.humidity;
  now["wind"] = roundf(wx.wind);
  now["sky"] = weather::describe(wx.code);
  now["time"] = wx.updated;
  JsonArray days = doc["days"].to<JsonArray>();
  for (int i = 0; i < wx.dayCount; ++i) {
    JsonObject d = days.add<JsonObject>();
    d["day"] = wx.days[i].name;
    d["sky"] = weather::describe(wx.days[i].code);
    d["hi"] = wx.days[i].hi;
    d["lo"] = wx.days[i].lo;
    d["rain"] = wx.days[i].rain;
  }
  pclink::send(doc);
}

void fetchWeather() {
  weather::request(wxCity);
  wxFetching = true;
  if (!wx.valid || wx.city != wxCity) {
    wxStatus = "Getting the weather...";
    wxDirty = true;
  }
}

void updateWeather(uint32_t now) {
  if (pclink::wifiState() != pclink::WifiState::Online) {
    if (!wx.valid && wxStatus != "Waiting for Wi-Fi...") {
      wxStatus = "Waiting for Wi-Fi...";
      wxDirty = true;
    }
    return;
  }
  if (!wxFetching && (wxNextFetch == 0 || (int32_t)(now - wxNextFetch) >= 0)) fetchWeather();

  bool ok = false;
  String error;
  if (weather::takeResult(wx, ok, error)) {
    wxFetching = false;
    if (ok && wx.city != wxCity) {  // the city changed while it was downloading
      fetchWeather();
      return;
    }
    wxNextFetch = now + (ok ? WX_REFRESH_MS : WX_RETRY_MS);
    if (!ok) wxStatus = "Can't get the weather right now (" + error + ")";
    wxDirty = true;
    if (ok) sendWeather();
  }
}

void onWeatherTap(int x, int y) {
  int city = wxCity;
  switch (weather::tap(x, y, wxPicker, city)) {
    case weather::Tap::OpenPicker:
      wxPicker = true;
      wxDirty = true;
      break;
    case weather::Tap::ClosePicker:
      wxPicker = false;
      wxDirty = true;
      break;
    case weather::Tap::PickCity:
      wxPicker = false;
      wxDirty = true;
      if (city != wxCity) {
        wxCity = city;
        wxPrefs.putInt("city", city);
        if (!wxFetching) fetchWeather();  // otherwise it fetches again when the current one ends
        else if (!wx.valid || wx.city != wxCity) wxStatus = "Getting the weather...";
      }
      break;
    case weather::Tap::None:
      break;
  }
}

// ---------- firmware update over Wi-Fi ----------

bool updating = false;

void drawFrame(uint32_t now);

// While new firmware is written to flash, the chip can't read the screen's
// picture memory, so the picture jumps around. So Gorgi says goodbye, then
// turns the screen off; it comes back on by itself after the restart.
// (The progress bar is shown on the PC.)
void onUpdate(int percent, const String& message) {
  if (percent < 0) {  // failed: back to normal
    updating = false;
    board::setBacklight(true);
    face.invalidate();
    face.setMood(Mood::Sad);
    ui::setSubtitle(message);
    showIdleStatus();
    setPhase(Phase::Idle);
    moodHoldUntil = millis() + 6000;
    return;
  }
  if (!updating) {
    updating = true;
    if (inConversation) endConversation();
    showPage(Page::Face);
    face.setMood(Mood::Happy);
    face.setTalking(false);
    ui::setStatus("Updating", ui::COLOR_THINK, false);
    ui::setSubtitle(message);
    const uint32_t until = millis() + 600;
    while ((int32_t)(millis() - until) < 0) {  // let the face settle so the message is seen
      drawFrame(millis());
      delay(30);
    }
    delay(1200);
    board::setBacklight(false);
  }
}

// ---------- conversation messages from the helper ----------

void onVoice(const char* type, JsonObjectConst msg) {
  uint32_t now = millis();
  lastVoiceMsg = now;
  lastActivity = now;  // a conversation keeps Gorgi awake
  if (page != Page::Face && strcmp(type, "level") != 0) showPage(Page::Face);
  if (alertOn && strcmp(type, "level") != 0) alertOn = false;  // a conversation takes over

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
    sendWeather();
    sendTimeList();
    showMediaPlaceholder();
    if (!isSleeping()) react(Mood::Happy, String("Connected to ") + name + "!");
  } else {
    if (!wallclock::valid()) ui::setClock("Gorgi");
    showMediaPlaceholder();
    if (!isSleeping()) react(Mood::Sad, "I lost my connection to the PC.");
  }
}

void onMedia(const ui::MediaInfo& m) {
  String oldTitle = media.title;
  bool first = !gotMediaSinceConnect;
  gotMediaSinceConnect = true;

  int keepVolume = media.volume;
  if (m.title != media.title || m.artist != media.artist || m.album != media.album || m.playing != media.playing ||
      m.artId != media.artId)
    musicDirty = true;
  media = m;
  if (touchTarget == ui::Target::Volume || millis() < ignorePcVolumeUntil) media.volume = keepVolume;

  if (media.title.length() == 0) {
    showMediaPlaceholder();
  } else {
    ui::setMedia(media);
  }

  // A new song started: a little happy reaction (unless Gorgi is busy or asleep)
  if (!first && media.title.length() && media.title != oldTitle && !isBusy() && !isSleeping() && !alertOn &&
      !alarms::focusRunning()) {
    react(Mood::Happy, String("Now playing: ") + media.title);
  }
}

// The PC's time: only used until Gorgi gets the time from the internet
void onClock(const String& text, uint32_t epoch) {
  if (epoch) wallclock::setFromPC((time_t)epoch);
  if (!wallclock::valid()) ui::setClock(text);
}

void onArt(uint32_t id, uint32_t total, uint32_t offset, const uint8_t* data, size_t len) {
  if (music::artChunk(id, total, offset, data, len) && id == media.artId) musicDirty = true;
}

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

// ---------- time: timers, focus sessions, reminders ----------

uint32_t msSince(uint32_t now, uint32_t since);
void onTap(ui::Target target);

void showTimeNote(const String& text) {
  timeNote = text;
  timeNoteUntil = millis() + 3000;
  timeDirty = true;
}

// Tells the helper what's set, so the AI knows (and can cancel things)
void sendTimeList() {
  JsonDocument doc;
  alarms::toJson(doc, millis());
  pclink::send(doc);
}

const char* dueKindName(alarms::Due::Kind k) {
  switch (k) {
    case alarms::Due::Timer: return "timer";
    case alarms::Due::Focus: return "focus";
    case alarms::Due::Reminder: return "reminder";
    default: return "missed";
  }
}

void showAlert(const alarms::Due& d) {
  const uint32_t now = millis();
  alertOn = true;
  alertStart = alertNudge = now;
  alertMoodBack = now + 2000;
  alertMood = d.kind == alarms::Due::Focus ? Mood::Excited : (d.kind == alarms::Due::Missed ? Mood::Sad : Mood::Happy);
  lastActivity = now;  // wakes Gorgi up
  moodHoldUntil = 0;
  showPage(Page::Face);
  face.setTalking(false);
  face.setMood(d.kind == alarms::Due::Missed ? Mood::Sad : Mood::Surprised);
  ui::setSubtitle(d.text);
  ui::setStatus("Tap to stop", ui::COLOR_ACCENT, true);
  setPhase(Phase::Alert);
  // the PC says it out loud
  JsonDocument doc;
  doc["type"] = "due";
  doc["kind"] = dueKindName(d.kind);
  doc["text"] = d.text;
  pclink::send(doc);
}

void endAlert(bool tapped) {
  alertOn = false;
  face.setTalking(false);
  showIdleStatus();
  setPhase(Phase::Idle);
  if (tapped) {
    face.setMood(Mood::Happy);
    ui::setSubtitle("Okay!");
    moodHoldUntil = millis() + 2000;
  } else {
    face.setMood(Mood::Neutral);
    ui::setSubtitle(restingSubtitle());
  }
}

void updateAlert(uint32_t now) {
  if (msSince(now, alertStart) > ALERT_GIVE_UP_MS) {
    endAlert(false);
    return;
  }
  if (alertMoodBack && (int32_t)(now - alertMoodBack) >= 0) {
    alertMoodBack = 0;
    face.setMood(alertMood);
  }
  if (msSince(now, alertNudge) > ALERT_NUDGE_MS) {  // a little jump every 30 s until tapped
    alertNudge = now;
    face.setMood(Mood::Surprised);
    alertMoodBack = now + 1500;
  }
  lastActivity = now;  // don't fall asleep while something is due
}

void updateTime(uint32_t now) {
  alarms::loop(now);

  // the clock in the top bar (Gorgi's own, once it knows the time)
  char hm[8];
  wallclock::hhmm(hm, sizeof hm);
  if (hm[0]) ui::setClock(hm);

  // countdown next to the clock
  uint32_t left = 0;
  bool focus = false;
  if (alarms::soonest(now, left, focus)) {
    char buf[16];
    timescreen::formatLeft(left, buf, sizeof buf);
    ui::setTimerChip(buf, focus);
  } else {
    ui::setTimerChip("", false);
  }

  if (alarms::version() != timeVersionSeen) {
    timeVersionSeen = alarms::version();
    timeDirty = true;
    timeConfirm = -1;
    sendTimeList();
  }
  if (timeNoteUntil && (int32_t)(now - timeNoteUntil) >= 0) {
    timeNoteUntil = 0;
    timeNote = "";
    timeDirty = true;
  }

  // something due: show it (after a conversation, not in the middle of one)
  if (!alertOn && !inConversation && !updating && !touching) {
    alarms::Due d;
    if (alarms::takeDue(d)) showAlert(d);
  }
}

void onTimeTap(int x, int y) {
  alarms::fillView(timeView, millis());
  timeView.confirmRow = timeConfirm;
  const timescreen::TapResult t = timescreen::tap(x, y, timeView);
  switch (t.what) {
    case timescreen::Tap::Ring:
      alarms::togglePause(timeView.rings[t.index].id);
      break;
    case timescreen::Tap::RingClose:
      alarms::cancel(timeView.rings[t.index].id);
      break;
    case timescreen::Tap::QuickTimer:
      if (alarms::addTimer(t.index * 60, nullptr) == alarms::Result::Full)
        showTimeNote(String("I can only run ") + alarms::MAX_TIMERS + " timers at once");
      break;
    case timescreen::Tap::FocusLength:
      alarms::nextFocusLength();
      break;
    case timescreen::Tap::FocusStart:
      if (alarms::focusRunning())
        showTimeNote("We're already focusing!");
      else
        alarms::startFocus(alarms::focusMinutes() * 60);
      break;
    case timescreen::Tap::Row:
      timeConfirm = t.index;
      timeDirty = true;
      break;
    case timescreen::Tap::Delete:
      timeConfirm = -1;
      timeDirty = true;
      alarms::cancel(timeView.rows[t.index].id);
      break;
    case timescreen::Tap::Keep:
      timeConfirm = -1;
      timeDirty = true;
      break;
    case timescreen::Tap::AskMe:  // talk right from here: back to the face, listening
      showPage(Page::Face);
      if (!inConversation) onTap(ui::Target::Face);
      break;
    case timescreen::Tap::None:
      break;
  }
}

void drawTime(uint32_t now) {
  alarms::fillView(timeView, now);
  timeView.confirmRow = timeConfirm;
  timeView.note = timeNoteUntil ? timeNote.c_str() : "";
  uint32_t rings = timeView.ringCount;
  for (int i = 0; i < timeView.ringCount; ++i)
    rings = rings * 31 + timeView.rings[i].left * 2 + (timeView.rings[i].paused ? 1 : 0);
  const uint32_t list = wallclock::valid() ? (uint32_t)(wallclock::now() / 60) : 1;
  auto draw = [](gfx::Canvas& c) { timescreen::draw(c, timeView); };
  if (timeDirty) {
    display::render(timescreen::AREA, ui::BACKGROUND, draw);
  } else {
    if (rings != timeRingsKey) display::render(timescreen::RINGS_AREA, ui::BACKGROUND, draw);
    if (list != timeListKey) display::render(timescreen::LIST_AREA, ui::BACKGROUND, draw);
  }
  timeDirty = false;
  timeRingsKey = rings;
  timeListKey = list;
}

// Timers and reminders set by voice (the helper turns the AI's tags into these)
void onTime(const char* type, JsonObjectConst msg) {
  if (strcmp(type, "time_cancel") == 0) {
    alarms::cancel(msg["id"] | 0u);
    return;
  }
  const char* kind = msg["kind"] | "";
  const char* text = msg["text"] | "";
  alarms::Result r = alarms::Result::Bad;
  if (strcmp(kind, "timer") == 0) {
    r = alarms::addTimer(msg["seconds"] | 0u, msg["label"] | "");
  } else if (strcmp(kind, "focus") == 0) {
    r = alarms::startFocus(msg["seconds"] | (uint32_t)alarms::focusMinutes() * 60);
  } else if (strcmp(kind, "remind") == 0) {
    if (msg["at"].is<uint32_t>())
      r = alarms::remindAt((time_t)(msg["at"] | 0u), text);
    else
      r = alarms::remindRepeat(msg["minute"] | -1, (uint8_t)(msg["days"] | 0), text);
  }
  if (r != alarms::Result::Ok) {
    Serial.printf("time: couldn't add %s (%d)\n", kind, (int)r);
    sendTimeList();  // so the helper sees nothing changed
  }
}

// ---------- sketch pad ----------

uint16_t* skPic = nullptr;  // the drawing (PSRAM)
constexpr int UNDO_STEPS = 5;
uint16_t* skUndo[UNDO_STEPS] = {};
int skUndoSlots = 0;          // how many undo pictures could be made
int skUndoCount = 0, skUndoNext = 0;
uint16_t* skThumbs = nullptr;  // gallery pictures
sketch::Tools skTools;
sketch::Gallery skGallery;
Preferences skPrefs;
uint32_t skCurrent = 1, skNextId = 2;
bool skLoaded = false;     // the current sketch was read from flash
bool skChanged = false;    // drawn on since the last save
bool skToolbarDirty = false;
uint32_t skLastStroke = 0;
bool sketchStroking = false;
float skLastX = 0, skLastY = 0;
constexpr uint32_t SKETCH_SAVE_AFTER_MS = 20 * 1000;  // save after a pause in drawing
constexpr size_t SKETCH_PIXELS = (size_t)sketch::PAPER_W * sketch::PAPER_H;

void sketchMove(int x, int y);

void sketchSetup() {
  skPic = (uint16_t*)heap_caps_malloc(SKETCH_PIXELS * 2, MALLOC_CAP_SPIRAM);
  skThumbs = (uint16_t*)heap_caps_malloc((size_t)sketch::MAX_SKETCHES * sketch::THUMB_W * sketch::THUMB_H * 2,
                                         MALLOC_CAP_SPIRAM);
  for (int i = 0; i < UNDO_STEPS; ++i) {
    skUndo[i] = (uint16_t*)heap_caps_malloc(SKETCH_PIXELS * 2, MALLOC_CAP_SPIRAM);
    if (!skUndo[i]) break;
    skUndoSlots = i + 1;
  }
  skPrefs.begin("sketch", false);
  skCurrent = skPrefs.getUInt("cur", 1);
  skNextId = skPrefs.getUInt("next", skCurrent + 1);
  sketchstore::begin();
  Serial.printf("sketch: ready, %d undo steps, free psram %u\n", skUndoSlots, ESP.getFreePsram());
}

void sketchClear() {
  if (!skPic) return;
  for (size_t i = 0; i < SKETCH_PIXELS; ++i) skPic[i] = sketch::PAPER_COLOR.c;
}

void sketchForgetUndo() {
  skUndoCount = 0;
  skTools.canUndo = false;
}

void sketchSave() {
  if (!skChanged || !skPic) return;
  skChanged = false;
  sketchstore::save(skCurrent, skPic);
}

void sketchLoadCurrent() {
  skLoaded = true;
  if (!skPic) return;
  if (!sketchstore::load(skCurrent, skPic)) sketchClear();
  sketchForgetUndo();
}

void sketchStartNew() {
  sketchSave();
  skCurrent = skNextId++;
  skPrefs.putUInt("cur", skCurrent);
  skPrefs.putUInt("next", skNextId);
  sketchClear();
  sketchForgetUndo();
}

void sketchShown() {
  if (!skLoaded) sketchLoadCurrent();
  skTools.gallery = false;
  sketchFull = true;
}

void sketchHidden() {
  sketchStroking = false;
  skTools.gallery = false;
  sketchSave();
}

void sketchFillGallery() {
  uint32_t ids[sketch::MAX_SKETCHES];
  skGallery.count = sketchstore::list(ids, sketch::MAX_SKETCHES);
  skGallery.current = skCurrent;
  skGallery.confirm = -1;
  for (int i = 0; i < skGallery.count; ++i) {
    uint16_t* thumb = skThumbs ? skThumbs + (size_t)i * sketch::THUMB_W * sketch::THUMB_H : nullptr;
    if (thumb && !sketchstore::loadThumb(ids[i], thumb)) thumb = nullptr;
    skGallery.items[i] = sketch::GalleryItem{ids[i], thumb};
  }
}

void sketchRenderRect(const gfx::Rect& r) {
  display::render(r, sketch::PAPER_COLOR, [](gfx::Canvas& c) { sketch::drawPaper(c, skPic); });
}

// Finger down on the paper: start a stroke (a dot so far)
void sketchBegin(int x, int y) {
  if (skTools.gallery || !skPic || x < sketch::PAPER_X || y < sketch::PAPER_Y ||
      x >= sketch::PAPER_X + sketch::PAPER_W || y >= sketch::PAPER_Y + sketch::PAPER_H)
    return;
  if (skUndoSlots) {  // remember the picture before this stroke
    memcpy(skUndo[skUndoNext], skPic, SKETCH_PIXELS * 2);
    skUndoNext = (skUndoNext + 1) % skUndoSlots;
    if (skUndoCount < skUndoSlots) ++skUndoCount;
    if (!skTools.canUndo) skToolbarDirty = true;
    skTools.canUndo = true;
  }
  sketchStroking = true;
  skLastX = x;
  skLastY = y;
  sketchMove(x, y);
}

void sketchMove(int x, int y) {
  const float dx = x - skLastX, dy = y - skLastY;
  if (dx * dx + dy * dy < 4 && skChanged && (x != skLastX || y != skLastY)) return;  // wait for a bigger move
  const gfx::Color& col = skTools.eraser ? sketch::PAPER_COLOR : sketch::COLORS[skTools.color];
  const float r = sketch::SIZES[skTools.size] * (skTools.eraser ? 2.5f : 1.0f);
  sketchRenderRect(sketch::stroke(skPic, skLastX, skLastY, x, y, col, r));
  skLastX = x;
  skLastY = y;
  skChanged = true;
}

void sketchEnd(uint32_t now) {
  sketchStroking = false;
  skLastStroke = now;
}

void sketchUndo() {
  if (!skUndoCount) return;
  skUndoNext = (skUndoNext + skUndoSlots - 1) % skUndoSlots;
  memcpy(skPic, skUndo[skUndoNext], SKETCH_PIXELS * 2);
  --skUndoCount;
  skTools.canUndo = skUndoCount > 0;
  skChanged = true;
  skLastStroke = millis();
  sketchFull = true;
}

void onSketchTap(int x, int y) {
  if (x < sketch::TOOLBAR_RIGHT) {
    const sketch::ToolTap t = sketch::toolbarTap(x, y);
    switch (t.what) {
      case sketch::Tool::Color:
        skTools.color = t.index;
        skTools.eraser = false;
        break;
      case sketch::Tool::Eraser:
        skTools.eraser = !skTools.eraser;
        break;
      case sketch::Tool::Size:
        skTools.size = (skTools.size + 1) % sketch::SIZE_COUNT;
        break;
      case sketch::Tool::Undo:
        if (!skTools.gallery) sketchUndo();
        break;
      case sketch::Tool::Gallery:
        skTools.gallery = !skTools.gallery;
        if (skTools.gallery) {
          sketchSave();
          sketchFillGallery();
        }
        sketchFull = true;
        break;
      case sketch::Tool::None:
        return;
    }
    if (skTools.gallery && t.what != sketch::Tool::Gallery) {  // picking a colour closes the gallery
      skTools.gallery = false;
      sketchFull = true;
    }
    skToolbarDirty = true;
    return;
  }
  if (!skTools.gallery) return;
  const sketch::GalleryTap g = sketch::galleryTap(x, y, skGallery);
  switch (g.what) {
    case sketch::GTap::New:
      sketchStartNew();
      skTools.gallery = false;
      break;
    case sketch::GTap::Open:
      sketchSave();
      skCurrent = skGallery.items[g.index].id;
      skPrefs.putUInt("cur", skCurrent);
      sketchLoadCurrent();
      skTools.gallery = false;
      break;
    case sketch::GTap::AskDelete:
      skGallery.confirm = g.index;
      break;
    case sketch::GTap::Delete: {
      const uint32_t id = skGallery.items[g.index].id;
      sketchstore::remove(id);
      if (id == skCurrent) {  // the one on the paper: start a fresh page
        skChanged = false;
        sketchStartNew();
      }
      sketchFillGallery();
      break;
    }
    case sketch::GTap::Keep:
      skGallery.confirm = -1;
      break;
    case sketch::GTap::Close:
      skTools.gallery = false;
      break;
    case sketch::GTap::None:
      return;
  }
  sketchFull = true;
}

void drawSketch() {
  if (sketchFull) {
    sketchFull = skToolbarDirty = false;
    display::render(sketch::AREA, ui::BACKGROUND, [](gfx::Canvas& c) {
      sketch::drawToolbar(c, skTools);
      if (skTools.gallery) sketch::drawGallery(c, skGallery);
      else sketch::drawPaper(c, skPic);
    });
  } else if (skToolbarDirty) {
    skToolbarDirty = false;
    display::render(sketch::TOOLBAR, ui::BACKGROUND, [](gfx::Canvas& c) { sketch::drawToolbar(c, skTools); });
  }
}

// Saves after a pause in drawing (writing to flash can make the screen jump
// for a moment, so not while the finger is drawing)
void updateSketch(uint32_t now) {
  if (skChanged && !touching && msSince(now, skLastStroke) > SKETCH_SAVE_AFTER_MS) sketchSave();
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

  if (tp.pressed && !touching && alertOn) {  // any touch stops what's going off
    touching = true;
    touchUsed = true;
    touchTarget = ui::Target::None;
    lastActivity = now;
    endAlert(true);
    return;
  }
  if (touchUsed) {  // wait for the finger to lift
    if (!tp.pressed) {
      touching = false;
      touchUsed = false;
    }
    return;
  }

  if (tp.pressed && !touching) {
    touching = true;
    touchStart = now;
    longPressDone = false;
    touchTarget = ui::hitTest(tp.x, tp.y);
    touchX0 = tp.x;
    touchY0 = tp.y;
    swiping = false;
    ui::showPressed(touchTarget);
    Serial.printf("touch at %d,%d -> target %d\n", tp.x, tp.y, (int)touchTarget);

    if (page == Page::Sketch && touchTarget == ui::Target::Face) sketchBegin(tp.x, tp.y);

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
    if (sketchStroking) {  // drawing with the finger
      sketchMove(tp.x, tp.y);
      return;
    }
    const int dx = tp.x - touchX0;
    // A sideways drag in the middle of the screen moves the screens
    if (touchTarget == ui::Target::Face && !swiping && !longPressDone && abs(dx) > 16 &&
        abs(dx) * 5 > abs(tp.y - touchY0) * 6 && !(page == Page::Sketch && touchX0 >= sketch::TOOLBAR_RIGHT)) {
      swiping = true;
    }
    if (swiping) {
      touchEndX = tp.x;
      return;
    }
    if (touchTarget == ui::Target::Volume) {
      media.volume = ui::volumeFromX(tp.x);
      ui::setVolume(media.volume);
      sendVolume(false);
    }
    if (touchTarget == ui::Target::Face && page == Page::Face && !longPressDone &&
        now - touchStart >= LONG_PRESS_MS) {
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
    if (sketchStroking) {
      sketchEnd(now);
      return;
    }
    if (swiping) {
      swiping = false;
      // A long enough sideways swipe changes screens
      const int dx = touchEndX - touchX0;
      const int next = (int)page + (dx < 0 ? 1 : -1);  // swipe left -> the next screen
      if (abs(dx) >= SWIPE_MIN_PX && next >= 0 && next < (int)Page::Count) showPage((Page)next);
      return;
    }
    if (released == ui::Target::Face && page != Page::Face) {
      if (!longPressDone && page == Page::Weather) onWeatherTap(touchX0, touchY0);
      if (!longPressDone && page == Page::Time) onTimeTap(touchX0, touchY0);
      if (page == Page::Sketch) onSketchTap(touchX0, touchY0);
      return;
    }
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
    case Phase::Alert:
      updateAlert(now);
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
  if (page == Page::Face) {
    face.render(lcd, ui::FACE_X, ui::FACE_Y, now);
  } else if (page == Page::Weather) {
    if (wxDirty) {
      wxDirty = false;
      display::render(weather::AREA, ui::BACKGROUND,
                      [](gfx::Canvas& c) { weather::draw(c, wx, wxCity, wxPicker, wxStatus.c_str()); });
    }
  } else if (page == Page::Time) {
    drawTime(now);
  } else if (page == Page::Sketch) {
    drawSketch();
  } else if (page == Page::Music) {
    const int key = music::progressKey(media, now);
    if (musicDirty) {
      display::render(MIDDLE, ui::BACKGROUND,
                      [&](gfx::Canvas& c) { music::draw(c, media, now, musicEmptyTitle(), musicEmptyHint()); });
    } else if (key != musicProgressKey) {
      display::render(music::PROGRESS_AREA, ui::BACKGROUND,
                      [&](gfx::Canvas& c) { music::draw(c, media, now, musicEmptyTitle(), musicEmptyHint()); });
    }
    musicDirty = false;
    musicProgressKey = key;
  }
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
  if (!music::begin()) fatal("Not enough memory for the music screen");

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
  h.onUpdate = onUpdate;
  h.onArt = onArt;
  h.onTime = onTime;
  pclink::begin(h);
  wallclock::begin();
  alarms::begin();
  sketchSetup();

  wxPrefs.begin("weather", false);
  wxCity = constrain(wxPrefs.getInt("city", weather::DEFAULT_CITY), 0, weather::CITY_COUNT - 1);
  ui::setPages((int)Page::Count, (float)(int)page);
  weather::beginFetching();

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
  updateWeather(millis());
  updateTime(millis());
  updateSketch(millis());

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
