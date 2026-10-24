// Timers, focus sessions and reminders: keeps them (saved in Gorgi's own
// memory, so they survive restarts and updates) and says when one is due.
// The Time screen (time_screen.h) shows them.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

#include "time_screen.h"

namespace alarms {

constexpr int MAX_TIMERS = 3;      // plus one focus session
constexpr int MAX_REMINDERS = 20;

enum class Result { Ok, Full, NoClock, Bad };

// Call once at startup (after wallclock::begin), then loop() every frame.
void begin();
void loop(uint32_t nowMs);

// Something that just became due (or was missed while Gorgi was off).
struct Due {
  enum Kind : uint8_t { Timer, Focus, Reminder, Missed } kind;
  String text;  // what Gorgi shows and the PC says, e.g. "Ding! Your pasta timer is done!"
};
// Takes the oldest due item, if any.
bool takeDue(Due& out);

Result addTimer(uint32_t seconds, const char* label);
Result startFocus(uint32_t seconds);
// A reminder at a moment in time (seconds since 1970)
Result remindAt(time_t at, const char* text);
// A repeating reminder: minuteOfDay 0..1439 (local time), days = bit 0 Sunday .. bit 6 Saturday
Result remindRepeat(int minuteOfDay, uint8_t days, const char* text);

// The id of the timer or reminder added last (for the To-do screen to cancel it later)
uint32_t lastAddedId();

bool cancel(uint32_t id);       // a timer, focus session or reminder
bool togglePause(uint32_t id);  // a timer or focus session

int focusMinutes();             // the length the focus button starts
void nextFocusLength();
bool focusRunning();

// Changes whenever something was added, removed, paused or finished
uint32_t version();

// Fills what the Time screen shows
void fillView(timescreen::View& v, uint32_t nowMs);
// Something counting down, for the top bar ("9:41"); false if nothing runs.
// focus is set when the soonest one is a focus session.
bool soonest(uint32_t nowMs, uint32_t& secondsLeft, bool& focus);

// The list for the PC helper (so the AI knows what's set)
void toJson(JsonDocument& doc, uint32_t nowMs);

}  // namespace alarms
