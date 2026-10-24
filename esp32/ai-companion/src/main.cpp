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
//   swipe sideways    -> other screens: music (swipe right), time, to-do, Claude (swipe left)
//                        (or tap the icons at the top)
//   when a timer or reminder goes off: tap anywhere to stop it

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "alarms.h"
#include "board.h"
#include "claude_screen.h"
#include "display.h"
#include "face.h"
#include "music_screen.h"
#include "action_center.h"
#include "pclink.h"
#include <LittleFS.h>

#include "tabs.h"
#include "time_screen.h"
#include "todo_screen.h"
#include "todos.h"
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
enum class Page { Music, Face, Time, Todo, Claude, Count };  // the weather moved to the Action Center in 4.3
const uint8_t TAB_ICONS[(int)Page::Count] = {tabs::MUSIC, tabs::FACE, tabs::TIME, tabs::TODO, tabs::CLAUDE};
Page page = Page::Face;
bool musicDirty = true;      // the music screen needs drawing
int musicProgressKey = -2;   // what the progress bar showed last

constexpr int SWIPE_MIN_PX = 80;  // a sideways drag at least this long changes screens
constexpr gfx::Rect MIDDLE{0, 44, 800, 332};  // the part of the screen that changes

// Weather
weather::Data wx;
int wxCity = weather::DEFAULT_CITY;
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

// Claude Code on the PC (sessions and usage, sent by the helper)
claudescreen::View claudeView;
int32_t claudeSince[claudescreen::MAX_SESSIONS];  // when each session's state changed (seconds since 1970)
int32_t claudeUsageAt = 0;
bool claudeDirty = true;
uint32_t claudeDrawnAt = 0;
bool alertClaude = false;  // the alert on screen is from Claude Code
String alertText, alertTitle;  // for a notification if nobody taps it
uint8_t alertNotice = 0;
String alertSession;       // ...for this session

// A Teams call is going on (the helper notices the microphone)
bool callOn = false;

// The Action Center (pull down from the top bar): a full screen with notifications, weather, Sleep
bool acOpen = false, acDirty = false;
actioncenter::View acView;
uint32_t acMinute = 0;      // the clock on it changes every minute
char updateBusyText[48] = "";
// Notifications (newest first): an update is ready, alerts nobody tapped...
actioncenter::Notice notices[8];
int noticeCount = 0;
int touchEndY = 0;
bool screenOff = false;        // put to sleep from the Action Center: backlight off until touched
bool sleepAfterAlert = false;  // an alert woke the screen: back to sleep once it's dismissed
bool updateAvailable = false, updateBusy = false;
String updateVersion;

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

const gfx::Color CALL_RED = gfx::rgb(0xFF6B6B);
void showIdleStatus() {
  // Nothing when idle: on the face you tap the face; screens without one have a Talk button
  if (callOn) ui::setStatus("On a call", CALL_RED, false);
  else ui::setStatus("", ui::COLOR_TEXT_DIM, false);
}

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
  if (p == Page::Music) musicDirty = true;
  if (p == Page::Time) timeDirty = true;
  if (p == Page::Todo) todoFull = true;
  if (p == Page::Claude) claudeDirty = true;
}

void drawFrame(uint32_t now);
void addNotice(uint8_t kind, const char* title, const char* text, const char* action);
void addMissed(uint8_t kind, const char* title, const String& what);
void closeActionCenter();
void goToSleep();
void sendTimeList();
void todoShown();
void todoHidden();
void todoVoiceReset();
void sendTodoList();
void drawClaude(uint32_t now);

// Switches screens right away (no animation)
void showPage(Page p) {
  if (p == page) return;
  if (page == Page::Todo) todoHidden();
  page = p;
  ui::setTab((int)p);
  ui::setSubtitleVisible(p == Page::Face);
  ui::setBottomHidden(p == Page::Todo);  // the To-do screen uses the whole height
  if (p == Page::Face) {
    // clear around the face (the face and subtitle draw their own background)
    display::render(gfx::Rect{0, ui::TOP_H, 800, ui::FACE_Y - ui::TOP_H}, ui::BACKGROUND, [](gfx::Canvas&) {});
    display::render(gfx::Rect{0, ui::FACE_Y, ui::FACE_X, 330 - ui::FACE_Y}, ui::BACKGROUND, [](gfx::Canvas&) {});
    display::render(gfx::Rect{ui::FACE_X + 600, ui::FACE_Y, 800 - ui::FACE_X - 600, 330 - ui::FACE_Y},
                    ui::BACKGROUND, [](gfx::Canvas&) {});
  } else {
    // the subtitle reached a little lower than the other screens do
    display::render(gfx::Rect{0, 376, 800, 12}, ui::BACKGROUND, [](gfx::Canvas&) {});
  }
  if (p == Page::Time) {
    timeConfirm = -1;
  } else if (p == Page::Todo) {
    todoShown();
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
    acDirty = true;  // the Action Center shows the weather
  }
}

void updateWeather(uint32_t now) {
  if (pclink::wifiState() != pclink::WifiState::Online) {
    if (!wx.valid && wxStatus != "Waiting for Wi-Fi...") {
      wxStatus = "Waiting for Wi-Fi...";
      acDirty = true;  // the Action Center shows the weather
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
    acDirty = true;  // the Action Center shows the weather
    if (ok) sendWeather();
  }
}

void setWeatherCity(int city) {
  if (city == wxCity) return;
  wxCity = city;
  wxPrefs.putInt("city", city);
  if (!wxFetching) fetchWeather();  // otherwise it fetches again when the current one ends
  else if (!wx.valid || wx.city != wxCity) wxStatus = "Getting the weather...";
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
    if (acOpen) closeActionCenter();
    if (screenOff) {
      screenOff = false;
      board::setBacklight(true);
    }
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
  todoVoiceReset();  // (voice typing ended with the connection)
  if (inConversation) endConversation();
  if (connected) {
    pcName = name;
    sendWeather();
    sendTimeList();
    sendTodoList();
    showMediaPlaceholder();
    if (!isSleeping()) react(Mood::Happy, String("Connected to ") + name + "!");
  } else {
    if (!wallclock::valid()) ui::setClock("Gorgi");
    callOn = false;  // without the PC there's no call to show
    ui::setCallButton(false);
    showIdleStatus();
    claudeView.count = 0;
    claudeDirty = true;
    ui::setTabBadges(0);
    showMediaPlaceholder();
    if (updateBusy) react(Mood::Happy, "The PC helper is updating. Back in a moment!");
    else if (!isSleeping()) react(Mood::Sad, "I lost my connection to the PC.");
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
      !alarms::focusRunning() && !callOn) {
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
  // (drawTime notices what changed)
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

// An alert while the screen is off: turn it on, and back off once it's dismissed
void alertWakesScreen() {
  if (acOpen) closeActionCenter();
  if (!screenOff) return;
  screenOff = false;
  sleepAfterAlert = true;
  board::setBacklight(true);
}

void showAlert(const alarms::Due& d) {
  alertWakesScreen();
  const uint32_t now = millis();
  alertOn = true;
  alertStart = alertNudge = now;
  alertMoodBack = now + 2000;
  alertClaude = false;
  alertMood = d.kind == alarms::Due::Focus ? Mood::Excited : (d.kind == alarms::Due::Missed ? Mood::Sad : Mood::Happy);
  alertText = d.text;
  alertNotice = actioncenter::MISSED;
  alertTitle = d.kind == alarms::Due::Timer      ? "Timer finished"
               : d.kind == alarms::Due::Focus    ? "Focus session done"
               : d.kind == alarms::Due::Reminder ? "Missed reminder"
                                                 : "Missed while Gorgi was off";
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
  alertClaude = false;
  if (sleepAfterAlert) {  // it was asleep before: back to sleep
    sleepAfterAlert = false;
    goToSleep();
    return;
  }
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
  if (msSince(now, alertStart) > ALERT_GIVE_UP_MS) {  // nobody tapped it: keep it as a notification
    const String text = alertText, title = alertTitle;
    const uint8_t kind = alertNotice;
    endAlert(false);
    addMissed(kind, title.c_str(), text);
    return;
  }
  if (alertMoodBack && (int32_t)(now - alertMoodBack) >= 0) {
    alertMoodBack = 0;
    face.setMood(alertMood);
  }
  if (!alertClaude && msSince(now, alertNudge) > ALERT_NUDGE_MS) {  // a little jump every 30 s until tapped
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
    // (drawTime notices what changed)
    timeConfirm = -1;
    sendTimeList();
  }
  if (timeNoteUntil && (int32_t)(now - timeNoteUntil) >= 0) {
    timeNoteUntil = 0;
    timeNote = "";
    // (drawTime notices what changed)
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
      // (drawTime notices what changed)
      break;
    case timescreen::Tap::Delete:
      timeConfirm = -1;
      // (drawTime notices what changed)
      alarms::cancel(timeView.rows[t.index].id);
      break;
    case timescreen::Tap::Keep:
      timeConfirm = -1;
      // (drawTime notices what changed)
      break;
    case timescreen::Tap::AskMe:  // talk right from here: back to the face, listening
      showPage(Page::Face);
      if (!inConversation) onTap(ui::Target::Face);
      break;
    case timescreen::Tap::None:
      break;
  }
}

// Only the parts that changed are redrawn (a full redraw flickers on this screen)
uint32_t timeButtonsKey = 0;

uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
uint32_t mixText(uint32_t h, const char* s) {
  while (s && *s) h = mix(h, (uint8_t)*s++);
  return h;
}

void drawTime(uint32_t now) {
  alarms::fillView(timeView, now);
  timeView.confirmRow = timeConfirm;
  timeView.note = timeNoteUntil ? timeNote.c_str() : "";
  uint32_t rings = mixText(2166136261u + timeView.ringCount, timeView.note);
  for (int i = 0; i < timeView.ringCount; ++i) {
    const timescreen::Ring& r = timeView.rings[i];
    rings = mix(mix(mix(rings, r.id), r.left * 2 + (r.paused ? 1 : 0)), r.total);
  }
  const uint32_t buttons = (timeView.focusRunning ? 1 : 0) | (timeView.timersFull ? 2 : 0) | (timeView.focusMinutes << 2);
  uint32_t list = mix(wallclock::valid() ? (uint32_t)(wallclock::now() / 60) : 1, timeView.rowCount);
  list = mix(mix(mix(list, timeView.confirmRow + 1), timeView.moreRows), timeView.focusToday);
  for (int i = 0; i < timeView.rowCount; ++i) list = mixText(mix(list, timeView.rows[i].id), timeView.rows[i].when);
  auto draw = [](gfx::Canvas& c) { timescreen::draw(c, timeView); };
  if (timeDirty) {
    display::render(timescreen::AREA, ui::BACKGROUND, draw);
  } else {
    if (rings != timeRingsKey) display::render(timescreen::RINGS_AREA, ui::BACKGROUND, draw);
    if (buttons != timeButtonsKey) display::render(timescreen::BUTTONS_AREA, ui::BACKGROUND, draw);
    if (list != timeListKey) display::render(timescreen::LIST_AREA, ui::BACKGROUND, draw);
  }
  timeDirty = false;
  timeRingsKey = rings;
  timeButtonsKey = buttons;
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

// ---------- to-do list ----------
// The tasks live on Gorgi (todos.cpp). You add them by voice (the PC turns what you
// say into a task with a day), with the on-screen keyboard, or by asking Gorgi.

todoscreen::View todoView;
bool todoFull = true;  // the To-do screen needs drawing in full
uint32_t todoHeaderKey = 0, todoListKey = 0, todoBottomKey = 0;
uint32_t todoFieldKey = 0, todoWhenKey = 0, todoKeysKey = 0, todoKbBottomKey = 0;
int todoPressedDrawn = -1;
uint32_t todoVersionSeen = 0;
uint32_t todoFreshId = 0, todoFreshUntil = 0;  // a task just added: highlighted for a moment
uint32_t todoToastUntil = 0;
enum class TodoUndo : uint8_t { None, Done, Delete };
TodoUndo todoUndoKind = TodoUndo::None;
todos::Task todoUndoTask;
bool todoVoiceForKb = false;  // the words go into the keyboard's text box
char todoKbNote[40] = "";
uint32_t todoKbNoteUntil = 0;
bool todoScrolled = false;   // this touch scrolls the list (it's no tap)
int todoFirstAtDown = 0;
constexpr uint32_t TODO_FRESH_MS = 4000;
constexpr uint32_t TODO_UNDO_MS = 5000;
constexpr uint32_t TODO_NOTE_MS = 3500;

int32_t todoToday() { return wallclock::valid() ? todos::dayOf(wallclock::now()) : todos::NO_DAY; }
uint32_t epochNow() { return wallclock::valid() ? (uint32_t)wallclock::now() : 0; }

// Tells the helper the list, so the AI knows it (and can tick tasks off by name)
void sendTodoList() {
  JsonDocument doc;
  todos::toJson(doc, todoToday());
  pclink::send(doc);
}

void todoToast(const String& text, bool undo) {
  snprintf(todoView.toast, sizeof todoView.toast, "%s", ui::toAscii(text).c_str());
  todoView.toastUndo = undo;
  todoToastUntil = millis() + (undo ? TODO_UNDO_MS : TODO_NOTE_MS);
  if (!undo) todoUndoKind = TodoUndo::None;
}

void todoKbSay(const char* text) {
  snprintf(todoKbNote, sizeof todoKbNote, "%s", text);
  todoKbNoteUntil = millis() + TODO_NOTE_MS;
}

// The reminder of a task rings like any other reminder (Time screen)
void todoArmReminder(uint32_t id) {
  const todos::Task* t = todos::find(id);
  if (!t || !t->remindAt || t->done() || t->remindAt <= epochNow()) return;
  if (alarms::remindAt((time_t)t->remindAt, t->text) == alarms::Result::Ok) todos::setAlarm(id, alarms::lastAddedId());
}

void todoDisarmReminder(uint32_t id) {
  const todos::Task* t = todos::find(id);
  if (!t || !t->alarmId) return;
  alarms::cancel(t->alarmId);
  todos::setAlarm(id, 0);
}

// Adds a task (from the keyboard, voice or Gorgi's AI) and shows where it landed
bool todoAdd(const String& text, int32_t due, bool someday, bool star, uint32_t remindAt, bool show) {
  uint32_t id = 0;
  const todos::Result r = todos::add(ui::toAscii(text).c_str(), due, someday, star, remindAt, &id);
  if (r == todos::Result::Full) {
    todoToast(String("Your list is full (") + todos::MAX_OPEN + " tasks). Finish a few first!", false);
    return false;
  }
  if (r != todos::Result::Ok) return false;
  todoArmReminder(id);
  todoFreshId = id;
  todoFreshUntil = millis() + TODO_FRESH_MS;
  if (show) {  // jump to its list and scroll to it
    const todos::Task* t = todos::find(id);
    const int32_t today = todoToday();
    todoView.tab = t ? todos::sectionOf(*t, today) : todos::TODAY;
    todoView.openRow = -1;
    const todos::Task* list[todos::MAX_OPEN + todos::MAX_DONE];
    const int n = todos::collect((todos::Section)todoView.tab, today, list, todos::MAX_OPEN + todos::MAX_DONE);
    todoView.first = 0;
    for (int i = 0; i < n; ++i)
      if (list[i]->id == id) todoView.first = i - todoscreen::ROWS + 1;
  }
  return true;
}

// "Next week" on the keyboard: the coming Sunday (the Israeli week starts on Sunday)
int32_t todoDueFor(uint8_t when, int32_t today) {
  if (today == todos::NO_DAY) return todos::NO_DAY;
  switch (when) {
    case todoscreen::WHEN_TODAY: return today;
    case todoscreen::WHEN_TOMORROW: return today + 1;
    case todoscreen::WHEN_NEXT_WEEK: return today + 7 - todos::weekday(today);
    default: return todos::NO_DAY;
  }
}

void todoCloseKeyboard() {
  if (todoView.kb.voice != todoscreen::VOICE_IDLE) pclink::sendSimple("cancel");
  todoView.kb = todoscreen::Keyboard();
  todoFull = true;
}

void todoAddFromKeyboard() {
  todoscreen::Keyboard& kb = todoView.kb;
  String text(kb.text);
  text.trim();
  if (!text.length()) {
    todoKbSay("Type a task first");
    return;
  }
  if (kb.when != todoscreen::WHEN_NONE && kb.when != todoscreen::WHEN_SOMEDAY && !wallclock::valid()) {
    todoKbSay("I don't know the date yet");
    return;
  }
  const bool someday = kb.when == todoscreen::WHEN_SOMEDAY;
  if (todoAdd(text, todoDueFor(kb.when, todoToday()), someday, kb.star, 0, true)) todoCloseKeyboard();
  else todoKbSay("My list is full");
}

void todoStartVoice(bool forKeyboard) {
  if (!pclink::helperConnected()) {
    if (forKeyboard) todoKbSay("The PC isn't connected");
    else todoToast("The PC isn't connected. Tap Type to write it.", false);
    return;
  }
  todoVoiceForKb = forKeyboard;
  pclink::sendText(forKeyboard ? "{\"type\":\"dictate\",\"mode\":\"text\"}" : "{\"type\":\"dictate\",\"mode\":\"todo\"}");
  if (forKeyboard) todoView.kb.voice = todoscreen::VOICE_LISTENING;
  else todoView.voice = todoscreen::VOICE_LISTENING;
}

void todoVoiceReset() { todoView.voice = todoView.kb.voice = todoscreen::VOICE_IDLE; }

void todoStopVoice() {
  pclink::sendSimple("cancel");
  todoView.voice = todoView.kb.voice = todoscreen::VOICE_IDLE;
}

// Voice typing: {"type":"dictation","text":"Buy milk","mode":"todo","due":"2026-10-25","at":0,"star":false}
// and its state: {"type":"dictate_state","state":"listening" | "thinking" | "idle","problem":"..."}
void onDictation(const char* type, JsonObjectConst msg) {
  lastActivity = millis();
  if (strcmp(type, "dictation") == 0) {
    const String text = ui::toAscii(String((const char*)(msg["text"] | "")));
    if (todoVoiceForKb && todoView.kb.open) {  // into the text box
      todoscreen::Keyboard& kb = todoView.kb;
      String t = String(kb.text);
      if (t.length() && t[t.length() - 1] != ' ') t += " ";
      t += text;
      snprintf(kb.text, sizeof kb.text, "%s", t.c_str());
      kb.shift = false;
      return;
    }
    if (todoVoiceForKb) return;  // the keyboard was closed meanwhile
    const bool someday = msg["someday"] | false;
    const int32_t due = todos::parseDate(msg["due"] | "");
    if (todoAdd(text, due, someday, msg["star"] | false, msg["at"] | 0u, page == Page::Todo)) {
      todoToast(String("Added: ") + text, false);
    }
    return;
  }
  const char* state = msg["state"] | "idle";
  const uint8_t v = strcmp(state, "listening") == 0  ? todoscreen::VOICE_LISTENING
                    : strcmp(state, "thinking") == 0 ? todoscreen::VOICE_THINKING
                                                     : todoscreen::VOICE_IDLE;
  if (todoVoiceForKb) todoView.kb.voice = todoView.kb.open ? v : todoscreen::VOICE_IDLE;
  else todoView.voice = v;
  if (v == todoscreen::VOICE_IDLE && msg["problem"].is<const char*>()) {
    if (todoVoiceForKb) todoKbSay(msg["problem"].as<const char*>());
    else todoToast(String(msg["problem"].as<const char*>()), false);
  }
}

// Gorgi's AI changed the list: {"type":"todo_add" | "todo_done" | "todo_remove", ...}
void onTodoMessage(const char* type, JsonObjectConst msg) {
  if (strcmp(type, "todo_add") == 0) {
    todoAdd(String((const char*)(msg["text"] | "")), todos::parseDate(msg["due"] | ""), msg["someday"] | false,
            msg["star"] | false, msg["at"] | 0u, false);
  } else if (strcmp(type, "todo_done") == 0) {
    const uint32_t id = msg["id"] | 0u;
    const bool done = msg["done"] | true;
    if (done) todoDisarmReminder(id);
    todos::setDone(id, done, epochNow());
    if (!done) todoArmReminder(id);
  } else if (strcmp(type, "todo_remove") == 0) {
    const uint32_t id = msg["id"] | 0u;
    todoDisarmReminder(id);
    todos::remove(id);
  } else if (strcmp(type, "todo_star") == 0) {
    todos::setStar(msg["id"] | 0u, msg["star"] | true);
  }
  sendTodoList();  // (also when nothing changed, so the helper knows)
}

void todoSetup() {
  todos::begin();
  todoVersionSeen = todos::version();
  // The Sketch screen is gone (4.6): its drawings go too (the user said so)
  Preferences old;
  if (old.begin("sketch", false)) {
    if (old.getUInt("cur", 0) != 0) {
      if (LittleFS.begin(false)) {
        File dir = LittleFS.open("/sketch");
        String names[32];
        int n = 0;
        if (dir) {
          for (File f = dir.openNextFile(); f && n < 32; f = dir.openNextFile()) {
            String name = f.name();
            f.close();
            const int slash = name.lastIndexOf('/');
            names[n++] = String("/sketch/") + (slash >= 0 ? name.substring(slash + 1) : name);
          }
          dir.close();
        }
        for (int i = 0; i < n; ++i) LittleFS.remove(names[i]);
        LittleFS.rmdir("/sketch");
        LittleFS.end();
        Serial.printf("todo: removed %d old drawing file(s)\n", n);
      }
      old.clear();
    }
    old.end();
  }
}

void todoShown() {
  todoView.openRow = -1;
  todoFull = true;
}

void todoHidden() {
  if (todoView.voice != todoscreen::VOICE_IDLE || todoView.kb.voice != todoscreen::VOICE_IDLE) todoStopVoice();
  todoView.kb = todoscreen::Keyboard();
  todoView.openRow = -1;
  todoScrolled = false;
}

// Finger down on the To-do screen: a key lights up; on the list, maybe a scroll starts
void todoBegin(int x, int y) {
  todoScrolled = false;
  todoFirstAtDown = todoView.first;
  if (todoView.kb.open) todoView.kb.pressed = todoscreen::keyAt(x, y);
}

// Finger moving: the list scrolls with it (row by row)
void todoMove(int x, int y) {
  if (todoView.kb.open) {
    if (todoView.kb.pressed >= 0 && todoscreen::keyAt(x, y) != todoView.kb.pressed) todoView.kb.pressed = -1;
    return;
  }
  const int dy = y - touchY0, dx = x - touchX0;
  if (!todoScrolled && touchY0 >= todoscreen::LIST_AREA.y && touchY0 < todoscreen::LIST_AREA.bottom() &&
      abs(dy) > 14 && abs(dy) > abs(dx))
    todoScrolled = true;
  if (todoScrolled) {
    const int steps = (dy >= 0 ? dy + todoscreen::ROW_STEP / 2 : dy - todoscreen::ROW_STEP / 2) / todoscreen::ROW_STEP;
    todoView.first = todoFirstAtDown - steps;
    todoView.openRow = -1;
  }
}

void onTodoTap(int x, int y) {
  todoscreen::Keyboard& kb = todoView.kb;
  if (todoScrolled) {
    todoScrolled = false;
    return;
  }
  const int pressedKey = kb.pressed;
  kb.pressed = -1;
  const todoscreen::TapResult t = todoscreen::tap(x, y, todoView);
  const todoscreen::Row* row = (t.index >= 0 && t.index < todoView.rowCount) ? &todoView.rows[t.index] : nullptr;
  switch (t.what) {
    case todoscreen::Tap::KbKey:
      if (pressedKey == t.index && todoscreen::press(kb, t.index) == todoscreen::Press::Add) todoAddFromKeyboard();
      return;
    case todoscreen::Tap::KbWhen:
      kb.when = kb.when == t.index ? todoscreen::WHEN_NONE : (uint8_t)t.index;
      return;
    case todoscreen::Tap::KbStar:
      kb.star = !kb.star;
      return;
    case todoscreen::Tap::KbCancel:
      todoCloseKeyboard();
      return;
    case todoscreen::Tap::KbVoice:
      if (kb.voice != todoscreen::VOICE_IDLE) todoStopVoice();
      else todoStartVoice(true);
      return;
    case todoscreen::Tap::Tab:
      if (todoView.tab != t.index) {
        todoView.tab = (uint8_t)t.index;
        todoView.first = 0;
      }
      todoView.openRow = -1;
      return;
    case todoscreen::Tap::Check: {
      if (!row) return;
      const uint32_t id = row->id;
      todoView.openRow = -1;
      if (row->done) {
        todos::setDone(id, false, 0);
        todoArmReminder(id);
        return;
      }
      const todos::Task* task = todos::find(id);
      if (!task) return;
      todoUndoTask = *task;
      todoDisarmReminder(id);
      todos::setDone(id, true, epochNow());
      todoToast(String("Done: ") + todoUndoTask.text, true);
      todoUndoKind = TodoUndo::Done;
      return;
    }
    case todoscreen::Tap::Star:
      if (row) todos::setStar(row->id, !row->star);
      return;
    case todoscreen::Tap::Row:
      todoView.openRow = todoView.openRow == t.index ? -1 : t.index;
      return;
    case todoscreen::Tap::Action: {
      if (!row) return;
      const uint32_t id = row->id;
      const int32_t today = todoToday();
      todoView.openRow = -1;
      switch (t.action) {
        case todoscreen::ACT_STAR:
          todos::setStar(id, !row->star);
          break;
        case todoscreen::ACT_TODAY:
        case todoscreen::ACT_TOMORROW:
          if (today == todos::NO_DAY) {
            todoToast("I don't know the date yet", false);
            break;
          }
          todos::setDue(id, today + (t.action == todoscreen::ACT_TOMORROW ? 1 : 0), false);
          todoFreshId = id;
          todoFreshUntil = millis() + 1500;
          break;
        case todoscreen::ACT_NOT_DONE:
          todos::setDone(id, false, 0);
          todoArmReminder(id);
          break;
        case todoscreen::ACT_DELETE: {
          const todos::Task* task = todos::find(id);
          if (!task) break;
          todoDisarmReminder(id);
          todoUndoTask = *todos::find(id);
          todos::remove(id);
          todoToast(String("Deleted: ") + todoUndoTask.text, true);
          todoUndoKind = TodoUndo::Delete;
          break;
        }
      }
      return;
    }
    case todoscreen::Tap::Undo:
      if (todoUndoKind == TodoUndo::Done) {
        todos::setDone(todoUndoTask.id, false, 0);
        todoArmReminder(todoUndoTask.id);
      } else if (todoUndoKind == TodoUndo::Delete) {
        todoUndoTask.alarmId = 0;
        todos::restore(todoUndoTask);
        todoArmReminder(todoUndoTask.id);
      }
      todoUndoKind = TodoUndo::None;
      todoView.toast[0] = 0;
      todoToastUntil = 0;
      return;
    case todoscreen::Tap::Voice:
      if (todoView.voice != todoscreen::VOICE_IDLE) todoStopVoice();
      else todoStartVoice(false);
      return;
    case todoscreen::Tap::Type:
      todoView.kb = todoscreen::Keyboard();
      todoView.kb.open = true;
      todoView.openRow = -1;
      if (todoView.tab == todos::UPCOMING) todoView.kb.when = todoscreen::WHEN_TOMORROW;
      todoFull = true;
      return;
    case todoscreen::Tap::Elsewhere:
      todoView.openRow = -1;
      return;
    case todoscreen::Tap::None:
      return;
  }
}

// Today is empty because everything on it got ticked today (a little celebration)
bool todoAllDoneToday(int32_t today) {
  if (today == todos::NO_DAY) return false;
  const todos::Task* list[todos::MAX_DONE + todos::MAX_OPEN];
  const int n = todos::collect(todos::FINISHED, today, list, todos::MAX_DONE + todos::MAX_OPEN);
  for (int i = 0; i < n; ++i)
    if (list[i]->doneAt && todos::dayOf((time_t)list[i]->doneAt) == today) return true;
  return false;
}

void drawTodo() {
  const int32_t today = todoToday();
  todoscreen::View& v = todoView;
  todoscreen::fill(v, today, todoFreshUntil ? todoFreshId : 0);
  v.allDone = v.counts[todos::TODAY] == 0 && todoAllDoneToday(today);
  v.kb.note = todoKbNoteUntil ? todoKbNote : "";
  auto draw = [](gfx::Canvas& c) { todoscreen::draw(c, todoView); };
  if (v.kb.open) {
    const uint32_t field = mixText(2166136261u, v.kb.text);
    const uint32_t when = mix(mix(2166136261u, v.kb.when), v.kb.star);
    const uint32_t keys = mix(mix(2166136261u, v.kb.shift), v.kb.symbols);
    const uint32_t bottom = mixText(mix(2166136261u, v.kb.voice), v.kb.note);
    if (todoFull) {
      display::render(todoscreen::AREA, ui::BACKGROUND, draw);
    } else {
      if (field != todoFieldKey) display::render(todoscreen::KB_FIELD_AREA, ui::BACKGROUND, draw);
      if (when != todoWhenKey) display::render(todoscreen::KB_WHEN_AREA, ui::BACKGROUND, draw);
      if (keys != todoKeysKey) {
        display::render(todoscreen::KB_KEYS_AREA, ui::BACKGROUND, draw);
      } else if (v.kb.pressed != todoPressedDrawn) {  // just the key that lit up / went out
        if (todoPressedDrawn >= 0)
          display::render(gfx::inflate(todoscreen::keyRect(todoPressedDrawn), 1), ui::BACKGROUND, draw);
        if (v.kb.pressed >= 0)
          display::render(gfx::inflate(todoscreen::keyRect(v.kb.pressed), 1), ui::BACKGROUND, draw);
      }
      if (bottom != todoKbBottomKey) display::render(todoscreen::KB_BOTTOM_AREA, ui::BACKGROUND, draw);
    }
    todoFieldKey = field;
    todoWhenKey = when;
    todoKeysKey = keys;
    todoKbBottomKey = bottom;
    todoPressedDrawn = v.kb.pressed;
    todoFull = false;
    return;
  }
  uint32_t header = mix(mix(mix(2166136261u, v.tab), v.counts[0]), mix(v.counts[1], v.counts[2]));
  uint32_t list = mix(mix(mix(2166136261u, v.first), v.total), mix(v.openRow + 1, v.allDone));
  list = mixText(mix(list, v.toastUndo), v.toast);
  for (int i = 0; i < v.rowCount; ++i) {
    const todoscreen::Row& r = v.rows[i];
    list = mix(mix(list, r.id), (r.star ? 1 : 0) | (r.done ? 2 : 0) | (r.fresh ? 4 : 0) | r.chipKind << 3);
    list = mixText(mixText(list, r.text), r.chip);
  }
  const uint32_t bottom = mix(2166136261u, v.voice);
  if (todoFull) {
    display::render(todoscreen::AREA, ui::BACKGROUND, draw);
  } else {
    if (header != todoHeaderKey) display::render(todoscreen::HEADER_AREA, ui::BACKGROUND, draw);
    if (list != todoListKey) display::render(todoscreen::LIST_AREA, ui::BACKGROUND, draw);
    if (bottom != todoBottomKey) display::render(todoscreen::BOTTOM_AREA, ui::BACKGROUND, draw);
  }
  todoHeaderKey = header;
  todoListKey = list;
  todoBottomKey = bottom;
  todoFull = false;
}

void updateTodo(uint32_t now) {
  todos::loop(now, !touching);
  if (todoFreshUntil && (int32_t)(now - todoFreshUntil) >= 0) todoFreshUntil = 0;
  if (todoToastUntil && (int32_t)(now - todoToastUntil) >= 0) {
    todoToastUntil = 0;
    todoView.toast[0] = 0;
    todoUndoKind = TodoUndo::None;
  }
  if (todoKbNoteUntil && (int32_t)(now - todoKbNoteUntil) >= 0) todoKbNoteUntil = 0;
  static uint32_t lastTidy = 0;
  if (msSince(now, lastTidy) > 60 * 1000) {  // finished tasks older than a week go
    lastTidy = now;
    todos::tidy(epochNow());
  }
  if (todos::version() != todoVersionSeen) {
    todoVersionSeen = todos::version();
    sendTodoList();
  }
}

// ---------- notifications (shown in the Action Center) ----------

void noticesChanged() {
  ui::setNoticeDot(noticeCount > 0);
  acDirty = true;
}

void removeNotice(int i) {
  if (i < 0 || i >= noticeCount) return;
  for (int k = i; k + 1 < noticeCount; ++k) notices[k] = notices[k + 1];
  --noticeCount;
  noticesChanged();
}

void removeNotices(uint8_t kind) {
  for (int i = noticeCount - 1; i >= 0; --i)
    if (notices[i].kind == kind) removeNotice(i);
}

// Newest first. Only one "update" notice at a time.
void addNotice(uint8_t kind, const char* title, const char* text, const char* action) {
  if (kind == actioncenter::UPDATE) removeNotices(actioncenter::UPDATE);
  const int max = (int)(sizeof notices / sizeof notices[0]);
  if (noticeCount == max) --noticeCount;  // the oldest goes
  for (int k = noticeCount; k > 0; --k) notices[k] = notices[k - 1];
  actioncenter::Notice& n = notices[0];
  n.kind = kind;
  snprintf(n.title, sizeof n.title, "%s", ui::toAscii(String(title)).c_str());
  snprintf(n.text, sizeof n.text, "%s", ui::toAscii(String(text)).c_str());
  snprintf(n.action, sizeof n.action, "%s", action);
  ++noticeCount;
  noticesChanged();
}

// Something nobody saw (an alert that gave up, or came while Gorgi was busy)
void addMissed(uint8_t kind, const char* title, const String& what) {
  char hm[8];
  wallclock::hhmm(hm, sizeof hm);
  String text = what;
  if (hm[0]) text += String("  (") + hm + ")";
  addNotice(kind, title, text.c_str(), "");
}

// ---------- Claude Code and calls (from the helper) ----------

uint8_t claudeState(const char* s) {
  if (strcmp(s, "working") == 0) return claudescreen::WORKING;
  if (strcmp(s, "waiting") == 0) return claudescreen::WAITING;
  if (strcmp(s, "done") == 0) return claudescreen::DONE;
  return claudescreen::IDLE;
}

// "18:40" today, else "Fri 09:00"
void formatWhen(int32_t epoch, char* out, int size) {
  out[0] = 0;
  if (epoch <= 0) return;
  const time_t t = epoch;
  struct tm lt;
  localtime_r(&t, &lt);
  const bool soon = wallclock::valid() && epoch - (int32_t)wallclock::now() < 20 * 3600;
  strftime(out, size, soon ? "%H:%M" : "%a %H:%M", &lt);
}

void updateClaudeBadge() {
  bool waiting = false;
  for (int i = 0; i < claudeView.count; ++i) waiting |= claudeView.sessions[i].state == claudescreen::WAITING;
  ui::setTabBadges(waiting ? 1u << (int)Page::Claude : 0);
}

void showClaudeAlert(const String& name, bool waiting, const String& text) {
  alertWakesScreen();
  const uint32_t now = millis();
  alertOn = true;
  alertClaude = true;
  alertSession = name;
  alertText = text;
  alertNotice = actioncenter::CLAUDE;
  alertTitle = waiting ? "Claude needed you" : "Claude finished";
  alertStart = alertNudge = now;
  alertMoodBack = now + 2000;
  alertMood = waiting ? Mood::Surprised : Mood::Excited;
  lastActivity = now;
  moodHoldUntil = 0;
  showPage(Page::Face);
  face.setTalking(false);
  face.setMood(Mood::Surprised);
  ui::setSubtitle(text);
  ui::setStatus("Tap to dismiss", ui::COLOR_ACCENT, true);
  setPhase(Phase::Alert);
}

void onOther(const char* type, JsonObjectConst msg) {
  if (strncmp(type, "todo_", 5) == 0) {
    onTodoMessage(type, msg);
    return;
  }
  if (strcmp(type, "dictation") == 0 || strcmp(type, "dictate_state") == 0) {
    onDictation(type, msg);
    return;
  }
  if (strcmp(type, "claude") == 0) {
    claudeView.hooked = msg["hooked"] | false;
    claudeView.count = 0;
    for (JsonObjectConst s : msg["sessions"].as<JsonArrayConst>()) {
      if (claudeView.count >= claudescreen::MAX_SESSIONS) break;
      const int i = claudeView.count++;
      claudescreen::Session& out = claudeView.sessions[i];
      snprintf(out.name, sizeof out.name, "%s", ui::toAscii(String((const char*)(s["name"] | "?"))).c_str());
      snprintf(out.text, sizeof out.text, "%s", ui::toAscii(String((const char*)(s["text"] | ""))).c_str());
      out.state = claudeState(s["state"] | "idle");
      claudeSince[i] = s["since"] | 0;
      formatWhen(claudeSince[i], out.at, sizeof out.at);
      // the alert is about a session that is busy again (answered on the PC): put it away
      if (alertOn && alertClaude && alertSession == out.name && out.state == claudescreen::WORKING) endAlert(false);
    }
    JsonObjectConst u = msg["usage"];
    const bool tokens = !u.isNull() && strcmp(u["mode"] | "percent", "tokens") == 0;
    claudeView.usage.known = !u.isNull() && (tokens || u["five"].is<float>());
    claudeView.usage.tokens = tokens;
    snprintf(claudeView.usage.plan, sizeof claudeView.usage.plan, "%s", (const char*)(u["plan"] | ""));
    if (tokens) {
      claudescreen::formatTokens(u["five_tokens"] | 0u, claudeView.usage.fiveTokens, sizeof claudeView.usage.fiveTokens);
      claudescreen::formatTokens(u["week_tokens"] | 0u, claudeView.usage.weekTokens, sizeof claudeView.usage.weekTokens);
      claudeUsageAt = u["at"] | 0;
    } else if (claudeView.usage.known) {
      claudeView.usage.five = u["five"] | 0.0f;
      claudeView.usage.week = u["week"] | 0.0f;
      formatWhen(u["five_reset"] | 0, claudeView.usage.fiveReset, sizeof claudeView.usage.fiveReset);
      formatWhen(u["week_reset"] | 0, claudeView.usage.weekReset, sizeof claudeView.usage.weekReset);
      claudeUsageAt = u["at"] | 0;
    }
    claudeDirty = true;
    updateClaudeBadge();
  } else if (strcmp(type, "claude_alert") == 0) {
    const bool waiting = strcmp(msg["kind"] | "done", "waiting") == 0;
    if (inConversation || updating || (alertOn && !alertClaude)) {  // busy: keep it for the Action Center
      addMissed(actioncenter::CLAUDE, waiting ? "Claude needed you" : "Claude finished",
                String((const char*)(msg["text"] | "")));
      return;
    }
    showClaudeAlert(String((const char*)(msg["name"] | "")), waiting, String((const char*)(msg["text"] | "")));
  } else if (strcmp(type, "update_info") == 0) {
    updateAvailable = msg["available"] | false;
    updateBusy = msg["busy"] | false;  // the helper says when it's done (or failed)
    updateVersion = (const char*)(msg["version"] | "");
    if (updateAvailable) {
      char text[64];
      snprintf(text, sizeof text, "Gorgi %s - the PC helper first, then Gorgi", updateVersion.c_str());
      addNotice(actioncenter::UPDATE, "Update ready", text, "Update");
    } else {
      removeNotices(actioncenter::UPDATE);
    }
    acDirty = true;
  } else if (strcmp(type, "update_status") == 0) {
    updateBusy = true;
    snprintf(updateBusyText, sizeof updateBusyText, "%s", (const char*)(msg["text"] | ""));
    acDirty = true;
    if (!alertOn && !inConversation) {
      ui::setSubtitle(String("Updating: ") + updateBusyText);
      moodHoldUntil = millis() + 60000;
    }
  } else if (strcmp(type, "call") == 0) {
    callOn = msg["on"] | false;
    ui::setCallButton(callOn);
    if (phase == Phase::Idle || phase == Phase::Sleepy || phase == Phase::Asleep) showIdleStatus();
    Serial.printf("call: %s\n", callOn ? "started" : "ended");
  }
}

void drawClaude(uint32_t now) {
  if (!claudeDirty && msSince(now, claudeDrawnAt) < 30000) return;  // "4 min ago" changes slowly
  claudeDirty = false;
  claudeDrawnAt = now;
  claudeView.pc = pclink::helperConnected();
  const int32_t t = wallclock::valid() ? (int32_t)wallclock::now() : 0;
  for (int i = 0; i < claudeView.count; ++i)
    claudeView.sessions[i].ago = t && claudeSince[i] ? t - claudeSince[i] : -1;
  claudeView.usage.age = t && claudeUsageAt ? t - claudeUsageAt : -1;
  display::render(claudescreen::AREA, ui::BACKGROUND, [](gfx::Canvas& c) { claudescreen::draw(c, claudeView); });
}

// ---------- Action Center, notifications, sleep ----------

void openActionCenter() {
  acOpen = true;
  acDirty = true;
  acView.weatherOpen = acView.cityPicker = false;
  pclink::sendCommand("check_updates");  // so an update shows up if there is one
}

void closeActionCenter() {
  if (!acOpen) return;
  acOpen = false;
  // it covered everything: the top bar, the screen and the music line come back
  ui::redrawAll();
  if (page == Page::Face) {
    display::render(gfx::Rect{0, ui::TOP_H, 800, 330 - ui::TOP_H}, ui::BACKGROUND, [](gfx::Canvas&) {});
  }
  markPageDirty(page);
}

void goToSleep() {
  screenOff = true;
  face.setTalking(false);
  face.setMood(Mood::Asleep);
  ui::setSubtitle("");
  showIdleStatus();
  setPhase(Phase::Asleep);
  board::setBacklight(false);
}

void wakeScreen() {
  screenOff = false;
  board::setBacklight(true);
  wakeUp();
}

void onActionCenterRelease() {
  if (touchEndY - touchY0 < -60) {  // pushed back up
    closeActionCenter();
    return;
  }
  const actioncenter::TapResult t = actioncenter::tap(touchX0, touchY0, acView);
  switch (t.what) {
    case actioncenter::Tap::Sleep:
      closeActionCenter();
      goToSleep();
      return;
    case actioncenter::Tap::Weather:
      acView.weatherOpen = true;
      break;
    case actioncenter::Tap::Back:
      acView.weatherOpen = acView.cityPicker = false;
      break;
    case actioncenter::Tap::OpenPicker:
      acView.cityPicker = true;
      break;
    case actioncenter::Tap::ClosePicker:
      acView.cityPicker = false;
      break;
    case actioncenter::Tap::PickCity:
      acView.cityPicker = false;
      setWeatherCity(t.index);
      break;
    case actioncenter::Tap::Action:  // only updates have a button for now
      if (notices[t.index].kind == actioncenter::UPDATE && updateAvailable && !updateBusy &&
          pclink::helperConnected()) {
        pclink::sendCommand("update");
        updateBusy = true;
        snprintf(updateBusyText, sizeof updateBusyText, "Starting...");
        closeActionCenter();
        showPage(Page::Face);
        say(Mood::Happy, "Updating! The PC helper goes first, then me.");
        return;
      }
      break;
    case actioncenter::Tap::Dismiss:
      removeNotice(t.index);
      break;
    case actioncenter::Tap::None:
      return;
  }
  acDirty = true;
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

  if (tp.pressed && !touching && screenOff) {  // asleep: a touch only wakes Gorgi up
    touching = true;
    touchUsed = true;
    touchTarget = ui::Target::None;
    lastActivity = now;
    wakeScreen();
    return;
  }
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

  if (acOpen) {  // the Action Center takes every touch until it closes
    if (tp.pressed && !touching) {
      touching = true;
      touchX0 = tp.x;
      touchY0 = touchEndY = tp.y;
      lastActivity = now;
    } else if (tp.pressed) {
      touchEndY = tp.y;
    } else if (touching) {
      touching = false;
      onActionCenterRelease();
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
    if (touchTarget == ui::Target::Prev || touchTarget == ui::Target::PlayPause || touchTarget == ui::Target::Next ||
        touchTarget == ui::Target::Volume)
      ui::openMusicBar();  // using it keeps it open
    Serial.printf("touch at %d,%d -> target %d\n", tp.x, tp.y, (int)touchTarget);

    if (page == Page::Todo && touchTarget == ui::Target::Face) todoBegin(tp.x, tp.y);

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
    // pulling down from the top bar opens the Action Center
    if (touchY0 < ui::TOP_H && tp.y - touchY0 > 50 && abs(tp.y - touchY0) > abs(tp.x - touchX0)) {
      touchUsed = true;
      touchTarget = ui::Target::None;
      openActionCenter();
      return;
    }
    const int dx = tp.x - touchX0;
    // A sideways drag in the middle of the screen moves the screens
    if (touchTarget == ui::Target::Face && !swiping && !longPressDone && abs(dx) > 16 &&
        abs(dx) * 5 > abs(tp.y - touchY0) * 6 &&
        !(page == Page::Todo && (todoView.kb.open || todoScrolled))) {
      swiping = true;
    }
    if (swiping) {
      touchEndX = tp.x;
      return;
    }
    if (page == Page::Todo && touchTarget == ui::Target::Face) todoMove(tp.x, tp.y);
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
    if (swiping) {
      swiping = false;
      // A long enough sideways swipe changes screens
      const int dx = touchEndX - touchX0;
      const int next = (int)page + (dx < 0 ? 1 : -1);  // swipe left -> the next screen
      if (abs(dx) >= SWIPE_MIN_PX && next >= 0 && next < (int)Page::Count) showPage((Page)next);
      return;
    }
    if (released == ui::Target::Tab) {
      const int t = ui::tabAt(touchX0, touchY0);
      if (t >= 0 && t < (int)Page::Count) showPage((Page)t);
      return;
    }
    if (released == ui::Target::Mini) {  // the slim music line: open the full bar for a while
      ui::openMusicBar();
      return;
    }
    if (released == ui::Target::Talk) {  // a Talk button in the top bar: to the face, listening
      showPage(Page::Face);
      if (!inConversation) onTap(ui::Target::Face);
      return;
    }
    if (released == ui::Target::CallMute) {
      pclink::sendCommand("teams_mute");
      return;
    }
    if (released == ui::Target::Face && page != Page::Face) {
      if (!longPressDone && page == Page::Time) onTimeTap(touchX0, touchY0);
      if (page == Page::Todo) onTodoTap(touchX0, touchY0);
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

void drawActionCenter() {
  char hm[8];
  wallclock::hhmm(hm, sizeof hm);
  snprintf(acView.time, sizeof acView.time, "%s", hm[0] ? hm : "--:--");
  acView.date[0] = 0;
  if (wallclock::valid()) {
    const time_t t = wallclock::now();
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(acView.date, sizeof acView.date, "%a, %d %b", &lt);
    if (acView.date[5] == '0') memmove(acView.date + 5, acView.date + 6, strlen(acView.date + 5));  // "Fri, 4 Oct"
  }
  acView.wx = &wx;
  acView.city = wxCity;
  acView.wxStatus = wxStatus.c_str();
  acView.noticeCount = noticeCount < actioncenter::MAX_NOTICES ? noticeCount : actioncenter::MAX_NOTICES;
  for (int i = 0; i < acView.noticeCount; ++i) acView.notices[i] = notices[i];
  acView.busy = updateBusy;
  const int rssi = pclink::wifiRssi();
  const char* wifi = rssi == 0 ? "no Wi-Fi" : rssi > -60 ? "Wi-Fi good" : rssi > -72 ? "Wi-Fi OK" : "Wi-Fi weak";
  if (pclink::helperConnected())
    snprintf(acView.status, sizeof acView.status, "Gorgi %s  -  %s  -  PC: %s connected", GORGI_VERSION, wifi,
             ui::toAscii(pcName).c_str());
  else
    snprintf(acView.status, sizeof acView.status, "Gorgi %s  -  %s  -  PC not connected", GORGI_VERSION, wifi);
  display::render(actioncenter::AREA, ui::BACKGROUND, [](gfx::Canvas& c) { actioncenter::draw(c, acView); });
}

void drawFrame(uint32_t now) {
  if (acOpen) {  // it covers the whole screen: everything else waits until it closes
    const uint32_t minute = wallclock::valid() ? (uint32_t)(wallclock::now() / 60) : 0;
    if (acDirty || minute != acMinute) {
      acDirty = false;
      acMinute = minute;
      drawActionCenter();
    }
    return;
  }
  if (page == Page::Face) {
    face.render(lcd, ui::FACE_X, ui::FACE_Y, now);
  } else if (page == Page::Time) {
    drawTime(now);
  } else if (page == Page::Todo) {
    drawTodo();
  } else if (page == Page::Claude) {
    drawClaude(now);
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
  h.onOther = onOther;
  pclink::begin(h);
  wallclock::begin();
  alarms::begin();
  todoSetup();

  wxPrefs.begin("weather", false);
  wxCity = constrain(wxPrefs.getInt("city", weather::DEFAULT_CITY), 0, weather::CITY_COUNT - 1);
  ui::setTabs(TAB_ICONS, (int)Page::Count);
  ui::setTab((int)page);
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
  updateTodo(millis());

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
