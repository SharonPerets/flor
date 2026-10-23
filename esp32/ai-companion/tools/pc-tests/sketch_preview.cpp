// Draws the Sketch screen and its gallery into pictures, and checks that a
// drawing saved and loaded again is exactly the same.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "sketch.h"
using namespace sketch;
static void save(const char* name, void (*fn)(gfx::Canvas&, void*), void* ctx) {
  const int W = 800, H = 332;
  std::vector<uint16_t> buf(W * H);
  gfx::Canvas c; c.begin(buf.data(), W, H, 0, 44);
  c.clear(gfx::rgb(0x05070A));
  fn(c, ctx);
  FILE* f = fopen(name, "wb"); fprintf(f, "P6 %d %d 255\n", W, H);
  for (uint16_t p : buf) { unsigned char rgb[3] = {(unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p & 31) << 3)}; fwrite(rgb, 1, 3, f); }
  fclose(f);
}
int main() {
  std::vector<uint16_t> pic(PAPER_W * PAPER_H, PAPER_COLOR.c);
  // a little house, a sun and a squiggle
  auto line = [&](float a, float b, float c2, float d, int col, int size) { stroke(pic.data(), a, b, c2, d, COLORS[col], SIZES[size]); };
  line(250, 300, 250, 200, 0, 1); line(250, 200, 400, 200, 0, 1); line(400, 200, 400, 300, 0, 1); line(250, 300, 400, 300, 0, 1);
  line(240, 205, 325, 130, 1, 1); line(325, 130, 410, 205, 1, 1);
  for (int i = 0; i < 36; ++i) { float a = i * 0.1745f, b = (i + 1) * 0.1745f; line(650 + 40 * cosf(a), 120 + 40 * sinf(a), 650 + 40 * cosf(b), 120 + 40 * sinf(b), 3, 2); }
  float px = 150, py = 340; for (int i = 1; i < 60; ++i) { float x = 150 + i * 9, y = 340 + 12 * sinf(i * 0.4f); line(px, py, x, y, 4, 0); px = x; py = y; }
  // save + load
  std::vector<uint8_t> packed(PAPER_W * PAPER_H * 4);
  size_t n = 0; for (int y = 0; y < PAPER_H; y += 40) n += packRows(pic.data(), y, y + 40, packed.data() + n);
  printf("packed %zu bytes (raw %d)\n", n, PAPER_W * PAPER_H * 2);
  std::vector<uint16_t> back(PAPER_W * PAPER_H, 0);
  Unpacker u(back.data()); for (size_t i = 0; i < n; i += 777) u.feed(packed.data() + i, n - i < 777 ? n - i : 777);
  assert(u.complete() && back == pic);
  std::vector<uint16_t> thumb(THUMB_W * THUMB_H, 0);
  Unpacker t = Unpacker::forThumb(thumb.data()); t.feed(packed.data(), n);
  for (uint16_t p : thumb) assert(p != 0);  // every thumbnail pixel filled
  printf("save/load OK, thumbnail OK\n");
  struct Ctx { uint16_t* pic; Tools tools; Gallery g; } ctx{pic.data(), Tools(), Gallery()};
  ctx.tools.color = 3; ctx.tools.canUndo = true;
  save("sketch.ppm", [](gfx::Canvas& c, void* p) { Ctx* x = (Ctx*)p; drawToolbar(c, x->tools); drawPaper(c, x->pic); }, &ctx);
  ctx.tools.gallery = true;
  for (int i = 0; i < 7; ++i) ctx.g.items[i] = GalleryItem{(uint32_t)(10 - i), thumb.data()};
  ctx.g.count = 7; ctx.g.current = 9; ctx.g.confirm = 3;
  save("gallery.ppm", [](gfx::Canvas& c, void* p) { Ctx* x = (Ctx*)p; drawToolbar(c, x->tools); drawGallery(c, x->g); }, &ctx);
  printf("pictures: sketch.ppm gallery.ppm\n");
}
