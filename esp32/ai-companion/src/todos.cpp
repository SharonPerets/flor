#include "todos.h"

#include <Preferences.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace todos {
namespace {

constexpr int MAX_TASKS = MAX_OPEN + MAX_DONE;
constexpr uint32_t SAVE_AFTER_MS = 1500;  // a moment after the last change (writing flash can make the screen jump)
constexpr uint32_t MAGIC = 0x31444F54;    // "TOD1"
constexpr uint32_t MAGIC_PACKED = 0x32444F54;  // "TOD2": how 4.7-4.8 saved the list (read once, then saved as TOD1)
constexpr size_t PACKED_FIXED = 21;            // id, due, remindAt, alarmId, doneAt, flags; then length + text

Task tasks[MAX_TASKS];
int count = 0;
uint32_t nextId = 1;
uint32_t changes = 1;
bool dirty = false;
uint32_t changedAt = 0;
Preferences prefs;
bool prefsOpen = false;

void changed() {
  ++changes;
  dirty = true;
  changedAt = millis();
}

int indexOf(uint32_t id) {
  for (int i = 0; i < count; ++i)
    if (tasks[i].id == id) return i;
  return -1;
}

void removeAt(int i) {
  for (int k = i; k + 1 < count; ++k) tasks[k] = tasks[k + 1];
  --count;
}

int doneCount() {
  int n = 0;
  for (int i = 0; i < count; ++i) n += tasks[i].done() ? 1 : 0;
  return n;
}

// Too many finished tasks: the oldest finished one goes
void makeRoomForDone() {
  while (doneCount() > MAX_DONE) {
    int oldest = -1;
    for (int i = 0; i < count; ++i)
      if (tasks[i].done() && (oldest < 0 || tasks[i].doneAt < tasks[oldest].doneAt)) oldest = i;
    if (oldest < 0) return;
    removeAt(oldest);
  }
}

void copyText(char* out, const char* in) {
  // squeeze spaces, cut to TEXT_MAX
  int n = 0;
  bool space = true;
  for (const char* p = in; *p && n < TEXT_MAX; ++p) {
    char ch = *p;
    if (ch == '\n' || ch == '\t' || ch == '\r') ch = ' ';
    if ((uint8_t)ch < 32 || (uint8_t)ch > 126) continue;
    if (ch == ' ' && space) continue;
    space = ch == ' ';
    out[n++] = ch;
  }
  while (n > 0 && out[n - 1] == ' ') --n;
  out[n] = 0;
}

int sortKey(const Task& t, int32_t today, Section s) {
  // smaller first
  if (s == TODAY) {
    if (t.star()) return 0;
    if (t.due != NO_DAY && t.due < today) return 1;  // late
    return 2;
  }
  return 0;
}

bool before(const Task& a, const Task& b, int32_t today, Section s) {
  if (s == FINISHED) return a.doneAt != b.doneAt ? a.doneAt > b.doneAt : a.id > b.id;  // newest first
  const int ka = sortKey(a, today, s), kb = sortKey(b, today, s);
  if (ka != kb) return ka < kb;
  if (s == UPCOMING) {
    if (a.someday() != b.someday()) return !a.someday();  // someday ones last
    if (a.due != b.due) return a.due < b.due;
    if (a.star() != b.star()) return a.star();
  } else if (ka == 1 && a.due != b.due) {
    return a.due < b.due;  // the latest one first
  }
  return a.id < b.id;  // in the order they were added
}

}  // namespace

// ---------------------------------------------------------------- days

int32_t dayFromDate(int y, int m, int d) {
  // days since 1970-01-01 (Howard Hinnant's days_from_civil)
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

void dateOf(int32_t z, int& y, int& m, int& d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)yoe + era * 400 + (m <= 2);
}

int32_t dayOf(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  return dayFromDate(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

int weekday(int32_t day) { return (int)(((day % 7) + 11) % 7); }  // 1 Jan 1970 was a Thursday

int32_t parseDate(const char* s) {
  int y = 0, m = 0, d = 0;
  if (!s || sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) return NO_DAY;
  if (y < 2000 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31) return NO_DAY;
  return dayFromDate(y, m, d);
}

// ---------------------------------------------------------------- the list

// Tasks saved by 4.7-4.8: kept, with only plain letters (others dropped)
static void loadPacked(const uint8_t* p, size_t len) {
  size_t at = 8;
  while (count < MAX_TASKS && at + PACKED_FIXED + 1 <= len) {
    Task& t = tasks[count];
    memcpy(&t.id, p + at, 4);
    memcpy(&t.due, p + at + 4, 4);
    memcpy(&t.remindAt, p + at + 8, 4);
    memcpy(&t.alarmId, p + at + 12, 4);
    memcpy(&t.doneAt, p + at + 16, 4);
    t.flags = p[at + 20];
    const size_t n = p[at + PACKED_FIXED];
    at += PACKED_FIXED + 1;
    if (at + n > len) break;
    char raw[256];
    memcpy(raw, p + at, n);
    raw[n] = 0;
    at += n;
    copyText(t.text, raw);
    if (t.text[0]) ++count;
  }
  dirty = count > 0;
}

void begin() {
  prefsOpen = prefs.begin("todo", false);
  count = 0;
  const size_t len = prefs.getBytesLength("list");
  if (len >= 8 && len <= 8 + 256 * MAX_TASKS) {
    uint8_t* buf = (uint8_t*)malloc(len);
    uint32_t magic = 0;
    if (buf && prefs.getBytes("list", buf, len) == len) memcpy(&magic, buf, 4);
    if (magic == MAGIC_PACKED) {
      memcpy(&nextId, buf + 4, 4);
      loadPacked(buf, len);
      for (int i = 0; i < count; ++i)
        if (tasks[i].id >= nextId) nextId = tasks[i].id + 1;
    }
    free(buf);
  }
  if (count == 0 && len >= 8 && (len - 8) % sizeof(Task) == 0) {
    static uint8_t buf[8 + sizeof(Task) * MAX_TASKS];
    if (len <= sizeof buf && prefs.getBytes("list", buf, len) == len) {
      uint32_t magic;
      memcpy(&magic, buf, 4);
      memcpy(&nextId, buf + 4, 4);
      const int n = (int)((len - 8) / sizeof(Task));
      if (magic == MAGIC && n <= MAX_TASKS) {
        memcpy(tasks, buf + 8, n * sizeof(Task));
        count = n;
        for (int i = 0; i < count; ++i) {
          tasks[i].text[TEXT_MAX] = 0;
          if (tasks[i].id >= nextId) nextId = tasks[i].id + 1;
        }
      }
    }
  }
  if (!nextId) nextId = 1;
  Serial.printf("todo: %d task(s) from memory\n", count);
  if (dirty) saveNow();
}

void saveNow() {
  if (!dirty) return;
  dirty = false;
  if (!prefsOpen) return;
  static uint8_t buf[8 + sizeof(Task) * MAX_TASKS];
  memcpy(buf, &MAGIC, 4);
  memcpy(buf + 4, &nextId, 4);
  memcpy(buf + 8, tasks, count * sizeof(Task));
  prefs.putBytes("list", buf, 8 + count * sizeof(Task));
}

void loop(uint32_t nowMs, bool canSave) {
  if (dirty && canSave && (int32_t)(nowMs - changedAt) > (int32_t)SAVE_AFTER_MS) saveNow();
}

Result add(const char* text, int32_t due, bool someday, bool star, uint32_t remindAt, uint32_t* idOut) {
  Task t;
  memset(&t, 0, sizeof t);
  copyText(t.text, text ? text : "");
  if (!t.text[0]) return Result::Bad;
  if (t.text[0] >= 'a' && t.text[0] <= 'z') t.text[0] -= 32;  // "buy milk" -> "Buy milk"
  if (openCount() >= MAX_OPEN) return Result::Full;
  if (count >= MAX_TASKS) {  // only finished ones beyond the open limit: the oldest goes
    int oldest = -1;
    for (int i = 0; i < count; ++i)
      if (tasks[i].done() && (oldest < 0 || tasks[i].doneAt < tasks[oldest].doneAt)) oldest = i;
    if (oldest < 0) return Result::Full;
    removeAt(oldest);
  }
  t.id = nextId++;
  t.due = someday ? NO_DAY : due;
  t.remindAt = remindAt;
  t.flags = (star ? STAR : 0) | (someday ? SOMEDAY : 0);
  tasks[count++] = t;
  if (idOut) *idOut = t.id;
  changed();
  return Result::Ok;
}

bool restore(const Task& t) {
  if (indexOf(t.id) >= 0 || count >= MAX_TASKS) return false;
  tasks[count++] = t;
  if (t.id >= nextId) nextId = t.id + 1;
  changed();
  return true;
}

bool setDone(uint32_t id, bool done, uint32_t nowEpoch) {
  const int i = indexOf(id);
  if (i < 0 || tasks[i].done() == done) return false;
  if (!done && openCount() >= MAX_OPEN) return false;
  Task& t = tasks[i];
  if (done) {
    t.flags |= DONE;
    t.doneAt = nowEpoch;
  } else {
    t.flags &= ~DONE;
    t.doneAt = 0;
  }
  if (done) makeRoomForDone();
  changed();
  return true;
}

bool remove(uint32_t id) {
  const int i = indexOf(id);
  if (i < 0) return false;
  removeAt(i);
  changed();
  return true;
}

bool setStar(uint32_t id, bool star) {
  const int i = indexOf(id);
  if (i < 0 || tasks[i].star() == star) return false;
  if (star) tasks[i].flags |= STAR;
  else tasks[i].flags &= ~STAR;
  changed();
  return true;
}

bool setDue(uint32_t id, int32_t due, bool someday) {
  const int i = indexOf(id);
  if (i < 0) return false;
  Task& t = tasks[i];
  t.due = someday ? NO_DAY : due;
  if (someday) t.flags |= SOMEDAY;
  else t.flags &= ~SOMEDAY;
  changed();
  return true;
}

bool setAlarm(uint32_t id, uint32_t alarmId) {
  const int i = indexOf(id);
  if (i < 0) return false;
  tasks[i].alarmId = alarmId;
  changed();
  return true;
}

const Task* find(uint32_t id) {
  const int i = indexOf(id);
  return i < 0 ? nullptr : &tasks[i];
}

int openCount() { return count - doneCount(); }

void tidy(uint32_t nowEpoch) {
  if (!nowEpoch) return;
  for (int i = count - 1; i >= 0; --i) {
    const Task& t = tasks[i];
    if (t.done() && t.doneAt && t.doneAt + KEEP_DONE_SECONDS < nowEpoch) {
      removeAt(i);
      changed();
    }
  }
}

uint32_t version() { return changes; }

Section sectionOf(const Task& t, int32_t today) {
  if (t.done()) return FINISHED;
  if (t.someday()) return UPCOMING;
  if (t.due == NO_DAY) return TODAY;
  if (today == NO_DAY) return UPCOMING;
  return t.due <= today ? TODAY : UPCOMING;
}

int collect(Section s, int32_t today, const Task** out, int max) {
  int n = 0;
  for (int i = 0; i < count; ++i) {
    const Task& t = tasks[i];
    if (sectionOf(t, today) != s) continue;
    // insert in order (the lists are short)
    int at = n < max ? n : max;
    while (at > 0 && before(t, *out[at - 1], today, s)) --at;
    if (at >= max) continue;
    for (int k = (n < max ? n : max - 1); k > at; --k) out[k] = out[k - 1];
    out[at] = &t;
    if (n < max) ++n;
  }
  return n;
}

void chipFor(const Task& t, int32_t today, char* out, int size, uint8_t& kind) {
  static const char* DAYS[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  out[0] = 0;
  kind = 0;
  if (t.done()) return;
  if (t.someday()) {
    snprintf(out, size, "Someday");
    return;
  }
  if (t.remindAt) {  // a reminder: its time ("17:00"), with the day when it isn't today
    const time_t at = t.remindAt;
    struct tm lt;
    localtime_r(&at, &lt);
    const int32_t day = dayFromDate(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    kind = 2;
    if (today != NO_DAY && day == today) snprintf(out, size, "%02d:%02d", lt.tm_hour, lt.tm_min);
    else if (today != NO_DAY && day == today + 1) snprintf(out, size, "Tmrw %02d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(out, size, "%s %02d:%02d", DAYS[weekday(day)], lt.tm_hour, lt.tm_min);
    if (today != NO_DAY && day < today) kind = 1;
    return;
  }
  if (t.due == NO_DAY) return;
  if (today != NO_DAY) {
    if (t.due < today) {
      kind = 1;
      snprintf(out, size, "Late");
      return;
    }
    if (t.due == today) {
      snprintf(out, size, "Today");
      return;
    }
    if (t.due == today + 1) {
      snprintf(out, size, "Tomorrow");
      return;
    }
    if (t.due < today + 7) {
      snprintf(out, size, "%s", DAYS[weekday(t.due)]);
      return;
    }
  }
  int y, m, d;
  dateOf(t.due, y, m, d);
  snprintf(out, size, "%s %d", MONTHS[m - 1], d);
}

void toJson(JsonDocument& doc, int32_t today) {
  doc["type"] = "todo_list";
  doc["max"] = MAX_OPEN;
  char date[12];
  if (today != NO_DAY) {
    int y, m, d;
    dateOf(today, y, m, d);
    snprintf(date, sizeof date, "%04d-%02d-%02d", y, m, d);
    doc["today"] = date;
  }
  JsonArray list = doc["tasks"].to<JsonArray>();
  for (int i = 0; i < count; ++i) {
    const Task& t = tasks[i];
    JsonObject o = list.add<JsonObject>();
    o["id"] = t.id;
    o["text"] = t.text;
    if (t.due != NO_DAY) {
      int y, m, d;
      dateOf(t.due, y, m, d);
      snprintf(date, sizeof date, "%04d-%02d-%02d", y, m, d);
      o["due"] = date;
    }
    if (t.someday()) o["someday"] = true;
    if (t.star()) o["star"] = true;
    if (t.done()) o["done"] = true;
    if (t.remindAt) o["at"] = t.remindAt;
  }
}

}  // namespace todos
