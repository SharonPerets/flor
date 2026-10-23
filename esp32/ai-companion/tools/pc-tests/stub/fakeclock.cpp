#include "wallclock.h"
#include <stdio.h>
time_t fakeNow = 0;
namespace wallclock {
void begin() {}
bool valid() { return fakeNow > 1700000000; }
time_t now() { return fakeNow; }
void setFromPC(time_t) {}
void hhmm(char* o, int n) { o[0] = 0; }
}
