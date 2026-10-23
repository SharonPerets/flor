// The Claude screen: your Claude Code sessions on the PC (working, waiting for
// you, done) and how much of your Claude plan you've used (5-hour and weekly).
//
// Drawing only, so it also runs in the PC preview. The helper sends the data
// (see claude_watch.py); main.cpp keeps it.
#pragma once

#include <stdint.h>

#include "gfx.h"

namespace claudescreen {

enum State : uint8_t { IDLE = 0, WORKING = 1, WAITING = 2, DONE = 3 };

constexpr int MAX_SESSIONS = 5;

struct Session {
  char name[28];   // the project folder, e.g. "flor"
  uint8_t state;
  char text[48];   // e.g. "needs permission"
  int32_t ago;     // seconds since the state changed (-1 = unknown)
  char at[8];      // "12:05" when it changed ("" = unknown)
};

struct Usage {
  bool known = false;
  float five = 0, week = 0;  // percent used
  char fiveReset[20] = "";   // "18:40"
  char weekReset[20] = "";   // "Fri 09:00"
  int32_t age = -1;          // seconds since it was read
  char plan[12] = "";        // "Max": which account this is
  bool tokens = false;       // only token amounts are known (no plan %)
  char fiveTokens[12] = "";  // "1.2M"
  char weekTokens[12] = "";
};

// 1234567 -> "1.2M"
void formatTokens(uint32_t n, char* out, int size);

struct View {
  Session sessions[MAX_SESSIONS];
  int count = 0;
  bool pc = false;      // the helper is connected
  bool hooked = false;  // Claude Code reports to the helper
  Usage usage;
};

constexpr gfx::Rect AREA{0, 44, 800, 332};

void draw(gfx::Canvas& c, const View& v);

// "4 min", "2 h", "just now"
void formatAgo(int32_t seconds, char* out, int size);

}  // namespace claudescreen
