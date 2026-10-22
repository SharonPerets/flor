// Hardware drivers for the Waveshare ESP32-S3-Touch-LCD-7:
//  - 800x480 RGB parallel LCD (driven by screen_esp32.cpp)
//  - GT911 capacitive touch (I2C)
//  - CH422G IO expander (I2C) that controls backlight and reset lines
#pragma once

#include <Arduino.h>

static constexpr int SCREEN_W = 800;
static constexpr int SCREEN_H = 480;

struct TouchPoint {
  bool pressed = false;
  int x = 0;
  int y = 0;
};

namespace board {

// Powers up the IO expander, resets the touch chip and turns on the backlight.
// Must run before the display is initialised.
void begin();

// Turn the backlight on or off.
void setBacklight(bool on);

// Read the first touch point (non-blocking).
TouchPoint readTouch();

// True if the touch chip answered at startup.
bool touchFound();

}  // namespace board
