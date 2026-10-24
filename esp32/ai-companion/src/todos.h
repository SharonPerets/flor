// The to-do list: keeps the tasks (saved in Gorgi's own memory, so they survive
// restarts and updates) and sorts them into Today / Upcoming / Done.
// The To-do screen (todo_screen.h) shows them; the PC helper gets the list so
// you can add, finish and remove tasks by talking to Gorgi.
//
// Days are counted as "day numbers": days since 1 Jan 1970 in local time.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

namespace todos {

constexpr int MAX_OPEN = 40;   // tasks not done yet
constexpr int MAX_DONE = 20;   // finished ones kept (for a week)
constexpr int TEXT_MAX = 112;  // bytes of text in a task (English letters take 1, Hebrew letters 2)
constexpr int32_t NO_DAY = 0;  // no due day
constexpr uint32_t KEEP_DONE_SECONDS = 7 * 24 * 3600;

enum Flag : uint8_t { STAR = 1, DONE = 2, SOMEDAY = 4 };

struct Task {
  uint32_t id;
  int32_t due;        // day number, NO_DAY = none
  uint32_t remindAt;  // a reminder rings then (seconds since 1970), 0 = none
  uint32_t alarmId;   // that reminder on the Time screen
  uint32_t doneAt;    // when it was ticked (seconds since 1970, 0 if the clock wasn't known)
  uint8_t flags;
  char text[TEXT_MAX + 1];
  bool star() const { return flags & STAR; }
  bool done() const { return flags & DONE; }
  bool someday() const { return flags & SOMEDAY; }
};

enum class Result { Ok, Full, Bad };
enum Section : uint8_t { TODAY = 0, UPCOMING = 1, FINISHED = 2 };

// Call once at startup, then loop() every frame (it saves a moment after a change).
void begin();
void loop(uint32_t nowMs, bool canSave);
void saveNow();

// Adds a task; idOut gets its id.
Result add(const char* text, int32_t due, bool someday, bool star, uint32_t remindAt, uint32_t* idOut = nullptr);
// Puts back a task exactly as it was (undo a delete)
bool restore(const Task& t);
bool setDone(uint32_t id, bool done, uint32_t nowEpoch);
bool remove(uint32_t id);
bool setStar(uint32_t id, bool star);
bool setDue(uint32_t id, int32_t due, bool someday);
bool setAlarm(uint32_t id, uint32_t alarmId);
const Task* find(uint32_t id);
int openCount();

// Finished tasks older than a week go (needs the clock: nowEpoch 0 = keep them)
void tidy(uint32_t nowEpoch);

// Changes whenever something changed
uint32_t version();

// The tasks of a section, sorted the way the screen shows them. Returns how many (up to max).
// today = today's day number (NO_DAY when the clock isn't known: dated tasks count as upcoming).
int collect(Section s, int32_t today, const Task** out, int max);
Section sectionOf(const Task& t, int32_t today);

// The little chip on a row: "Late", "Tomorrow", "Fri", "Nov 2", "17:00", "Someday" (empty = none).
// kind: 0 = plain, 1 = late (red), 2 = a reminder time (accent)
void chipFor(const Task& t, int32_t today, char* out, int size, uint8_t& kind);

// The list for the PC helper (so the AI knows it)
void toJson(JsonDocument& doc, int32_t today);

// ---- days
int32_t dayFromDate(int year, int month, int day);  // month 1..12
void dateOf(int32_t dayNumber, int& year, int& month, int& day);
int32_t dayOf(time_t t);         // local time
int weekday(int32_t dayNumber);  // 0 = Sunday
// "2026-10-25" -> day number (NO_DAY if it isn't a date)
int32_t parseDate(const char* s);

}  // namespace todos
