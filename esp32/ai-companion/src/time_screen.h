// The Time screen (swipe left from the weather): timers and focus sessions as
// countdown rings on the left, reminders coming up on the right.
//
// This part only draws and handles taps, so it also runs in the PC preview.
// alarms.cpp keeps the timers and reminders and says when something is due.
#pragma once

#include <stdint.h>

#include "gfx.h"

namespace timescreen {

constexpr int MAX_RINGS = 4;  // 3 timers + 1 focus session
constexpr int MAX_ROWS = 4;   // reminders shown at once (the rest: "+2 more")

enum Kind : uint8_t { TIMER = 0, FOCUS = 1 };

struct Ring {
  uint32_t id;
  uint8_t kind;
  char label[32];   // "Pasta", "10 min timer", "Focus"
  uint32_t left;    // seconds to go
  uint32_t total;   // seconds at the start
  bool paused;
};

struct Row {
  uint32_t id;
  char when[28];  // "Today 17:00", "Every day 08:00"
  char text[64];
};

struct View {
  Ring rings[MAX_RINGS];
  int ringCount = 0;
  Row rows[MAX_ROWS];
  int rowCount = 0;
  int moreRows = 0;        // reminders not shown
  int focusMinutes = 25;   // length the focus button starts
  bool focusRunning = false;
  bool timersFull = false;
  int focusToday = 0;
  bool clockKnown = false;
  int confirmRow = -1;     // row asking "Delete?" (-1 = none)
  const char* note = "";   // short message at the top for a few seconds
};

// The middle of the screen, between the top bar and the music bar
constexpr gfx::Rect AREA{0, 44, 800, 332};
// Redrawn every second while something counts down
constexpr gfx::Rect RINGS_AREA{0, 44, 536, 264};
// The reminder list (redrawn every minute: "In 5 min" changes)
constexpr gfx::Rect LIST_AREA{536, 44, 264, 332};

void draw(gfx::Canvas& c, const View& v);

// Seconds as "9:41" or "1:05:00"
void formatLeft(uint32_t seconds, char* out, int size);

enum class Tap { None, Ring, RingClose, QuickTimer, FocusLength, FocusStart, Row, Delete, Keep };
struct TapResult {
  Tap what = Tap::None;
  int index = 0;  // ring / row number, or minutes for QuickTimer
};
TapResult tap(int x, int y, const View& v);

// Minutes the quick timer buttons start
constexpr int QUICK[3] = {1, 5, 10};
// Lengths the focus button cycles through
constexpr int FOCUS_LENGTHS[4] = {15, 25, 45, 60};

}  // namespace timescreen
