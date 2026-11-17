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

constexpr int MAX_ACCOUNTS = 4;

struct Account {
  char email[48];
  char plan[12];  // "Max", "Pro", or ""
};

// Which Claude account's usage is shown (more than one can be signed in on the PC).
struct Accounts {
  Account list[MAX_ACCOUNTS];
  int count = 0;
  char chosen[48] = "";  // "" = automatic (the best plan)
  char shown[48] = "";   // the account the numbers belong to
  bool picking = false;  // the list is open in the Usage card
  bool switching = false;  // a choice was sent, waiting for the new numbers
};

// 1234567 -> "1.2M"
void formatTokens(uint32_t n, char* out, int size);

struct View {
  Session sessions[MAX_SESSIONS];
  int count = 0;
  bool pc = false;      // the helper is connected
  bool hooked = false;  // Claude Code reports to the helper
  Usage usage;
  Accounts accounts;
};

// Taps on the Usage card
constexpr int HIT_NONE = -3;
constexpr int HIT_PILL = -2;  // the account button: open / close the list
constexpr int HIT_AUTO = -1;  // "Automatic" in the list; 0.. = that account
int hit(const View& v, int x, int y);
bool showsPill(const View& v);

constexpr gfx::Rect AREA{0, 44, 800, 332};

void draw(gfx::Canvas& c, const View& v);

// "4 min", "2 h", "just now"
void formatAgo(int32_t seconds, char* out, int size);

}  // namespace claudescreen
