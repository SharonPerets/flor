// The panel driver for the Waveshare ESP32-S3-Touch-LCD-7, using ESP-IDF's
// RGB LCD driver with two frame buffers in PSRAM and bounce buffers in SRAM.
#if defined(ESP_PLATFORM)

#include "screen.h"

#include <Arduino.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
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
SemaphoreHandle_t flipDone = nullptr;
volatile bool flipWanted = false;  // a swap was asked for and hasn't happened yet
bool waitFlip = false;             // must wait for that swap before drawing again

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

// Called by the driver (in an interrupt) when the panel starts reading a new picture
bool IRAM_ATTR onFrameStart(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  if (!flipWanted) return false;
  flipWanted = false;
  BaseType_t woke = pdFALSE;
  xSemaphoreGiveFromISR(flipDone, &woke);
  return woke == pdTRUE;
}

bool contains(const gfx::Rect& outer, const gfx::Rect& in) {
  return in.x >= outer.x && in.y >= outer.y && in.right() <= outer.right() && in.bottom() <= outer.bottom();
}

void copyRect(uint16_t* to, const uint16_t* from, const gfx::Rect& r) {
  for (int y = r.y; y < r.bottom(); ++y) memcpy(to + (size_t)y * W + r.x, from + (size_t)y * W + r.x, r.w * 2);
}

// Before drawing a new frame: wait until the last swap happened, then copy what
// changed last frame into the hidden picture, so it matches the screen.
void openFrame() {
  if (frameOpen) return;
  if (waitFlip) {
    const uint32_t t0 = micros();
    xSemaphoreTake(flipDone, pdMS_TO_TICKS(100));
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
  cfg.timings.pclk_hz = 16 * 1000 * 1000;
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
  cfg.bounce_buffer_size_px = W * 10;  // two buffers of 10 lines in fast RAM
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
  cbs.on_frame_buf_complete = onFrameStart;
  if (esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr) != ESP_OK) return false;
  if (esp_lcd_panel_reset(panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK) return false;
  void* a = nullptr;
  void* b = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &a, &b) != ESP_OK) return false;
  fb[0] = (uint16_t*)a;
  fb[1] = (uint16_t*)b;
  memset(fb[0], 0, (size_t)W * H * 2);
  memset(fb[1], 0, (size_t)W * H * 2);
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
  flipDone = xSemaphoreCreateBinary();
  // The driver's interrupt (which keeps the panel fed) runs on the core that
  // creates it. Core 0 is used, so it doesn't slow down Gorgi's drawing on core 1.
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
  thisFrame.add(r);
}

void present() {
  if (!panel || !frameOpen) return;
  // Giving the driver one of its own frame buffers makes it switch to that
  // one at the start of the next refresh (it copies nothing).
  esp_lcd_panel_draw_bitmap(panel, 0, 0, W, H, fb[drawing]);
  flipWanted = true;  // set after: if the switch just happened, we wait one extra refresh (safe)
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
