// The panel driver for the Waveshare ESP32-S3-Touch-LCD-7, using ESP-IDF's
// RGB LCD driver with two frame buffers in PSRAM that the LCD hardware reads
// directly (DMA). Swapping pictures is timed with the panel's VSYNC.
//
// (The driver's "bounce buffer" mode was tried first: the CPU then copies every
// picture to the panel ~40 times a second, which on this board starved Wi-Fi
// and couldn't keep up - the screen flickered and showed garbage.)
#if defined(ESP_PLATFORM)

#include "screen.h"

#include <Arduino.h>
#include <esp_cache.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#include "board.h"

namespace screen {
namespace {

constexpr int W = SCREEN_W, H = SCREEN_H;
constexpr int MAX_RECTS = 24;

esp_lcd_panel_handle_t panel = nullptr;
uint16_t* fb[2] = {nullptr, nullptr};
int shown = 0;    // the picture on the panel
int drawing = 1;  // the picture being prepared
// The panel's refreshes: counted in the VSYNC interrupt
volatile uint32_t vsyncCount = 0;
volatile int64_t lastVsyncUs = 0;
volatile int64_t periodUs = 29000;  // measured; about 34 refreshes a second at 14 MHz
uint32_t safeAfterVsync = 0;        // the old picture is free once vsyncCount reaches this
bool waitFlip = false;

// What changed in each frame, so the other picture can catch up
struct RectList {
  gfx::Rect r[MAX_RECTS];
  int n = 0;
  void add(const gfx::Rect& a) {
    for (int i = 0; i < n; ++i)
      if (gfx::intersect(r[i], a).w == a.w && gfx::intersect(r[i], a).h == a.h) return;  // already inside one
    if (n < MAX_RECTS) {
      r[n++] = a;
      return;
    }
    for (int i = 1; i < n; ++i) r[0] = gfx::unite(r[0], r[i]);  // too many: one big box
    r[0] = gfx::unite(r[0], a);
    n = 1;
  }
} lastFrame, thisFrame;

bool frameOpen = false;
bool haveCover = false;
gfx::Rect cover{0, 0, 0, 0};
Stats stats;

// Called by the driver (in an interrupt) at every VSYNC: a new refresh starts
bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  const int64_t now = esp_timer_get_time();
  const int64_t p = now - lastVsyncUs;
  if (p > 5000 && p < 100000) periodUs = (periodUs * 7 + p) / 8;
  lastVsyncUs = now;
  vsyncCount = vsyncCount + 1;
  return false;
}

// The LCD hardware reads PSRAM directly, past the CPU's cache: written pixels
// must be pushed out of the cache first.
void flushRows(const uint16_t* buf, const gfx::Rect& r) {
  const uint8_t* start = (const uint8_t*)(buf + (size_t)r.y * W + r.x);
  const uint8_t* end = (const uint8_t*)(buf + (size_t)(r.bottom() - 1) * W + r.right());
  esp_cache_msync((void*)start, end - start, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

bool contains(const gfx::Rect& outer, const gfx::Rect& in) {
  return in.x >= outer.x && in.y >= outer.y && in.right() <= outer.right() && in.bottom() <= outer.bottom();
}

void copyRect(uint16_t* to, const uint16_t* from, const gfx::Rect& r) {
  for (int y = r.y; y < r.bottom(); ++y) memcpy(to + (size_t)y * W + r.x, from + (size_t)y * W + r.x, r.w * 2);
  flushRows(to, r);
}

// Before drawing a new frame: wait until the last swap happened, then copy what
// changed last frame into the hidden picture, so it matches the screen.
void openFrame() {
  if (frameOpen) return;
  if (waitFlip) {
    const uint32_t t0 = micros();
    while ((int32_t)(vsyncCount - safeAfterVsync) < 0 && micros() - t0 < 100000) vTaskDelay(1);
    stats.waitUs += micros() - t0;
    waitFlip = false;
  }
  const uint32_t t0 = micros();
  for (int i = 0; i < lastFrame.n; ++i)
    if (!haveCover || !contains(cover, lastFrame.r[i])) copyRect(fb[drawing], fb[shown], lastFrame.r[i]);
  stats.syncUs += micros() - t0;
  lastFrame.n = 0;
  frameOpen = true;
}

struct InitResult {
  bool ok = false;
  SemaphoreHandle_t done = nullptr;
};

bool createPanel() {
  esp_lcd_rgb_panel_config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = 14 * 1000 * 1000;  // like 3.7: stable while Wi-Fi is busy
  cfg.timings.h_res = W;
  cfg.timings.v_res = H;
  cfg.timings.hsync_pulse_width = 4;
  cfg.timings.hsync_back_porch = 8;
  cfg.timings.hsync_front_porch = 8;
  cfg.timings.vsync_pulse_width = 4;
  cfg.timings.vsync_back_porch = 8;
  cfg.timings.vsync_front_porch = 8;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 2;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = 46;
  cfg.vsync_gpio_num = 3;
  cfg.de_gpio_num = 5;
  cfg.pclk_gpio_num = 7;
  cfg.disp_gpio_num = -1;
  // Blue 3-7, green 2-7, red 3-7 (bit 0 of a pixel is the first data line)
  const int pins[16] = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40};
  for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = pins[i];
  cfg.flags.fb_in_psram = 1;

  if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK) return false;
  esp_lcd_rgb_panel_event_callbacks_t cbs;
  memset(&cbs, 0, sizeof cbs);
  cbs.on_vsync = onVsync;
  if (esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr) != ESP_OK) return false;
  if (esp_lcd_panel_reset(panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK) return false;
  void* a = nullptr;
  void* b = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &a, &b) != ESP_OK) return false;
  fb[0] = (uint16_t*)a;
  fb[1] = (uint16_t*)b;
  memset(fb[0], 0, (size_t)W * H * 2);
  memset(fb[1], 0, (size_t)W * H * 2);
  flushRows(fb[0], gfx::Rect{0, 0, W, H});
  flushRows(fb[1], gfx::Rect{0, 0, W, H});
  return true;
}

void initTask(void* arg) {
  InitResult* r = (InitResult*)arg;
  r->ok = createPanel();
  xSemaphoreGive(r->done);
  vTaskDelete(nullptr);
}

}  // namespace

bool begin() {
  // The driver's interrupt runs on the core that creates it: core 0, next to Wi-Fi.
  InitResult r;
  r.done = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(initTask, "screen", 4096, &r, 5, nullptr, 0);
  xSemaphoreTake(r.done, portMAX_DELAY);
  vSemaphoreDelete(r.done);
  if (!r.ok) Serial.println("screen: the LCD driver didn't start");
  shown = 0;
  drawing = 1;
  return r.ok;
}

void willCover(const gfx::Rect& area) {
  if (frameOpen) return;  // too late, already prepared
  cover = area;
  haveCover = true;
  openFrame();
}

void write(int x, int y, int w, int h, const uint16_t* px, int stride) {
  if (!panel) return;
  const gfx::Rect r = gfx::intersect(gfx::Rect{x, y, w, h}, gfx::Rect{0, 0, W, H});
  if (r.empty()) return;
  openFrame();
  px += (size_t)(r.y - y) * stride + (r.x - x);
  for (int row = 0; row < r.h; ++row)
    memcpy(fb[drawing] + (size_t)(r.y + row) * W + r.x, px + (size_t)row * stride, r.w * 2);
  flushRows(fb[drawing], r);
  thisFrame.add(r);
}

void present() {
  if (!panel || !frameOpen) return;
  // Giving the driver one of its own frame buffers makes the LCD hardware jump
  // to it after the picture it's reading now (nothing is copied). If that
  // picture is nearly done, the hardware may already have read past the jump,
  // so it shows the old one once more: then the old one is only free after
  // two refreshes instead of one.
  esp_lcd_panel_draw_bitmap(panel, 0, 0, 1, 1, fb[drawing]);
  // Where the panel is now (read after asking, so a VSYNC in between only makes us wait longer)
  uint32_t count;
  int64_t last;
  do {
    count = vsyncCount;
    last = lastVsyncUs;
  } while (count != vsyncCount);
  const int64_t into = esp_timer_get_time() - last;
  safeAfterVsync = count + (into < periodUs * 80 / 100 ? 1 : 2);
  waitFlip = true;
  shown = drawing;
  drawing = 1 - drawing;
  lastFrame = thisFrame;
  thisFrame.n = 0;
  frameOpen = false;
  haveCover = false;
  stats.frames++;
}

Stats takeStats() {
  Stats s = stats;
  stats = Stats();
  return s;
}

}  // namespace screen

#endif  // ESP_PLATFORM
