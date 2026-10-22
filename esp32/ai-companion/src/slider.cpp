#include "slider.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

#include "display.h"

namespace slider {
namespace {

constexpr int W = AREA.w, H = AREA.h;
uint16_t* pics[2] = {nullptr, nullptr};

}  // namespace

bool begin() {
  for (auto& p : pics) {
#if defined(ESP_PLATFORM)
    p = (uint16_t*)heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM);
#else
    p = (uint16_t*)malloc((size_t)W * H * 2);
#endif
    if (!p) return false;
  }
  return true;
}

uint16_t* picture(int which) { return pics[which & 1]; }

void fill(int which, const gfx::Color& c) {
  uint16_t* p = pics[which & 1];
  if (!p) return;
  for (size_t i = 0, n = (size_t)W * H; i < n; ++i) p[i] = c.c;
}

void show(int offset) {
  if (!pics[0] || !pics[1]) return;
  if (offset > W) offset = W;
  if (offset < -W) offset = -W;
  size_t cap = 0;
  uint16_t* rows = display::scratch(cap);
  const int perBand = (int)(cap / W);
  if (!rows || perBand < 1) return;
  display::willCover(AREA);  // every pixel of AREA is drawn below

  // Which part of which picture goes where, along one row
  const uint16_t* left;
  const uint16_t* right;
  int leftFrom, split;  // the left picture starts at column leftFrom; the right one starts at screen x = split
  if (offset <= 0) {    // this screen on the left, moved left
    left = pics[0];
    right = pics[1];
    leftFrom = -offset;
    split = W + offset;
  } else {  // the other screen on the left
    left = pics[1];
    right = pics[0];
    leftFrom = W - offset;
    split = offset;
  }

  for (int y = 0; y < H; y += perBand) {
    const int n = gfx::imin(perBand, H - y);
    for (int r = 0; r < n; ++r) {
      uint16_t* out = rows + (size_t)r * W;
      const size_t src = (size_t)(y + r) * W;
      if (split > 0) memcpy(out, left + src + leftFrom, split * 2);
      if (split < W) memcpy(out + split, right + src, (W - split) * 2);
    }
    display::copyToScreen(rows, W, 0, 0, AREA.x, AREA.y + y, W, n);
  }
}

}  // namespace slider
