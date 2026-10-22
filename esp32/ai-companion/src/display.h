// Puts gfx drawing on the screen.
//
// Shapes are drawn in a small buffer in the chip's fast internal RAM, one
// horizontal strip at a time (drawing in slow PSRAM pixel by pixel would be
// much slower), and each strip is copied into the screen's next picture
// (see screen.h). Nothing shows until screen::present().
#pragma once

#include <type_traits>

#include "board.h"
#include "gfx.h"

namespace display {

// Call once at startup, after screen::begin().
bool begin();

namespace detail {
using DrawFn = void (*)(gfx::Canvas&, void*);
void renderBands(const gfx::Rect& area, const gfx::Color& bg, DrawFn fn, void* ctx);
}  // namespace detail

// How long the last stretch of drawing took, split into its steps (microseconds).
// Reading it starts a new stretch. Used for the log.
struct Stats {
  uint32_t drawUs = 0;  // shapes and text
  uint32_t pushUs = 0;  // copying to the screen
  uint32_t pixels = 0;  // how many pixels were copied
};
Stats takeStats();

// Draw into a picture in memory instead of the screen (used to slide screens).
// The picture holds area.w * area.h pixels, row after row. Anything outside
// area is skipped. setTarget(nullptr, ...) goes back to drawing on the screen.
void setTarget(uint16_t* picture, const gfx::Rect& area);
bool drawingToPicture();

// About to redraw all of `area` (see screen::willCover)
void willCover(const gfx::Rect& area);

// Copies a rectangle of a picture to the screen. picture is pictureW pixels
// wide; (sx, sy) is where the rectangle starts in it.
void copyToScreen(const uint16_t* picture, int pictureW, int sx, int sy, int x, int y, int w, int h);

// The fast drawing buffer, for anyone who needs to put a picture together
// row by row (the screen slider). Holds `pixels` pixels.
uint16_t* scratch(size_t& pixels);

// Redraws a rectangle of the screen: fills it with bg, then runs draw(canvas)
// once for each strip. draw should draw everything that belongs in the
// rectangle (in screen coordinates); anything outside the strip is skipped.
template <class F>
void render(const gfx::Rect& area, const gfx::Color& bg, F&& draw) {
  using Fn = typename std::remove_reference<F>::type;
  detail::renderBands(
      area, bg, [](gfx::Canvas& c, void* ctx) { (*static_cast<Fn*>(ctx))(c); }, (void*)&draw);
}

}  // namespace display
