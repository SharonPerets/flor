// Draws the Claude screen, the top-bar tabs and the call button into pictures.
#include <stdio.h>
#include <string.h>
#include <vector>
#include "claude_screen.h"
#include "tabs.h"
#include "panel.h"
using namespace claudescreen;
static void save(const char* name, int y0, int h, void (*fn)(gfx::Canvas&, void*), void* ctx) {
  const int W = 800;
  std::vector<uint16_t> buf(W * h);
  gfx::Canvas c; c.begin(buf.data(), W, h, 0, y0);
  c.clear(gfx::rgb(0x05070A));
  fn(c, ctx);
  FILE* f = fopen(name, "wb"); fprintf(f, "P6 %d %d 255\n", W, h);
  for (uint16_t p : buf) { unsigned char rgb[3] = {(unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p & 31) << 3)}; fwrite(rgb, 1, 3, f); }
  fclose(f);
}
static Session S(const char* n, uint8_t st, const char* t, int ago, const char* at) { Session s{}; snprintf(s.name, 28, "%s", n); s.state = st; snprintf(s.text, 48, "%s", t); s.ago = ago; snprintf(s.at, 8, "%s", at); return s; }
int main() {
  View v; v.pc = true; v.hooked = true;
  v.sessions[0] = S("flor", WAITING, "needs permission", 30, ""); v.sessions[1] = S("api-server", WORKING, "", 250, "");
  v.sessions[2] = S("scripts", DONE, "", 3000, "12:05"); v.sessions[3] = S("a-very-long-project-name-here", IDLE, "", -1, ""); v.count = 4;
  v.usage.known = true; v.usage.five = 62; v.usage.week = 93; snprintf(v.usage.fiveReset, 20, "18:40"); snprintf(v.usage.weekReset, 20, "Fri 09:00"); v.usage.age = 130;
  save("claude.ppm", 44, 332, [](gfx::Canvas& c, void* p) { draw(c, *(View*)p); }, &v);
  View e; e.pc = true; e.hooked = false;
  save("claude_empty.ppm", 44, 332, [](gfx::Canvas& c, void* p) { draw(c, *(View*)p); }, &e);
  save("tabs.ppm", 0, 300, [](gfx::Canvas& c, void*) {
    const uint8_t icons[] = {tabs::MUSIC, tabs::FACE, tabs::WEATHER, tabs::TIME, tabs::SKETCH, tabs::CLAUDE};
    tabs::draw(c, icons, 6, 1, 1u << 5);
    tabs::drawCallButton(c, false);
  }, nullptr);
  struct P { View v; panel::View pv; } pp{v, panel::View()};
  pp.pv.update = panel::Update::Available; snprintf(pp.pv.newVersion, 16, "4.2");
  snprintf(pp.pv.status, 96, "Gorgi 4.1  -  Wi-Fi good  -  PC: MY-PC connected");
  save("panel.ppm", 0, 376, [](gfx::Canvas& c, void* p) {
    P* x = (P*)p;
    const uint8_t icons[] = {tabs::MUSIC, tabs::FACE, tabs::WEATHER, tabs::TIME, tabs::SKETCH, tabs::CLAUDE};
    tabs::draw(c, icons, 6, 5, 0);
    panel::drawHandle(c, true);
    draw(c, x->v);
    gfx::Canvas pc; pc.begin(c.row(panel::AREA.y) , c.width(), panel::AREA.h, 0, panel::AREA.y);
    panel::draw(pc, x->pv);
  }, &pp);
  printf("pictures: claude.ppm claude_empty.ppm tabs.ppm panel.ppm\n");
}
