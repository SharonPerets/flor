// The weather screen: now + the next 7 days for a city in Israel.
//
// This part only draws and handles taps, so it also runs in the PC preview.
// weather_net.cpp fetches the forecast from Open-Meteo (free, no key needed).
#pragma once

#include <stdint.h>

#include "gfx.h"

namespace weather {

struct City {
  const char* name;
  float lat, lon;
};
constexpr int CITY_COUNT = 16;
extern const City CITIES[CITY_COUNT];
constexpr int DEFAULT_CITY = 0;  // Nahariya

struct Day {
  char name[6];  // "Today", "Fri"...
  int code;      // WMO weather code
  int hi, lo;    // degrees C
  int rain;      // chance of rain, %
};

struct Data {
  bool valid = false;
  int city = DEFAULT_CITY;
  float temp = 0, feels = 0, wind = 0;  // C, C, km/h
  int code = 0, humidity = 0;
  bool isDay = true;
  Day days[7];
  int dayCount = 0;
  char updated[6] = "";  // "14:30", local time of the reading
};

// Weather codes (WMO) in words, e.g. 2 -> "Partly cloudy"
const char* describe(int code);

// The middle of the screen, between the top bar and the music bar
constexpr gfx::Rect AREA{0, 44, 800, 332};

// Draws the screen. status is shown when there's no data yet (e.g. "Getting the weather...").
void draw(gfx::Canvas& c, const Data& d, int city, bool picker, const char* status);

// A weather picture: sun, clouds, rain... centred on (cx, cy), about `size` px wide.
void drawIcon(gfx::Canvas& c, int code, bool isDay, float cx, float cy, float size);

enum class Tap { None, OpenPicker, ClosePicker, PickCity };
// What a tap at (x, y) does. For PickCity, city is set.
Tap tap(int x, int y, bool picker, int& city);

}  // namespace weather
