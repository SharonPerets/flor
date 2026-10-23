#include "sketch.h"

#include <stdio.h>
#include <string.h>

#include "gfont.h"

namespace sketch {

const gfx::Color COLORS[COLOR_COUNT] = {
    gfx::rgb(0xEEF2F6), gfx::rgb(0xFF5A5A), gfx::rgb(0xFF9F43), gfx::rgb(0xFFD166),
    gfx::rgb(0x5DD879), gfx::rgb(0x4FE3D6), gfx::rgb(0x5AA9FF), gfx::rgb(0xB69CFF),
};
const float SIZES[SIZE_COUNT] = {2.5f, 5.5f, 11.0f};

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::Pt;
using gfx::Rect;

const Color BG = gfx::rgb(0x05070A);
const Color PANEL = gfx::rgb(0x10151C);
const Color BORDER = gfx::rgb(0x1F2935);
const Color TEXT = gfx::rgb(0xE8EEF4);
const Color MUTED = gfx::rgb(0x9AA7B5);
const Color ACCENT = gfx::rgb(0x4FE3D6);
const Color DARK_TEXT = gfx::rgb(0x05201E);
const Color DANGER = gfx::rgb(0xFF7A7A);
const Color SELECTED = gfx::rgb(0x2A3646);

// Toolbar: two columns of round buttons
constexpr float COL_X[2] = {30, 78};
constexpr float ROW_Y[6] = {76, 126, 176, 226, 282, 336};
constexpr float CELL_R = 22;  // highlight behind a button

// Gallery
constexpr float G_X = 120, G_Y = 98, G_GAP_X = 8, G_GAP_Y = 12;
constexpr float CLOSE_X = 636, CLOSE_Y = 56, CLOSE_W = 148, CLOSE_H = 34;
constexpr float DEL_R = 12;

float tileX(int i) { return G_X + (i % 4) * (THUMB_W + G_GAP_X); }
float tileY(int i) { return G_Y + (i / 4) * (THUMB_H + G_GAP_Y); }

void centered(Canvas& c, const gfont::Font& f, const char* s, float cx, int baseline, const Color& col) {
  c.text(f, s, cx - Canvas::textWidth(f, s) * 0.5f, baseline, col);
}

void eraserIcon(Canvas& c, float cx, float cy, const Color& col) {
  const Pt body[4] = {{cx - 11, cy + 3}, {cx + 1, cy - 9}, {cx + 11, cy + 1}, {cx - 1, cy + 13}};
  c.fillPolygon(body, 4, col, 1.0f, 2.0f);
  const Pt tip[4] = {{cx - 11, cy + 3}, {cx - 5, cy - 3}, {cx + 5, cy + 7}, {cx - 1, cy + 13}};
  c.fillPolygon(tip, 4, DANGER, 1.0f, 2.0f);
  c.fillCapsule(cx - 2, cy + 14, cx + 12, cy + 14, 1.2f, col);
}

void undoIcon(Canvas& c, float cx, float cy, const Color& col) {
  c.strokeArc(cx + 1, cy + 2, 9, 3, 180, 400, col);
  const Pt head[3] = {{cx - 15, cy + 1}, {cx - 2, cy + 1}, {cx - 8.5f, cy - 8}};
  c.fillPolygon(head, 3, col, 1.0f, 1.0f);
}

void galleryIcon(Canvas& c, float cx, float cy, const Color& col) {
  for (int i = 0; i < 4; ++i)
    c.fillRRect(cx - 11 + (i % 2) * 12, cy - 11 + (i / 2) * 12, 10, 10, 2.5f, col);
}

void plusIcon(Canvas& c, float cx, float cy, float s, const Color& col) {
  c.fillCapsule(cx - s, cy, cx + s, cy, 2.2f, col);
  c.fillCapsule(cx, cy - s, cx, cy + s, 2.2f, col);
}

void xMark(Canvas& c, float x, float y, float s, const Color& col) {
  c.fillCapsule(x - s, y - s, x + s, y + s, 1.5f, col);
  c.fillCapsule(x - s, y + s, x + s, y - s, 1.5f, col);
}

// Copies a picture (pw wide) placed at (x, y) on screen into the canvas
void blit(Canvas& c, const uint16_t* pic, int pw, int ph, int x, int y) {
  const Rect r = gfx::intersect(c.area(), Rect{x, y, pw, ph});
  if (r.empty()) return;
  for (int row = r.y; row < r.bottom(); ++row)
    memcpy(c.row(row) + (r.x - c.originX()), pic + (size_t)(row - y) * pw + (r.x - x), (size_t)r.w * 2);
}

void outline(Canvas& c, float x, float y, float w, float h, float t, const Color& col) {
  c.fillRect(x - t, y - t, w + 2 * t, t, col);
  c.fillRect(x - t, y + h, w + 2 * t, t, col);
  c.fillRect(x - t, y, t, h, col);
  c.fillRect(x + w, y, t, h, col);
}

bool inside(int x, int y, float bx, float by, float bw, float bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

}  // namespace

// ---------------------------------------------------------------- toolbar

void drawToolbar(Canvas& c, const Tools& t) {
  c.fillRect(TOOLBAR_RIGHT - 1, 50, 1, 320, BORDER);
  for (int i = 0; i < COLOR_COUNT; ++i) {
    const float x = COL_X[i % 2], y = ROW_Y[i / 2];
    const bool on = !t.eraser && t.color == i && !t.gallery;
    if (on) c.fillCircle(x, y, CELL_R, SELECTED);
    c.fillCircle(x, y, on ? 15 : 13, COLORS[i]);
  }
  // eraser
  if (t.eraser && !t.gallery) c.fillCircle(COL_X[0], ROW_Y[4], CELL_R, SELECTED);
  eraserIcon(c, COL_X[0], ROW_Y[4] - 2, TEXT);
  // brush size: a dot as big as the brush
  const Color& dot = t.eraser ? MUTED : COLORS[t.color];
  c.strokeArc(COL_X[1], ROW_Y[4], 17, 1.5f, 0, 360, BORDER);
  c.fillCircle(COL_X[1], ROW_Y[4], SIZES[t.size] < 3 ? 3 : SIZES[t.size], dot);
  // undo and gallery
  undoIcon(c, COL_X[0], ROW_Y[5], t.canUndo && !t.gallery ? TEXT : BORDER);
  if (t.gallery) c.fillCircle(COL_X[1], ROW_Y[5], CELL_R, SELECTED);
  galleryIcon(c, COL_X[1], ROW_Y[5], t.gallery ? ACCENT : TEXT);
}

ToolTap toolbarTap(int x, int y) {
  if (x >= TOOLBAR_RIGHT) return ToolTap();
  const int col = x < 54 ? 0 : 1;
  for (int row = 0; row < 6; ++row) {
    if (y < ROW_Y[row] - 26 || y >= ROW_Y[row] + 26) continue;
    if (row < 4) return ToolTap(Tool::Color, row * 2 + col);
    if (row == 4) return ToolTap(col ? Tool::Size : Tool::Eraser);
    return ToolTap(col ? Tool::Gallery : Tool::Undo);
  }
  return ToolTap();
}

// ---------------------------------------------------------------- paper

void drawPaper(Canvas& c, const uint16_t* picture) {
  outline(c, PAPER_X, PAPER_Y, PAPER_W, PAPER_H, 1, BORDER);
  if (picture) blit(c, picture, PAPER_W, PAPER_H, PAPER_X, PAPER_Y);
  else c.fillRect(PAPER_X, PAPER_Y, PAPER_W, PAPER_H, PAPER_COLOR);
}

Rect stroke(uint16_t* picture, float x0, float y0, float x1, float y1, const Color& col, float radius) {
  Canvas pc;
  pc.begin(picture, PAPER_W, PAPER_H, PAPER_X, PAPER_Y);
  pc.fillCapsule(x0, y0, x1, y1, radius, col);
  const int m = (int)radius + 2;
  const int left = (int)(x0 < x1 ? x0 : x1) - m, top = (int)(y0 < y1 ? y0 : y1) - m;
  const int right = (int)(x0 > x1 ? x0 : x1) + m + 1, bottom = (int)(y0 > y1 ? y0 : y1) + m + 1;
  return gfx::intersect(PAPER, Rect{left, top, right - left, bottom - top});
}

// ---------------------------------------------------------------- gallery

void drawGallery(Canvas& c, const Gallery& g) {
  c.text(gfont::TITLE, "Your sketches", PAPER_X + 8, 80, TEXT);
  char buf[24];
  snprintf(buf, sizeof buf, "%d of %d", g.count, MAX_SKETCHES);
  c.text(gfont::SMALL, buf, PAPER_X + 8 + Canvas::textWidth(gfont::TITLE, "Your sketches") + 14, 80, MUTED);
  c.fillRRect(CLOSE_X, CLOSE_Y, CLOSE_W, CLOSE_H, CLOSE_H / 2, BORDER);
  centered(c, gfont::SMALL, "Back to drawing", CLOSE_X + CLOSE_W / 2, (int)CLOSE_Y + 23, TEXT);

  // tile 0: a new page
  {
    const float x = tileX(0), y = tileY(0);
    c.fillRRect(x, y, THUMB_W, THUMB_H, 10, BORDER);
    c.fillRRect(x + 1, y + 1, THUMB_W - 2, THUMB_H - 2, 9, PANEL);
    plusIcon(c, x + THUMB_W / 2, y + 28, 10, ACCENT);
    centered(c, gfont::SMALL, "New page", x + THUMB_W / 2, (int)y + 62, TEXT);
  }
  for (int i = 0; i < g.count && i < MAX_SKETCHES; ++i) {
    const float x = tileX(i + 1), y = tileY(i + 1);
    const GalleryItem& it = g.items[i];
    const bool current = it.id == g.current;
    outline(c, x, y, THUMB_W, THUMB_H, current ? 3 : 1, current ? ACCENT : BORDER);
    if (it.thumb) blit(c, it.thumb, THUMB_W, THUMB_H, (int)x, (int)y);
    else c.fillRect(x, y, THUMB_W, THUMB_H, PAPER_COLOR);
    if (i == g.confirm) {
      c.fillRect(x, y, THUMB_W, THUMB_H, BG, 0.8f);
      centered(c, gfont::SMALL, "Delete it?", x + THUMB_W / 2, (int)y + 24, TEXT);
      c.fillRRect(x + 8, y + 36, 68, 30, 15, DANGER);
      centered(c, gfont::SMALL, "Delete", x + 42, (int)y + 56, DARK_TEXT);
      c.fillRRect(x + 84, y + 36, 68, 30, 15, BORDER);
      centered(c, gfont::SMALL, "Keep", x + 118, (int)y + 56, TEXT);
      continue;
    }
    const float dx = x + THUMB_W - 14, dy = y + 14;
    c.fillCircle(dx, dy, DEL_R + 1, BG, 0.6f);
    c.fillCircle(dx, dy, DEL_R, BORDER);
    xMark(c, dx, dy, 4, TEXT);
  }
  if (g.count == 0) c.text(gfont::SMALL, "Nothing saved yet: your drawings are kept here by themselves.", PAPER_X + 8,
                           (int)(tileY(4) + 40), MUTED);
}

GalleryTap galleryTap(int x, int y, const Gallery& g) {
  if (inside(x, y, CLOSE_X - 6, CLOSE_Y - 6, CLOSE_W + 12, CLOSE_H + 12)) return GalleryTap(GTap::Close);
  if (g.confirm >= 0) {
    const float tx = tileX(g.confirm + 1), ty = tileY(g.confirm + 1);
    if (inside(x, y, tx, ty + 30, 80, THUMB_H - 26)) return GalleryTap(GTap::Delete, g.confirm);
    return GalleryTap(GTap::Keep, g.confirm);  // anything else: never mind
  }
  if (inside(x, y, tileX(0), tileY(0), THUMB_W, THUMB_H)) return GalleryTap(GTap::New);
  for (int i = 0; i < g.count && i < MAX_SKETCHES; ++i) {
    const float tx = tileX(i + 1), ty = tileY(i + 1);
    const float dx = x - (tx + THUMB_W - 14), dy = y - (ty + 14);
    if (dx * dx + dy * dy <= (DEL_R + 12) * (DEL_R + 12)) return GalleryTap(GTap::AskDelete, i);
    if (inside(x, y, tx, ty, THUMB_W, THUMB_H)) return GalleryTap(GTap::Open, i);
  }
  return GalleryTap();
}

// ---------------------------------------------------------------- saving

size_t packRows(const uint16_t* picture, int y0, int y1, uint8_t* out) {
  const uint16_t* p = picture + (size_t)y0 * PAPER_W;
  const size_t n = (size_t)(y1 - y0) * PAPER_W;
  size_t o = 0;
  for (size_t i = 0; i < n;) {
    const uint16_t col = p[i];
    size_t run = 1;
    while (i + run < n && run < 65535 && p[i + run] == col) ++run;
    out[o++] = (uint8_t)run;
    out[o++] = (uint8_t)(run >> 8);
    out[o++] = (uint8_t)col;
    out[o++] = (uint8_t)(col >> 8);
    i += run;
  }
  return o;
}

Unpacker Unpacker::forThumb(uint16_t* thumb) {
  Unpacker u(thumb);
  u.thumb_ = true;
  return u;
}

void Unpacker::feed(const uint8_t* data, size_t len) {
  while (len > 0) {
    if (carried_ > 0 || len < 4) {
      while (carried_ < 4 && len > 0) {
        carry_[carried_++] = *data++;
        --len;
      }
      if (carried_ < 4) return;
      put((uint16_t)(carry_[0] | carry_[1] << 8), (uint16_t)(carry_[2] | carry_[3] << 8));
      carried_ = 0;
      continue;
    }
    put((uint16_t)(data[0] | data[1] << 8), (uint16_t)(data[2] | data[3] << 8));
    data += 4;
    len -= 4;
  }
}

void Unpacker::put(uint16_t count, uint16_t color) {
  const uint32_t total = (uint32_t)PAPER_W * PAPER_H;
  uint32_t end = pos_ + count;
  if (end > total) end = total;
  if (!thumb_) {
    for (uint32_t i = pos_; i < end; ++i) pic_[i] = color;
    pos_ = end;
    return;
  }
  // thumbnail: keep the pixels that land on the small grid
  while (pos_ < end) {
    const int y = (int)(pos_ / PAPER_W), xa = (int)(pos_ % PAPER_W);
    const uint32_t rowEnd = (uint32_t)(y + 1) * PAPER_W;
    const int xb = (int)((end < rowEnd ? end : rowEnd) - (uint32_t)y * PAPER_W);
    const int ty = (y * THUMB_H + PAPER_H - 1) / PAPER_H;
    if (ty < THUMB_H && ty * PAPER_H / THUMB_H == y) {
      for (int tx = (xa * THUMB_W + PAPER_W - 1) / PAPER_W; tx < THUMB_W && tx * PAPER_W / THUMB_W < xb; ++tx)
        pic_[ty * THUMB_W + tx] = color;
    }
    pos_ = (uint32_t)y * PAPER_W + xb;
  }
}

}  // namespace sketch
