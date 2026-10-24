// Draws the Sketch screen and its gallery into pictures, and checks that a
// drawing saved and loaded again is exactly the same.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "sketch.h"
using namespace sketch;
using gfx::Rect;
static void save(const char* name, void (*fn)(gfx::Canvas&, void*), void* ctx) {
  const int W = 800, H = 436;
  std::vector<uint16_t> buf(W * H);
  gfx::Canvas c; c.begin(buf.data(), W, H, 0, 44);
  c.clear(gfx::rgb(0x05070A));
  fn(c, ctx);
  FILE* f = fopen(name, "wb"); fprintf(f, "P6 %d %d 255\n", W, H);
  for (uint16_t p : buf) { unsigned char rgb[3] = {(unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p & 31) << 3)}; fwrite(rgb, 1, 3, f); }
  fclose(f);
}
// A fake finger writing a word of n loops in the Write box, starting at box x
static void fakeWord(Writer& w, int n, float x0, uint32_t& now) {
  const Rect b = w.boxRect();
  const float base = b.y + BOX_BASE;
  w.begin(x0, base - 10, COLORS[0], 1);
  for (int i = 1; i <= n * 12; ++i) {
    const float t = i / 12.0f * 6.2832f;
    w.move(x0 + i * 3.2f + 14 * sinf(t), base - 30 + 28 * cosf(t));
    now += 16;
  }
  w.end(now);
}
static void writeTest() {
  std::vector<uint16_t> page(PAPER_W * PAPER_H, PAPER_COLOR.c), box(PAPER_W * BOX_H, 0);
  Writer w; w.attach(page.data(), box.data());
  uint32_t now = 1000;
  // a short word, then a pause: the box moves on
  fakeWord(w, 3, BOX_INK_X + 10, now);
  assert(w.hasInk() && w.cursor().x == MARGIN);
  assert(!w.tick(now + 100));
  assert(w.tick(now + PAUSE_MS + 1) && !w.hasInk());
  const float after1 = w.cursor().x;
  assert(after1 > MARGIN + 20 && after1 < MARGIN + 80);
  // a long word past the slide line: moves on as soon as the finger lifts
  fakeWord(w, 11, BOX_INK_X + 20, now);
  assert(!w.hasInk() && w.cursor().x > after1 + 60);
  // space, new line, a tap on the page
  w.space(); w.newLine(); assert(w.cursor().line == 1 && w.cursor().x == MARGIN);
  fakeWord(w, 4, BOX_INK_X + 10, now); w.commit();
  w.placeAt(PAPER_X + 300, PAPER_Y + FIRST_LINE + 2 * LINE_H - 5); assert(w.cursor().line == 2 && w.cursor().x == 300);
  // the lines fill up; reaching the lower half moves the box to the top
  w.setCursor(Cursor(5, MARGIN)); w.takeLayoutChange();
  fakeWord(w, 2, BOX_INK_X + 10, now); w.commit();
  assert(!w.boxOnTop());
  w.newLine(); w.newLine(); assert(w.boxOnTop() && w.takeLayoutChange());
  fakeWord(w, 5, BOX_INK_X + 10, now); w.commit();
  // finding where to carry on after loading the page
  Writer w2; w2.attach(page.data(), box.data()); w2.findCursor();
  assert(w2.cursor().line == 7 && w2.cursor().x > MARGIN);
  for (int i = 0; i < LINES; ++i) w2.newLine();
  assert(w2.takePageFull() && w2.cursor().line == LINES - 1);
  // smoother: a straight line stays straight, and the last point is reached
  Smoother sm; gfx::Pt out[Smoother::MAX_OUT]; sm.start(0, 0, out);
  gfx::Pt last = {0, 0};
  for (int i = 1; i <= 20; ++i) { int n = sm.add(i * 5.0f, 0, out); for (int k = 0; k < n; ++k) { assert(fabsf(out[k].y) < 0.01f && out[k].x >= last.x - 0.01f); last = out[k]; } }
  int n = sm.finish(out); assert(n > 0 && out[n - 1].x == 100.0f);
  // typed mode: the word goes to the PC, nothing on the page until it's typed
  {
    std::vector<uint16_t> pg(PAPER_W * PAPER_H, PAPER_COLOR.c);
    Writer t; t.attach(pg.data(), box.data()); t.setTyped(true);
    fakeWord(t, 3, BOX_INK_X + 10, now);
    for (uint16_t p : pg) assert(p == PAPER_COLOR.c);
    assert(t.commit() && !t.hasInk());
    static Word wd; assert(t.takeWord(wd) && wd.points > 10 && wd.strokes == 1 && !t.takeWord(wd));
    assert(t.cursor().x == MARGIN);
    assert(t.typeWord("hello", COLORS[0]) && t.cursor().x > MARGIN + 40);
    const float x1 = t.cursor().x;
    assert(t.placeHandwriting(wd) && t.cursor().x > x1 + 20);
    for (int i = 0; i < 40; ++i) t.typeWord("wrap", COLORS[0]);
    assert(t.cursor().line >= 1);
    const char* alts[] = {"hallo", "hella"}; t.setChips(alts, 2);
    assert(t.chipAt((int)BOX_INK_X + 60, t.boxRect().y + 20) == 0);
  }
  printf("write mode OK\n");
  // pictures: writing near the top (box at the bottom) and lower down (box on top)
  struct C2 { Writer* w; Tools t; } ctx{&w, Tools()};
  w.setCursor(Cursor(2, 300)); w.takeLayoutChange();
  fakeWord(w, 3, BOX_INK_X + 10, now);
  save("write.ppm", [](gfx::Canvas& c, void* p) { C2* x = (C2*)p; drawToolbar(c, x->t); x->w->draw(c); }, &ctx);
  w.commit(); w.setCursor(Cursor(7, MARGIN)); fakeWord(w, 4, BOX_INK_X + 10, now);
  ctx.t.color = 1; ctx.t.confirmClear = true; ctx.t.canUndo = true;
  save("write_low.ppm", [](gfx::Canvas& c, void* p) { C2* x = (C2*)p; drawToolbar(c, x->t); x->w->draw(c); }, &ctx);
}
int main() {
  std::vector<uint16_t> pic(PAPER_W * PAPER_H, PAPER_COLOR.c);
  // a little house, a sun and a squiggle
  auto line = [&](float a, float b, float c2, float d, int col, int size) { stroke(pic.data(), a, b, c2, d, COLORS[col], SIZES[size]); };
  line(250, 300, 250, 200, 0, 1); line(250, 200, 400, 200, 0, 1); line(400, 200, 400, 300, 0, 1); line(250, 300, 400, 300, 0, 1);
  line(240, 205, 325, 130, 1, 1); line(325, 130, 410, 205, 1, 1);
  for (int i = 0; i < 36; ++i) { float a = i * 0.1745f, b = (i + 1) * 0.1745f; line(650 + 40 * cosf(a), 120 + 40 * sinf(a), 650 + 40 * cosf(b), 120 + 40 * sinf(b), 2, 2); }
  float px = 150, py = 340; for (int i = 1; i < 60; ++i) { float x = 150 + i * 9, y = 340 + 12 * sinf(i * 0.4f); line(px, py, x, y, 3, 0); px = x; py = y; }
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
  ctx.tools.color = 2; ctx.tools.canUndo = true; ctx.tools.write = false;
  save("sketch.ppm", [](gfx::Canvas& c, void* p) { Ctx* x = (Ctx*)p; drawToolbar(c, x->tools); drawPaper(c, x->pic); }, &ctx);
  ctx.tools.gallery = true;
  for (int i = 0; i < 7; ++i) ctx.g.items[i] = GalleryItem{(uint32_t)(10 - i), thumb.data()};
  ctx.g.count = 7; ctx.g.current = 9; ctx.g.confirm = 3;
  save("gallery.ppm", [](gfx::Canvas& c, void* p) { Ctx* x = (Ctx*)p; drawToolbar(c, x->tools); drawGallery(c, x->g); }, &ctx);
  writeTest();
  printf("pictures: sketch.ppm gallery.ppm write.ppm write_low.ppm\n");
}
