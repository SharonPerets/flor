#include "display.h"

#include <stdlib.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace display {
namespace {

LGFX* lcd = nullptr;
Stats stats;
uint16_t* buffer = nullptr;
size_t capacity = 0;  // in pixels

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

Stats takeStats() {
  Stats s = stats;
  stats = Stats();
  return s;
}

namespace detail {

void renderBands(const gfx::Rect& want, const gfx::Color& bg, DrawFn fn, void* ctx) {
  if (!lcd || !buffer) return;
  const gfx::Rect area = gfx::intersect(want, gfx::Rect{0, 0, SCREEN_W, SCREEN_H});
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
    lcd->pushImage(area.x, y, area.w, h, buffer);
    stats.drawUs += t1 - t0;
    stats.pushUs += micros() - t1;
    stats.pixels += (uint32_t)area.w * h;
  }
}

}  // namespace detail
}  // namespace display
