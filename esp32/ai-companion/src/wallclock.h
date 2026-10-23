// Gorgi's own clock: the time of day from the internet (Israel time, summer
// and winter time handled automatically). Until the internet answers, the
// time the PC helper sends is used instead.
#pragma once

#include <time.h>

namespace wallclock {

// Call once, after Wi-Fi has been started.
void begin();

// True once Gorgi knows the real time (from the internet or the PC).
bool valid();

// Seconds since 1970 (UTC). Only meaningful when valid().
time_t now();

// The PC's time, used while the internet time hasn't arrived yet.
void setFromPC(time_t epoch);

// "21:45" (24-hour), local time. Empty when the time isn't known.
void hhmm(char* out, int size);

}  // namespace wallclock
