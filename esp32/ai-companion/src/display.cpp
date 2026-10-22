#include "display.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace display {
namespace {

LGFX* lcd = nullptr;
Stats stats;
uint16_t* buffer = nullptr;
size_t capacity = 0;  // in pixels
uint16_t* target = nullptr;  // drawing into a picture instead of the screen
gfx::Rect targetArea{0, 0, 0, 0};

}  // namespace

bool begin(LGFX& l) {
  lcd = &l;
  lcd->setSwapBytes(true);  // our buffer holds plain RGB565
  const size_t sizes[] = {32768, 24576, 16384, 8192};
  for (size_t px : sizes) {
#if defined(ESP_PLATFORM)
    buffer = (uint16_t*)heap_caps_malloc(px * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    buffer = (uint16_t*)malloc(px * 2);
#endif
    if (buffer) {
      capacity = px;
      return true;
    }
  }
#if defined(ESP_PLATFORM)
  // Not enough internal RAM: slower, but still works.
  buffer = (uint16_t*)heap_caps_malloc(32768 * 2, MALLOC_CAP_SPIRAM);
  if (buffer) {
    capacity = 32768;
    return true;
  }
#endif
  return false;
}

void setTarget(uint16_t* picture, const gfx::Rect& area) {
  target = picture;
  targetArea = area;
}

bool drawingToPicture() { return target != nullptr; }

uint16_t* scratch(size_t& pixels) {
  pixels = capacity;
  return buffer;
}

void copyToScreen(const uint16_t* picture, int pictureW, int sx, int sy, int x, int y, int w, int h) {
  if (!lcd || w <= 0 || h <= 0) return;
  if (w == pictureW) {  // whole rows: one copy
    lcd->pushImage(x, y, w, h, picture + (size_t)sy * pictureW + sx);
    return;
  }
  for (int r = 0; r < h; ++r) lcd->pushImage(x, y + r, w, 1, picture + (size_t)(sy + r) * pictureW + sx);
}

Stats takeStats() {
  Stats s = stats;
  stats = Stats();
  return s;
}

namespace detail {

void renderBands(const gfx::Rect& want, const gfx::Color& bg, DrawFn fn, void* ctx) {
  if (!lcd || !buffer) return;
  const gfx::Rect area = gfx::intersect(want, target ? targetArea : gfx::Rect{0, 0, SCREEN_W, SCREEN_H});
  if (area.empty()) return;
  const int rowsPerBand = (int)(capacity / (size_t)area.w);
  if (rowsPerBand < 1) return;

  gfx::Canvas canvas;
  for (int y = area.y; y < area.bottom(); y += rowsPerBand) {
    const int h = gfx::imin(rowsPerBand, area.bottom() - y);
    const uint32_t t0 = micros();
    canvas.begin(buffer, area.w, h, area.x, y);
    canvas.clear(bg);
    fn(canvas, ctx);
    const uint32_t t1 = micros();
    if (target) {
      for (int r = 0; r < h; ++r)
        memcpy(target + (size_t)(y + r - targetArea.y) * targetArea.w + (area.x - targetArea.x),
               buffer + (size_t)r * area.w, area.w * 2);
      continue;
    }
    lcd->pushImage(area.x, y, area.w, h, buffer);
    stats.drawUs += t1 - t0;
    stats.pushUs += micros() - t1;
    stats.pixels += (uint32_t)area.w * h;
  }
}

}  // namespace detail
}  // namespace display
