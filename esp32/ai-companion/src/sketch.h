// The Sketch screen (swipe left from the Time screen): draw with a finger.
// Toolbar on the left (colours, eraser, brush size, undo, gallery), the paper
// on the right. Sideways swipes on the toolbar change screens; on the paper
// they draw.
//
// This part only draws, so it also runs in the PC preview. sketch_store.cpp
// saves and loads sketches; main.cpp handles touch.
#pragma once

#include <stdint.h>

#include "gfx.h"

namespace sketch {

// The paper, in screen coordinates. The drawing is a PAPER_W x PAPER_H
// RGB565 picture, row after row.
constexpr int PAPER_X = 112, PAPER_Y = 50, PAPER_W = 680, PAPER_H = 320;
constexpr gfx::Rect PAPER{PAPER_X, PAPER_Y, PAPER_W, PAPER_H};
constexpr gfx::Rect TOOLBAR{0, 44, 108, 332};
constexpr gfx::Rect AREA{0, 44, 800, 332};  // the whole screen part
constexpr int TOOLBAR_RIGHT = 108;          // swipes starting left of this change screens

constexpr gfx::Color PAPER_COLOR = gfx::rgb(0x141B24);
constexpr int COLOR_COUNT = 8;
extern const gfx::Color COLORS[COLOR_COUNT];
constexpr int SIZE_COUNT = 3;
extern const float SIZES[SIZE_COUNT];  // brush radius in pixels

struct Tools {
  int color = 0;
  bool eraser = false;
  int size = 1;
  bool canUndo = false;
  bool gallery = false;  // the gallery is open instead of the paper
};

// Draws the toolbar (call inside display::render).
void drawToolbar(gfx::Canvas& c, const Tools& t);

enum class Tool { None, Color, Eraser, Size, Undo, Gallery };
struct ToolTap {
  Tool what;
  int index;
  ToolTap(Tool w = Tool::None, int i = 0) : what(w), index(i) {}
};
ToolTap toolbarTap(int x, int y);

// Copies the drawing (or the part of it inside c) onto the screen canvas.
void drawPaper(gfx::Canvas& c, const uint16_t* picture);

// Draws one stroke piece from (x0, y0) to (x1, y1) (screen coordinates) into
// the picture. Returns the part of the screen that changed.
gfx::Rect stroke(uint16_t* picture, float x0, float y0, float x1, float y1, const gfx::Color& col, float radius);

// ---------------------------------------------------------------- gallery

constexpr int MAX_SKETCHES = 10;
constexpr int THUMB_W = 160, THUMB_H = 75;  // small pictures in the gallery

struct GalleryItem {
  uint32_t id;
  const uint16_t* thumb;  // THUMB_W x THUMB_H, or nullptr if it couldn't be read
};

struct Gallery {
  GalleryItem items[MAX_SKETCHES];
  int count = 0;
  uint32_t current = 0;  // id of the sketch on the paper
  int confirm = -1;      // item asking "Delete?"
};

void drawGallery(gfx::Canvas& c, const Gallery& g);

enum class GTap { None, New, Open, AskDelete, Delete, Keep, Close };
struct GalleryTap {
  GTap what;
  int index;
  GalleryTap(GTap w = GTap::None, int i = 0) : what(w), index(i) {}
};
GalleryTap galleryTap(int x, int y, const Gallery& g);

// ---------------------------------------------------------------- saving
// A sketch file: "GSK1", width, height (uint16 each), then runs of
// (count, colour) as uint16 pairs, little-endian. Drawings are mostly empty
// paper, so this is small (usually 10-60 KB instead of 435 KB).

// Packs rows [y0, y1) of the picture. out must hold at least (y1 - y0) * PAPER_W * 4
// bytes in the worst case; returns the bytes written. Runs don't cross row blocks.
size_t packRows(const uint16_t* picture, int y0, int y1, uint8_t* out);

// Unpacks a file piece by piece. Feed it all the bytes after the 8-byte header.
class Unpacker {
 public:
  explicit Unpacker(uint16_t* picture) : pic_(picture) {}
  // A thumbnail instead of the full picture: only every few pixels are kept
  static Unpacker forThumb(uint16_t* thumb);
  void feed(const uint8_t* data, size_t len);
  bool complete() const { return pos_ >= (uint32_t)PAPER_W * PAPER_H; }

 private:
  void put(uint16_t count, uint16_t color);
  uint16_t* pic_;
  bool thumb_ = false;
  uint32_t pos_ = 0;
  uint8_t carry_[4];
  int carried_ = 0;
};

}  // namespace sketch
