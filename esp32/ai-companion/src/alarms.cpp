#include "alarms.h"

#include <Preferences.h>

#include <vector>

#include "wallclock.h"

namespace alarms {
namespace {

// What is saved in flash. Plain structs, so they can be stored as they are.
// Change FORMAT if these ever change, so old data is ignored instead of misread.
constexpr int FORMAT = 1;
constexpr int SLOTS = MAX_TIMERS + 1;  // timers + one focus session

struct TimerRec {
  uint32_t id;
  uint8_t used, kind, paused, restore;  // restore: running before a restart, waiting for the clock
  char label[32];
  uint32_t total;     // seconds
  uint32_t left;      // seconds, while paused
  int32_t endEpoch;   // when it ends (0 = unknown), for after a restart
};

struct ReminderRec {
  uint32_t id;
  uint8_t used, days;  // days: 0 = once, else bit 0 Sunday .. bit 6 Saturday
  uint16_t minute;     // repeating: minute of the day
  int32_t at;          // when it goes off next (seconds since 1970; 0 = not worked out yet)
  char text[64];
};

TimerRec timers[SLOTS];
uint32_t endMs[SLOTS];  // when each running timer ends, in millis()
ReminderRec reminders[MAX_REMINDERS];

Preferences prefs;
uint32_t nextId = 1;
uint32_t changes = 1;
bool clockSeen = false;  // the first time the clock is known, missed things are collected
int focusLen = 25;
int focusDay = 0, focusCount = 0;
std::vector<Due> dueList;

void changed() { ++changes; }

int today() {
  if (!wallclock::valid()) return 0;
  const time_t t = wallclock::now();
  struct tm lt;
  localtime_r(&t, &lt);
  return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
}

int focusToday() { return focusDay && focusDay == today() ? focusCount : 0; }

void copyText(char* out, size_t size, const char* in) {
  // keep plain text only (the screen font has no emoji or accents)
  size_t n = 0;
  for (const char* p = in; *p && n + 1 < size; ++p) {
    const unsigned char ch = (unsigned char)*p;
    if (ch >= 32 && ch < 127) out[n++] = (char)ch;
  }
  while (n && out[n - 1] == ' ') --n;
  out[n] = 0;
}

uint32_t secondsLeft(int i, uint32_t nowMs) {
  const TimerRec& t = timers[i];
  if (t.paused || t.restore) return t.left;
  const int32_t ms = (int32_t)(endMs[i] - nowMs);
  return ms <= 0 ? 0 : (uint32_t)((ms + 999) / 1000);
}

void save() {
  const uint32_t nowMs = millis();
  const bool clock = wallclock::valid();
  const time_t now = clock ? wallclock::now() : 0;
  TimerRec out[SLOTS];
  for (int i = 0; i < SLOTS; ++i) {
    out[i] = timers[i];
    if (out[i].used && !out[i].paused && !out[i].restore)
      out[i].endEpoch = clock ? (int32_t)(now + secondsLeft(i, nowMs)) : 0;
  }
  prefs.putBytes("tmr", out, sizeof out);
  prefs.putBytes("rem", reminders, sizeof reminders);
  prefs.putInt("fmt", FORMAT);
}

// The next time a repeating reminder goes off, after `after`
time_t nextRepeat(const ReminderRec& r, time_t after) {
  struct tm base;
  localtime_r(&after, &base);
  for (int d = 0; d <= 8; ++d) {
    struct tm t = base;
    t.tm_mday += d;
    t.tm_hour = r.minute / 60;
    t.tm_min = r.minute % 60;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    const time_t when = mktime(&t);  // also tidies up the date (day 32 -> next month)
    struct tm check;
    localtime_r(&when, &check);
    if (when > after && (r.days & (1 << check.tm_wday))) return when;
  }
  return after + 24 * 3600;  // can't happen with at least one day set
}

String capitalised(const char* s) {
  String out(s);
  if (out.length() && out[0] >= 'a' && out[0] <= 'z') out.setCharAt(0, out[0] - 32);
  return out;
}

String sentence(const char* s) {
  String out(s);
  out.trim();
  if (out.length() && strchr(".!?", out[out.length() - 1]) == nullptr) out += "!";
  return out;
}

void defaultLabel(uint32_t s, char* out, size_t size) {
  if (s < 60)
    snprintf(out, size, "%u second timer", (unsigned)s);
  else if (s % 3600 == 0)
    snprintf(out, size, "%u hour timer", (unsigned)(s / 3600));
  else
    snprintf(out, size, "%u minute timer", (unsigned)((s + 30) / 60));
}

String timerDoneText(const TimerRec& t) {
  String label(t.label);
  String low = label;
  low.toLowerCase();
  if (low.endsWith("timer")) return String("Ding! Your ") + label + " is done!";
  return String("Ding! Your ") + label + " timer is done!";
}

String focusDoneText(int n) {
  if (n <= 1) {
    static const char* first[] = {"Focus session done! Great work!", "Done! That's your first focus session today. Nice!",
                                  "Focus time is over. You crushed it!"};
    return first[random(3)];
  }
  static const char* more[] = {"Focus session done! That's %d today. Great work!",
                               "Done! %d focus sessions today, you're on fire!",
                               "Focus time is over. That makes %d today. Nice job!"};
  char buf[80];
  snprintf(buf, sizeof buf, more[random(3)], n);
  return buf;
}

void addDue(Due::Kind kind, const String& text) {
  if (dueList.size() >= 8) dueList.erase(dueList.begin());
  dueList.push_back(Due{kind, text});
  Serial.printf("alarms: due: %s\n", text.c_str());
}

// When the clock is first known: restart timers that ran before a restart,
// and collect what went off while Gorgi was off.
void onClockKnown() {
  const time_t now = wallclock::now();
  const uint32_t nowMs = millis();
  std::vector<String> missed;
  for (int i = 0; i < SLOTS; ++i) {
    TimerRec& t = timers[i];
    if (!t.used || !t.restore) continue;
    t.restore = 0;
    const int32_t left = t.endEpoch - (int32_t)now;
    if (left > 0) {
      endMs[i] = nowMs + (uint32_t)left * 1000;
    } else {
      if (left > -12 * 3600) missed.push_back(t.kind == timescreen::FOCUS ? String("a focus session") : String(t.label));
      t.used = 0;
    }
  }
  for (ReminderRec& r : reminders) {
    if (!r.used) continue;
    if (r.days) {
      r.at = (int32_t)nextRepeat(r, now);
    } else if (r.at <= now) {
      missed.push_back(r.text);
      r.used = 0;
    }
  }
  if (!missed.empty()) {
    String text = "While I was off, I missed: ";
    for (size_t i = 0; i < missed.size() && i < 3; ++i) text += String(i ? ", " : "") + missed[i];
    if (missed.size() > 3) text += String(" and ") + (missed.size() - 3) + " more";
    addDue(Due::Missed, sentence(text.c_str()));
  }
  save();
  changed();
}

int freeTimerSlot() {
  for (int i = 0; i < SLOTS; ++i)
    if (!timers[i].used) return i;
  return -1;
}

int countTimers(uint8_t kind) {
  int n = 0;
  for (const TimerRec& t : timers) n += t.used && t.kind == kind;
  return n;
}

Result startTimer(uint8_t kind, uint32_t seconds, const char* label) {
  if (seconds == 0 || seconds > 24 * 3600) return Result::Bad;
  if (kind == timescreen::TIMER && countTimers(kind) >= MAX_TIMERS) return Result::Full;
  if (kind == timescreen::FOCUS && countTimers(kind) >= 1) return Result::Full;
  const int i = freeTimerSlot();
  if (i < 0) return Result::Full;
  TimerRec& t = timers[i];
  t = TimerRec{};
  t.id = nextId++;
  t.used = 1;
  t.kind = kind;
  t.total = seconds;
  if (label && label[0]) copyText(t.label, sizeof t.label, label);
  if (!t.label[0]) {
    if (kind == timescreen::FOCUS) snprintf(t.label, sizeof t.label, "Focus");
    else defaultLabel(seconds, t.label, sizeof t.label);
  }
  endMs[i] = millis() + seconds * 1000;
  Serial.printf("alarms: %s started, %u s (%s)\n", kind == timescreen::FOCUS ? "focus" : "timer", (unsigned)seconds,
                t.label);
  save();
  changed();
  return Result::Ok;
}

int freeReminderSlot() {
  for (int i = 0; i < MAX_REMINDERS; ++i)
    if (!reminders[i].used) return i;
  return -1;
}

const char* DAY_NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void describeWhen(const ReminderRec& r, time_t now, char* out, size_t size) {
  char hm[8];
  if (r.days) {
    snprintf(hm, sizeof hm, "%02d:%02d", r.minute / 60, r.minute % 60);
    if (r.days == 0x7F) snprintf(out, size, "Every day %s", hm);
    else if (r.days == 0x1F) snprintf(out, size, "Sun-Thu %s", hm);
    else if (r.days == 0x3E) snprintf(out, size, "Mon-Fri %s", hm);
    else if (r.days == 0x60) snprintf(out, size, "Fri-Sat %s", hm);
    else {
      int total = 0;
      for (int d = 0; d < 7; ++d) total += (r.days >> d) & 1;
      String s;
      int count = 0;
      for (int d = 0; d < 7; ++d)
        if (r.days & (1 << d)) {
          if (count++) s += total > 3 ? " " : ", ";
          s += DAY_NAMES[d];
        }
      if (count == 1) snprintf(out, size, "Every %s %s", s.c_str(), hm);
      else snprintf(out, size, "%s %s", s.c_str(), hm);
    }
    return;
  }
  const time_t at = r.at;
  const long diff = (long)(at - now);
  if (diff < 3600) {
    snprintf(out, size, "In %ld min", diff <= 60 ? 1L : (diff + 59) / 60);
    return;
  }
  struct tm a, n;
  localtime_r(&at, &a);
  localtime_r(&now, &n);
  strftime(hm, sizeof hm, "%H:%M", &a);
  struct tm mid = n;  // midnight today
  mid.tm_hour = mid.tm_min = mid.tm_sec = 0;
  mid.tm_isdst = -1;
  const long days = (long)((at - mktime(&mid)) / 86400);
  if (days == 0) snprintf(out, size, "Today %s", hm);
  else if (days == 1) snprintf(out, size, "Tomorrow %s", hm);
  else if (days < 7) snprintf(out, size, "%s %s", DAY_NAMES[a.tm_wday], hm);
  else {
    char date[16];
    strftime(date, sizeof date, "%d %b", &a);
    snprintf(out, size, "%s %s", date[0] == '0' ? date + 1 : date, hm);
  }
}

// Reminders in the order they go off
int sortedReminders(int* order) {
  int n = 0;
  for (int i = 0; i < MAX_REMINDERS; ++i)
    if (reminders[i].used) order[n++] = i;
  for (int i = 1; i < n; ++i)
    for (int j = i; j > 0 && reminders[order[j]].at < reminders[order[j - 1]].at; --j) {
      const int t = order[j];
      order[j] = order[j - 1];
      order[j - 1] = t;
    }
  return n;
}

}  // namespace

void begin() {
  prefs.begin("alarms", false);
  if (prefs.getInt("fmt", 0) == FORMAT) {
    if (prefs.getBytesLength("tmr") == sizeof timers) prefs.getBytes("tmr", timers, sizeof timers);
    if (prefs.getBytesLength("rem") == sizeof reminders) prefs.getBytes("rem", reminders, sizeof reminders);
  }
  focusLen = prefs.getInt("flen", 25);
  focusDay = prefs.getInt("fday", 0);
  focusCount = prefs.getInt("fcnt", 0);
  int restored = 0;
  for (int i = 0; i < SLOTS; ++i) {
    TimerRec& t = timers[i];
    if (!t.used) continue;
    nextId = max(nextId, t.id + 1);
    if (!t.paused) {
      if (t.endEpoch) {
        t.restore = 1;  // starts again once the clock is known
        t.left = 0;
      } else {
        t.used = 0;  // the time it ends wasn't known: can't continue it
      }
    }
    restored += t.used;
  }
  int rem = 0;
  for (const ReminderRec& r : reminders) {
    if (!r.used) continue;
    nextId = max(nextId, r.id + 1);
    ++rem;
  }
  Serial.printf("alarms: %d timer(s), %d reminder(s) from memory\n", restored, rem);
}

void loop(uint32_t nowMs) {
  static uint32_t lastCheck = 0;
  if ((uint32_t)(nowMs - lastCheck) < 200) return;
  lastCheck = nowMs;

  const bool clock = wallclock::valid();
  if (clock && !clockSeen) {
    clockSeen = true;
    onClockKnown();
  }

  bool any = false;
  for (int i = 0; i < SLOTS; ++i) {
    TimerRec& t = timers[i];
    if (!t.used || t.paused || t.restore || secondsLeft(i, nowMs) > 0) continue;
    t.used = 0;
    any = true;
    if (t.kind == timescreen::FOCUS) {
      const int d = today();
      if (d != focusDay) {
        focusDay = d;
        focusCount = 0;
      }
      ++focusCount;
      prefs.putInt("fday", focusDay);
      prefs.putInt("fcnt", focusCount);
      addDue(Due::Focus, focusDoneText(focusCount));
    } else {
      addDue(Due::Timer, timerDoneText(t));
    }
  }

  if (clock) {
    const time_t now = wallclock::now();
    for (ReminderRec& r : reminders) {
      if (!r.used || r.at == 0 || r.at > now) continue;
      any = true;
      addDue(Due::Reminder, String("Hey! Reminder: ") + sentence(r.text));
      if (r.days) r.at = (int32_t)nextRepeat(r, now);
      else r.used = 0;
    }
  }

  if (any) {
    save();
    changed();
  }
}

bool takeDue(Due& out) {
  if (dueList.empty()) return false;
  out = dueList.front();
  dueList.erase(dueList.begin());
  return true;
}

Result addTimer(uint32_t seconds, const char* label) { return startTimer(timescreen::TIMER, seconds, label); }

Result startFocus(uint32_t seconds) { return startTimer(timescreen::FOCUS, seconds, "Focus"); }

Result remindAt(time_t at, const char* text) {
  if (!wallclock::valid()) return Result::NoClock;
  const time_t now = wallclock::now();
  if (!text || !text[0] || at <= now - 60 || at > now + 400L * 24 * 3600) return Result::Bad;
  const int i = freeReminderSlot();
  if (i < 0) return Result::Full;
  ReminderRec& r = reminders[i];
  r = ReminderRec{};
  r.id = nextId++;
  r.used = 1;
  r.at = (int32_t)(at > now ? at : now + 1);
  copyText(r.text, sizeof r.text, text);
  if (!r.text[0]) {
    r.used = 0;
    return Result::Bad;
  }
  Serial.printf("alarms: reminder in %ld s: %s\n", (long)(r.at - now), r.text);
  save();
  changed();
  return Result::Ok;
}

Result remindRepeat(int minuteOfDay, uint8_t days, const char* text) {
  days &= 0x7F;
  if (!text || !text[0] || minuteOfDay < 0 || minuteOfDay >= 24 * 60 || days == 0) return Result::Bad;
  const int i = freeReminderSlot();
  if (i < 0) return Result::Full;
  ReminderRec& r = reminders[i];
  r = ReminderRec{};
  r.id = nextId++;
  r.used = 1;
  r.days = days;
  r.minute = (uint16_t)minuteOfDay;
  copyText(r.text, sizeof r.text, text);
  if (!r.text[0]) {
    r.used = 0;
    return Result::Bad;
  }
  // worked out now if the clock is known, otherwise when it is
  r.at = wallclock::valid() && clockSeen ? (int32_t)nextRepeat(r, wallclock::now()) : 0;
  Serial.printf("alarms: repeating reminder %02d:%02d days %02x: %s\n", r.minute / 60, r.minute % 60, days, r.text);
  save();
  changed();
  return Result::Ok;
}

bool cancel(uint32_t id) {
  for (TimerRec& t : timers)
    if (t.used && t.id == id) {
      t.used = 0;
      save();
      changed();
      return true;
    }
  for (ReminderRec& r : reminders)
    if (r.used && r.id == id) {
      r.used = 0;
      save();
      changed();
      return true;
    }
  return false;
}

bool togglePause(uint32_t id) {
  const uint32_t nowMs = millis();
  for (int i = 0; i < SLOTS; ++i) {
    TimerRec& t = timers[i];
    if (!t.used || t.id != id || t.restore) continue;
    if (t.paused) {
      t.paused = 0;
      endMs[i] = nowMs + t.left * 1000;
    } else {
      t.left = secondsLeft(i, nowMs);
      t.paused = 1;
    }
    save();
    changed();
    return true;
  }
  return false;
}

int focusMinutes() { return focusLen; }

void nextFocusLength() {
  int k = 0;
  for (int i = 0; i < 4; ++i)
    if (timescreen::FOCUS_LENGTHS[i] == focusLen) k = i + 1;
  focusLen = timescreen::FOCUS_LENGTHS[k % 4];
  prefs.putInt("flen", focusLen);
  changed();
}

bool focusRunning() { return countTimers(timescreen::FOCUS) > 0; }

uint32_t version() { return changes; }

void fillView(timescreen::View& v, uint32_t nowMs) {
  v.ringCount = 0;
  // in the order they were started
  int order[SLOTS], n = 0;
  for (int i = 0; i < SLOTS; ++i)
    if (timers[i].used) order[n++] = i;
  for (int i = 1; i < n; ++i)
    for (int j = i; j > 0 && timers[order[j]].id < timers[order[j - 1]].id; --j) {
      const int t = order[j];
      order[j] = order[j - 1];
      order[j - 1] = t;
    }
  for (int k = 0; k < n && v.ringCount < timescreen::MAX_RINGS; ++k) {
    const int i = order[k];
    timescreen::Ring& rg = v.rings[v.ringCount++];
    rg.id = timers[i].id;
    rg.kind = timers[i].kind;
    snprintf(rg.label, sizeof rg.label, "%s", capitalised(timers[i].label).c_str());
    rg.left = secondsLeft(i, nowMs);
    rg.total = timers[i].total;
    rg.paused = timers[i].paused || timers[i].restore;
  }

  v.clockKnown = wallclock::valid();
  int rorder[MAX_REMINDERS];
  const int rn = sortedReminders(rorder);
  v.rowCount = 0;
  const time_t now = v.clockKnown ? wallclock::now() : 0;
  for (int k = 0; v.clockKnown && k < rn && v.rowCount < timescreen::MAX_ROWS; ++k) {
    const ReminderRec& r = reminders[rorder[k]];
    timescreen::Row& row = v.rows[v.rowCount++];
    row.id = r.id;
    describeWhen(r, now, row.when, sizeof row.when);
    snprintf(row.text, sizeof row.text, "%s", capitalised(r.text).c_str());
  }
  v.moreRows = v.clockKnown ? rn - v.rowCount : 0;
  v.focusMinutes = focusLen;
  v.focusRunning = focusRunning();
  v.timersFull = countTimers(timescreen::TIMER) >= MAX_TIMERS;
  v.focusToday = focusToday();
}

bool soonest(uint32_t nowMs, uint32_t& left, bool& focus) {
  bool found = false;
  for (int i = 0; i < SLOTS; ++i) {
    const TimerRec& t = timers[i];
    if (!t.used || t.paused || t.restore) continue;
    const uint32_t s = secondsLeft(i, nowMs);
    if (!found || s < left) {
      left = s;
      focus = t.kind == timescreen::FOCUS;
      found = true;
    }
  }
  return found;
}

void toJson(JsonDocument& doc, uint32_t nowMs) {
  doc["type"] = "time_list";
  JsonArray ts = doc["timers"].to<JsonArray>();
  for (int i = 0; i < SLOTS; ++i) {
    const TimerRec& t = timers[i];
    if (!t.used) continue;
    JsonObject o = ts.add<JsonObject>();
    o["id"] = t.id;
    o["kind"] = t.kind == timescreen::FOCUS ? "focus" : "timer";
    o["label"] = t.label;
    o["left"] = secondsLeft(i, nowMs);
    o["paused"] = t.paused || t.restore;
  }
  JsonArray rs = doc["reminders"].to<JsonArray>();
  const time_t now = wallclock::valid() ? wallclock::now() : 0;
  int order[MAX_REMINDERS];
  const int n = sortedReminders(order);
  for (int k = 0; k < n; ++k) {
    const ReminderRec& r = reminders[order[k]];
    JsonObject o = rs.add<JsonObject>();
    o["id"] = r.id;
    o["text"] = r.text;
    char when[28];
    describeWhen(r, now, when, sizeof when);
    o["when"] = when;
    o["repeat"] = r.days != 0;
  }
  doc["focus_today"] = focusToday();
  doc["focus_minutes"] = focusLen;
  doc["max_timers"] = MAX_TIMERS;
  doc["max_reminders"] = MAX_REMINDERS;
}

}  // namespace alarms
