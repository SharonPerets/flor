// Whole-screen pictures of the 4.3 layout: top bar, the slim music line, the Action Center.
#include <stdio.h>
#include <string.h>
#include <vector>
#include "action_center.h"
#include "gfont.h"
#include "panel.h"
#include "tabs.h"
#include "time_screen.h"
static void save(const char* name, void (*fn)(gfx::Canvas&, void*), void* ctx) {
  const int W = 800, H = 480;
  std::vector<uint16_t> buf(W * H);
  gfx::Canvas c; c.begin(buf.data(), W, H, 0, 0);
  c.clear(gfx::rgb(0x05070A));
  fn(c, ctx);
  FILE* f = fopen(name, "wb"); fprintf(f, "P6 %d %d 255\n", W, H);
  for (uint16_t p : buf) { unsigned char rgb[3] = {(unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p & 31) << 3)}; fwrite(rgb, 1, 3, f); }
  fclose(f);
}
static const gfx::Color DIM = gfx::rgb(0xC9D3DD), BORDER = gfx::rgb(0x1F2935), PANEL = gfx::rgb(0x10151C), LINE = gfx::rgb(0x161D27);
static void topBar(gfx::Canvas& c, int current, bool dot) {
  c.fillRect(0, tabs::LINE_Y, 800, 1, LINE);
  c.text(gfont::TITLE, "23:08", 20, 30, DIM);
  const uint8_t icons[] = {tabs::MUSIC, tabs::FACE, tabs::TIME, tabs::SKETCH, tabs::CLAUDE};
  tabs::draw(c, icons, 5, current, 0);
  panel::drawHandle(c, dot);
  const char* st = "Tap me to talk";
  const float w = gfx::Canvas::textWidth(gfont::SMALL, st) + 52, x = 780 - w;
  c.fillRRect(x, 7, w, 30, 15, BORDER); c.fillRRect(x + 1, 8, w - 2, 28, 14, PANEL);
  c.fillCapsule(x + 21, 17, x + 21, 22, 3.2f, DIM); c.text(gfont::SMALL, st, x + 38, 27, DIM);
}
static void faceMock(gfx::Canvas& c) {  // stand-in for the real face (it needs the board)
  const gfx::Color eye = gfx::rgb(0x4FE3D6);
  c.fillRRect(400 - 150, 130, 100, 140, 46, eye); c.fillRRect(400 + 50, 130, 100, 140, 46, eye);
  c.strokeArc(400, 270, 46, 9, 35, 145, eye);
  const char* sub = "Hi! What are we working on today?";
  c.text(gfont::SUBTITLE, sub, 400 - gfx::Canvas::textWidth(gfont::SUBTITLE, sub) / 2, 390, gfx::rgb(0xE8EEF4));
}
int main() {
  save("l_face.ppm", [](gfx::Canvas& c, void*) { topBar(c, 1, false); faceMock(c); tabs::drawMiniPlayer(c, "Wicked Garden", "Stone Temple Pilots", true); }, nullptr);
  save("l_time.ppm", [](gfx::Canvas& c, void*) {
    topBar(c, 2, true);
    timescreen::View v; v.clockKnown = true; v.rings[0] = timescreen::Ring{1, timescreen::FOCUS, "Focus", 1490, 1500, false}; v.ringCount = 1; v.focusRunning = true;
    timescreen::draw(c, v);
    tabs::drawMiniPlayer(c, "Wicked Garden", "Stone Temple Pilots", false);
  }, nullptr);
  static weather::Data wx; wx.valid = true; wx.city = 0; wx.temp = 24.3f; wx.code = 1; wx.isDay = true; wx.dayCount = 7;
  const char* names[] = {"Today", "Sat", "Sun", "Mon", "Tue", "Wed", "Thu"}; int codes[] = {1, 2, 61, 3, 0, 0, 80};
  for (int i = 0; i < 7; ++i) { snprintf(wx.days[i].name, 6, "%s", names[i]); wx.days[i].code = codes[i]; wx.days[i].hi = 26 - i % 3; wx.days[i].lo = 17 + i % 2; wx.days[i].rain = 0; }
  static actioncenter::View a;
  snprintf(a.time, 8, "23:08"); snprintf(a.date, 32, "Friday, 24 October"); a.wx = &wx; a.city = 0;
  snprintf(a.status, 96, "Gorgi 4.3  -  Wi-Fi good  -  PC: SHARON-PC connected");
  a.notices[0] = actioncenter::Notice{actioncenter::UPDATE, "Update ready", "Gorgi 4.4 - helper first, then Gorgi", "Update"};
  a.notices[1] = actioncenter::Notice{actioncenter::MISSED, "Missed: Call mom", "Reminder at 17:00, nobody tapped it", ""};
  a.notices[2] = actioncenter::Notice{actioncenter::CLAUDE, "Claude finished in flor", "While Gorgi was asleep, 21:40", ""};
  a.noticeCount = 3;
  save("l_action.ppm", [](gfx::Canvas& c, void* p) { actioncenter::draw(c, *(actioncenter::View*)p); }, &a);
  static actioncenter::View e = a; e.noticeCount = 0;
  save("l_action_empty.ppm", [](gfx::Canvas& c, void* p) { actioncenter::draw(c, *(actioncenter::View*)p); }, &e);
  printf("pictures: l_face l_time l_action l_action_empty\n");
}
