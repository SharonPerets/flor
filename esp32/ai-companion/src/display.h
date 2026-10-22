// Puts gfx drawing on the real screen.
//
// The screen's picture memory (in slow PSRAM) is also being read non-stop to
// feed the display. Drawing straight into it makes the two fight, which can make
// parts of the picture jump sideways. So drawing happens in a small buffer in
// the chip's fast internal RAM, one horizontal strip at a time, and each strip
// is copied over to the screen once.
#pragma once

#include <type_traits>

#include "board.h"
#include "gfx.h"

namespace display {

// Call once at startup, after the screen is initialised.
bool begin(LGFX& lcd);

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
