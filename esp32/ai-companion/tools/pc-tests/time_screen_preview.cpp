#include <stdio.h>
#include <string.h>
#include <vector>
#include "time_screen.h"
using namespace timescreen;
static void save(const char* name, const View& v) {
  const int W = 800, H = 332;
  std::vector<uint16_t> buf(W * H);
  gfx::Canvas c; c.begin(buf.data(), W, H, 0, 44);
  c.clear(gfx::rgb(0x05070A));
  draw(c, v);
  FILE* f = fopen(name, "wb"); fprintf(f, "P6 %d %d 255\n", W, H);
  for (uint16_t p : buf) { unsigned char rgb[3] = {(unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p & 31) << 3)}; fwrite(rgb, 1, 3, f); }
  fclose(f);
}
static Ring R(uint32_t id, uint8_t k, const char* l, uint32_t left, uint32_t total, bool p) { Ring r{}; r.id=id; r.kind=k; snprintf(r.label,32,"%s",l); r.left=left; r.total=total; r.paused=p; return r; }
static Row W(const char* when, const char* text) { Row r{}; snprintf(r.when,28,"%s",when); snprintf(r.text,64,"%s",text); return r; }
int main() {
  View v; v.clockKnown = true;
  save("empty.ppm", v);
  v.rings[0] = R(1, TIMER, "Pasta", 581, 600, false); v.ringCount = 1;
  v.rows[0] = W("In 20 min", "Take the laundry out"); v.rows[1] = W("Today 17:00", "Call mom about the weekend plans"); v.rowCount = 2; v.focusToday = 2;
  save("one.ppm", v);
  v.rings[1] = R(2, FOCUS, "Focus", 1450, 1500, false); v.ringCount = 2; v.focusRunning = true;
  v.rows[2] = W("Every day 08:00", "Take vitamins"); v.rows[3] = W("Sun-Thu 07:30", "Gym"); v.rowCount = 4; v.moreRows = 3;
  save("two.ppm", v);
  v.rings[2] = R(3, TIMER, "Tea", 3725, 3900, true); v.ringCount = 3; v.confirmRow = 1;
  save("three.ppm", v);
  v.rings[3] = R(4, TIMER, "5 min timer", 59, 300, false); v.ringCount = 4; v.timersFull = true; v.confirmRow = -1; v.note = "I can only run 3 timers at once";
  save("four.ppm", v);
  View h; h.clockKnown = true;
  h.rings[0] = R(1, TIMER, "פסטה", 581, 600, false); h.ringCount = 1;
  h.rows[0] = W("In 20 min", "להוציא את הכביסה"); h.rows[1] = W("Today 17:00", "להתקשר לאמא על התוכניות לסופש");
  h.rows[2] = W("Every day 08:00", "Take vitamins"); h.rows[3] = W("Sun-Thu 07:30", "חדר כושר עם Dana ב-7:30"); h.rowCount = 4;
  save("hebrew.ppm", h);
}
