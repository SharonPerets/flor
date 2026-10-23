// The pull-down panel: swipe down from the top bar. Sleep (screen off until
// touched), Update (when a new version is out) and a status line.
//
// Drawing only, so it also runs in the PC preview. main.cpp opens and closes it.
#pragma once

#include <stdint.h>

#include "gfx.h"

namespace panel {

constexpr gfx::Rect AREA{110, 44, 580, 188};  // the panel, over the screen underneath

enum class Update : uint8_t { Unknown, UpToDate, Available, Busy };

struct View {
  Update update = Update::Unknown;
  char newVersion[16] = "";  // "4.2"
  char status[96] = "";      // "Gorgi 4.1 - Wi-Fi good - PC: MY-PC"
  char busy[48] = "";        // what's happening while updating
};

void draw(gfx::Canvas& c, const View& v);

enum class Tap { None, Sleep, Update, Outside };
Tap tap(int x, int y);

// The little handle at the top of the screen (hints that you can pull down);
// dot = an update is waiting.
void drawHandle(gfx::Canvas& c, bool dot);

}  // namespace panel
