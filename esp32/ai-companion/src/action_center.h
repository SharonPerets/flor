// The Action Center: pull down from the top bar. A full screen of its own with
// the time and date, the weather, notifications (an update is ready, things you
// missed...), Sleep, and a status line. Push it back up to close it.
//
// Drawing only, so it also runs in the PC preview. main.cpp keeps the
// notifications and opens / closes it.
#pragma once

#include <stdint.h>

#include "gfx.h"
#include "weather.h"

namespace actioncenter {

constexpr gfx::Rect AREA{0, 0, 800, 480};  // all of the screen
constexpr int MAX_NOTICES = 4;

enum Kind : uint8_t { UPDATE, MISSED, CLAUDE, INFO };

struct Notice {
  uint8_t kind;
  char title[40];   // "Update ready"
  char text[64];    // "Gorgi 4.4 - tap Update to install"
  char action[12];  // button text ("Update"), "" = only the x to clear it
};

struct View {
  char time[8] = "";     // "23:08"
  char date[32] = "";    // "Fri, 24 Oct"
  const weather::Data* wx = nullptr;
  int city = 0;
  const char* wxStatus = "";
  bool weatherOpen = false;  // the forecast is open (tap the weather chip)
  bool cityPicker = false;   // choosing the weather city
  Notice notices[MAX_NOTICES];
  int noticeCount = 0;
  bool busy = false;     // an update is running (its button shows "Updating...")
  char status[96] = "";  // "Gorgi 4.3 - Wi-Fi good - PC: MY-PC"
  int language = -1;     // what Gorgi speaks: 0 English, 1 Hebrew, -1 unknown (the switch is hidden)
};

void draw(gfx::Canvas& c, const View& v);

enum class Tap { None, Sleep, Weather, Back, OpenPicker, PickCity, ClosePicker, Action, Dismiss, Language };
struct TapResult {
  Tap what;
  int index;  // the notice, the city for PickCity, the language for Language (0 English, 1 Hebrew)
  TapResult(Tap w = Tap::None, int i = 0) : what(w), index(i) {}
};
TapResult tap(int x, int y, const View& v);

}  // namespace actioncenter
