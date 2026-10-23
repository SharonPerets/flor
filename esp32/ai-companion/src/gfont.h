// Smooth (anti-aliased) fonts, made by tools/make_fonts.py from Nunito.
#pragma once

#include <stdint.h>

namespace gfont {

struct Glyph {
  uint32_t offset;  // where this letter's 8-bit coverage bitmap starts in Font::alpha
  uint8_t w, h;     // bitmap size (0 for a space)
  int8_t xoff;      // bitmap left edge, relative to the pen position
  int8_t yoff;      // bitmap top edge, relative to the baseline (negative = above it)
  uint16_t adv16;   // how far the pen moves, in 1/16 pixel
};

struct Font {
  const Glyph* glyphs;
  const uint8_t* alpha;
  uint8_t first, last;  // first and last character in the table
  uint8_t ascent;       // baseline distance from the top of a line
  uint8_t height;       // line height
};

extern const Font SUBTITLE;  // 26 px bold
extern const Font TITLE;     // 22 px extra bold
extern const Font SMALL;     // 16 px bold
extern const Font Z_BIG;     // only the letter Z
extern const Font Z_SMALL;   // only the letter Z
extern const Font BIG;       // 76 px, only - and digits
extern const Font HEADLINE;  // 34 px extra bold

}  // namespace gfont
