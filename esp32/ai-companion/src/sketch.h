// The Sketch screen (swipe left from the Time screen): notes and drawings.
// Toolbar on the left (Write / Draw, colours, eraser, size, undo, gallery,
// clear, new page), the paper on the right. Sideways swipes on the toolbar change screens; on the paper
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
constexpr int COLOR_COUNT = 4;
extern const gfx::Color COLORS[COLOR_COUNT];
constexpr int SIZE_COUNT = 3;
extern const float SIZES[SIZE_COUNT];        // Draw: brush radius in pixels
extern const float WRITE_SIZES[SIZE_COUNT];  // Write: pen radius on the page (the box shows it 3x bigger)

struct Tools {
  bool write = true;  // Write mode (box + lined page) or Draw mode (free drawing)
  int color = 0;
  bool eraser = false;
  int size = 1;
  bool canUndo = false;
  bool gallery = false;       // the gallery is open instead of the paper
  bool confirmClear = false;  // "Clear" asks "Sure?" first
};

// Draws the toolbar (call inside display::render).
void drawToolbar(gfx::Canvas& c, const Tools& t);

enum class Tool { None, Write, Draw, Color, Eraser, Size, Undo, Gallery, Clear, NewPage };
struct ToolTap {
  Tool what;
  int index;
  ToolTap(Tool w = Tool::None, int i = 0) : what(w), index(i) {}
};
ToolTap toolbarTap(int x, int y);

// Copies the drawing (or the part of it inside c) onto the screen canvas.
// lines: the faint lines of Write mode.
void drawPaper(gfx::Canvas& c, const uint16_t* picture, bool lines = false);

// Draws one stroke piece from (x0, y0) to (x1, y1) (screen coordinates) into
// the picture. Returns the part of the screen that changed.
gfx::Rect stroke(uint16_t* picture, float x0, float y0, float x1, float y1, const gfx::Color& col, float radius);

// ---------------------------------------------------------------- smooth strokes
// Touch points come in steps and wobble a little. The smoother calms them down
// and joins them with gentle curves; it hands back short straight pieces to draw.
class Smoother {
 public:
  static constexpr int MAX_OUT = 24;
  // Each call fills out[] with points; draw a line from the previous point
  // (the first one of a stroke: a dot) through all of them. Returns how many.
  int start(float x, float y, gfx::Pt* out);
  int add(float x, float y, gfx::Pt* out);
  int finish(gfx::Pt* out);  // the finger lifted: the last bit up to where it was

 private:
  int curve(const gfx::Pt& to, gfx::Pt* out);
  gfx::Pt raw_ = {0, 0}, f_ = {0, 0}, mid_ = {0, 0};
};

// ---------------------------------------------------------------- Write mode
// The bottom (or top) of the paper becomes a big writing box. What is written
// there lands on the page at a third of the size, at the writing spot, which
// then moves along like typing. The page has LINES lines.

constexpr int LINES = 10;
constexpr int LINE_H = 30;      // between lines, page pixels
constexpr int FIRST_LINE = 28;  // the first line, from the top of the paper
constexpr int MARGIN = 10;      // left edge of the writing, page pixels
constexpr float SCALE = 3.0f;   // the box is 3x bigger than the page
constexpr int BOX_H = 148;
constexpr int BOX_BASE = 108;   // the guide line, from the top of the box
constexpr int BOX_INK_X = PAPER_X + 14;  // box writing starts here on screen
constexpr int BOX_INK_RIGHT = PAPER_X + 580;  // ...and ends here (the buttons are to the right)
constexpr int BOX_SLIDE_X = PAPER_X + 400;    // writing past this: the box moves on as soon as the finger lifts
constexpr float SPACE = 12;     // the space button, page pixels
constexpr uint32_t PAUSE_MS = 900;  // the box moves on after this long without writing

struct Cursor {
  int line;
  float x;  // page pixels from the paper's left edge
  Cursor(int l = 0, float x_ = MARGIN) : line(l), x(x_) {}
};

enum class BoxButton { None, Space, NewLine };

class Writer {
 public:
  // page: the PAPER_W x PAPER_H picture. box: a PAPER_W x BOX_H picture for the big writing.
  void attach(uint16_t* page, uint16_t* box);

  bool boxOnTop() const { return cur_.line >= LINES / 2; }
  gfx::Rect boxRect() const;
  bool inBox(int x, int y) const;
  BoxButton buttonAt(int x, int y) const;
  bool hasInk() const { return inkRight_ >= 0; }
  const Cursor& cursor() const { return cur_; }
  // The writing spot moved to a line under the box: everything needs drawing again
  bool takeLayoutChange() {
    const bool r = flip_;
    flip_ = false;
    return r;
  }

  // Writing in the box. The changed parts of the screen are collected (see dirty()).
  void begin(float x, float y, const gfx::Color& col, int size);
  void move(float x, float y);
  // The finger lifted. Returns true if the box moved on (a word is done).
  bool end(uint32_t now);
  // Call often: moves on after a pause. Returns true when it did.
  bool tick(uint32_t now);
  // Finishes the word in the box (if any). Returns true when there was one.
  bool commit();
  void clearBox();  // forgets the box without touching the page (undo does that)

  void space();
  void newLine();
  void setCursor(const Cursor& c);
  // A tap on the page: the writing spot goes there
  void placeAt(int x, int y);
  // After loading a page: continue after the last writing
  void findCursor();
  // The page is full (newLine past the last line): start a new page
  bool takePageFull() {
    const bool r = full_;
    full_ = false;
    return r;
  }

  // Parts of the screen that changed since the last call
  int takeDirty(gfx::Rect* out, int max);

  // Draws the page, its lines, the writing spot and the box (call inside display::render).
  void draw(gfx::Canvas& c) const;

 private:
  void mark(const gfx::Rect& r);
  void markCursor();
  void ink(const gfx::Pt* pts, int n);
  gfx::Pt toPage(const gfx::Pt& p) const;
  int boxY() const { return boxOnTop() ? PAPER_Y : PAPER_Y + PAPER_H - BOX_H; }
  int baseline(int line) const { return FIRST_LINE + line * LINE_H; }

  uint16_t* page_ = nullptr;
  uint16_t* box_ = nullptr;
  Cursor cur_;
  Smoother sm_;
  gfx::Pt last_ = {0, 0};
  gfx::Color col_ = gfx::rgb(0xFFFFFF);
  float rPage_ = 1.3f;
  float inkRight_ = -1;  // right edge of the writing in the box (screen x), -1 = empty
  bool drawing_ = false;
  uint32_t liftedAt_ = 0;
  bool flip_ = false, full_ = false;
  gfx::Rect dirty_[6];
  int dirtyCount_ = 0;
};

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
