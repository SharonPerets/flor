#include "sketch.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfont.h"

namespace sketch {

const gfx::Color COLORS[COLOR_COUNT] = {
    gfx::rgb(0xEEF2F6), gfx::rgb(0xFF5A5A), gfx::rgb(0xFFD166), gfx::rgb(0x5AA9FF),
};
const float SIZES[SIZE_COUNT] = {1.5f, 3.5f, 8.0f};
const float WRITE_SIZES[SIZE_COUNT] = {0.9f, 1.3f, 1.8f};

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
const Color RULE = gfx::rgb(0x222C38);
const Color BOX_BG = gfx::rgb(0x0B1118);
const Color GUIDE = gfx::rgb(0x2F5F5C);

// Toolbar: Write and Draw on top, then two columns of round buttons, then Clear and New page
constexpr float COL_X[2] = {30, 78};
constexpr float ROW_Y[4] = {170, 222, 274, 326};
constexpr float CELL_R = 21;  // highlight behind a button
constexpr float PILL_X = 6, PILL_W = 96, PILL_H = 36;
constexpr float WRITE_Y = 56, DRAW_Y = 100, CLEAR_Y = 372, NEW_Y = 418;

// Write mode box buttons (x on screen, y from the top of the box)
constexpr float BTN_X = 700, BTN_W = 80, BTN_H = 36, SPACE_Y = 28, NEWLINE_Y = 78;

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

static void pill(Canvas& c, float y, const char* label, bool on, const Color& onColor = ACCENT) {
  c.fillRRect(PILL_X, y, PILL_W, PILL_H, PILL_H / 2, on ? onColor : BORDER);
  centered(c, gfont::SMALL, label, PILL_X + PILL_W / 2, (int)y + 24, on ? DARK_TEXT : TEXT);
}

void drawToolbar(Canvas& c, const Tools& t) {
  c.fillRect(TOOLBAR_RIGHT - 1, PAPER_Y, 1, PAPER_H, BORDER);
  pill(c, WRITE_Y, "Write", t.write);
  pill(c, DRAW_Y, "Draw", !t.write);
  for (int i = 0; i < COLOR_COUNT; ++i) {
    const float x = COL_X[i % 2], y = ROW_Y[i / 2];
    const bool on = !t.eraser && t.color == i && !t.gallery;
    if (on) c.fillCircle(x, y, CELL_R, SELECTED);
    c.fillCircle(x, y, on ? 14 : 12, COLORS[i]);
  }
  // eraser
  if (t.eraser && !t.gallery) c.fillCircle(COL_X[0], ROW_Y[2], CELL_R, SELECTED);
  eraserIcon(c, COL_X[0], ROW_Y[2] - 2, TEXT);
  // pen size: a dot as big as the pen
  const Color& dot = t.eraser ? MUTED : COLORS[t.color];
  const float r = t.write ? WRITE_SIZES[t.size] * 2.2f : SIZES[t.size];
  c.strokeArc(COL_X[1], ROW_Y[2], 16, 1.5f, 0, 360, BORDER);
  c.fillCircle(COL_X[1], ROW_Y[2], r < 2 ? 2 : (r > 11 ? 11 : r), dot);
  // undo and gallery
  undoIcon(c, COL_X[0], ROW_Y[3], t.canUndo && !t.gallery ? TEXT : BORDER);
  if (t.gallery) c.fillCircle(COL_X[1], ROW_Y[3], CELL_R, SELECTED);
  galleryIcon(c, COL_X[1], ROW_Y[3], t.gallery ? ACCENT : TEXT);
  // clear (asks first) and new page
  pill(c, CLEAR_Y, t.confirmClear ? "Sure?" : "Clear", t.confirmClear, DANGER);
  pill(c, NEW_Y, "New page", false);
}

ToolTap toolbarTap(int x, int y) {
  if (x >= TOOLBAR_RIGHT || y < 48) return ToolTap();
  const int col = x < 54 ? 0 : 1;
  if (y < 96) return ToolTap(Tool::Write);
  if (y < 142) return ToolTap(Tool::Draw);
  if (y < 196) return ToolTap(Tool::Color, col);
  if (y < 248) return ToolTap(Tool::Color, 2 + col);
  if (y < 300) return ToolTap(col ? Tool::Size : Tool::Eraser);
  if (y < 362) return ToolTap(col ? Tool::Gallery : Tool::Undo);
  if (y < 412) return ToolTap(Tool::Clear);
  if (y < 480) return ToolTap(Tool::NewPage);
  return ToolTap();
}

// ---------------------------------------------------------------- paper

void drawPaper(Canvas& c, const uint16_t* picture, bool lines) {
  outline(c, PAPER_X, PAPER_Y, PAPER_W, PAPER_H, 1, BORDER);
  if (picture) blit(c, picture, PAPER_W, PAPER_H, PAPER_X, PAPER_Y);
  else c.fillRect(PAPER_X, PAPER_Y, PAPER_W, PAPER_H, PAPER_COLOR);
  if (!lines) return;
  // faint lines, only on empty paper (writing stays on top)
  const Rect a = c.area();
  const int x0 = gfx::imax(a.x, PAPER_X + 8), x1 = gfx::imin(a.right(), PAPER_X + PAPER_W - 8);
  for (int i = 0; i < LINES; ++i) {
    const int y = PAPER_Y + FIRST_LINE + i * LINE_H + 3;
    if (y < a.y || y >= a.bottom() || x0 >= x1) continue;
    uint16_t* p = c.row(y) + (x0 - c.originX());
    for (int x = x0; x < x1; ++x, ++p)
      if (*p == PAPER_COLOR.c) *p = RULE.c;
  }
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

// ---------------------------------------------------------------- smooth strokes

namespace {
// A gentle curve from a to b that leans towards ctrl, as short straight pieces
int quad(const Pt& a, const Pt& ctrl, const Pt& b, Pt* out, int max) {
  const float len = hypotf(ctrl.x - a.x, ctrl.y - a.y) + hypotf(b.x - ctrl.x, b.y - ctrl.y);
  int n = (int)ceilf(len / 3.0f);
  if (n < 1) n = 1;
  if (n > max) n = max;
  for (int i = 1; i <= n; ++i) {
    const float t = (float)i / n, u = 1 - t;
    out[i - 1] = Pt{u * u * a.x + 2 * u * t * ctrl.x + t * t * b.x, u * u * a.y + 2 * u * t * ctrl.y + t * t * b.y};
  }
  return n;
}
}  // namespace

int Smoother::start(float x, float y, Pt* out) {
  raw_ = f_ = mid_ = Pt{x, y};
  out[0] = raw_;
  return 1;
}

int Smoother::add(float x, float y, Pt* out) {
  raw_ = Pt{x, y};
  const Pt nf{f_.x + (x - f_.x) * 0.6f, f_.y + (y - f_.y) * 0.6f};
  if (fabsf(nf.x - f_.x) + fabsf(nf.y - f_.y) < 1.0f) return 0;  // wait for a bigger move
  const Pt nm{(f_.x + nf.x) * 0.5f, (f_.y + nf.y) * 0.5f};
  const int n = quad(mid_, f_, nm, out, MAX_OUT);
  mid_ = nm;
  f_ = nf;
  return n;
}

int Smoother::finish(Pt* out) {
  const int n = quad(mid_, f_, raw_, out, MAX_OUT);
  mid_ = f_ = raw_;
  return n;
}

// ---------------------------------------------------------------- Write mode

void Writer::attach(uint16_t* page, uint16_t* box) {
  page_ = page;
  box_ = box;
  if (box_)
    for (size_t i = 0; i < (size_t)PAPER_W * BOX_H; ++i) box_[i] = BOX_BG.c;
  inkRight_ = -1;
  drawing_ = false;
}

Rect Writer::boxRect() const { return Rect{PAPER_X, boxY(), PAPER_W, BOX_H}; }

bool Writer::inBox(int x, int y) const {
  const Rect r = boxRect();
  return x >= r.x && x < r.right() && y >= r.y && y < r.bottom() && buttonAt(x, y) == BoxButton::None;
}

BoxButton Writer::buttonAt(int x, int y) const {
  const float by = (float)boxY();
  if (inside(x, y, BTN_X - 8, by + SPACE_Y - 7, BTN_W + 16, BTN_H + 7 + 7)) return BoxButton::Space;
  if (inside(x, y, BTN_X - 8, by + NEWLINE_Y - 7, BTN_W + 16, BTN_H + 7 + 14)) return BoxButton::NewLine;
  return BoxButton::None;
}

Pt Writer::toPage(const Pt& p) const {
  return Pt{PAPER_X + cur_.x + (p.x - BOX_INK_X) / SCALE,
            PAPER_Y + baseline(cur_.line) + (p.y - (boxY() + BOX_BASE)) / SCALE};
}

void Writer::mark(const Rect& r) {
  const Rect m = gfx::intersect(PAPER, r);
  if (m.empty()) return;
  if (dirtyCount_ < 6) dirty_[dirtyCount_++] = m;
  else dirty_[5] = gfx::unite(dirty_[5], m);
}

void Writer::markCursor() {
  mark(Rect{(int)(PAPER_X + cur_.x) - 8, PAPER_Y + baseline(cur_.line) - 34,
            (int)((BOX_INK_RIGHT - BOX_INK_X) / SCALE) + 18, 48});
}

int Writer::takeDirty(Rect* out, int max) {
  int n = 0;
  for (int i = 0; i < dirtyCount_ && n < max; ++i) out[n++] = dirty_[i];
  dirtyCount_ = 0;
  return n;
}

void Writer::ink(const Pt* pts, int n) {
  if (!page_ || !box_) return;
  Canvas bc, pc;
  bc.begin(box_, PAPER_W, BOX_H, PAPER_X, boxY());
  pc.begin(page_, PAPER_W, PAPER_H, PAPER_X, PAPER_Y);
  const float rb = rPage_ * SCALE;
  for (int i = 0; i < n; ++i) {
    const Pt& p = pts[i];
    bc.fillCapsule(last_.x, last_.y, p.x, p.y, rb, col_);
    const int m = (int)rb + 2;
    mark(gfx::intersect(boxRect(), Rect{(int)fminf(last_.x, p.x) - m, (int)fminf(last_.y, p.y) - m,
                                         (int)fabsf(p.x - last_.x) + 2 * m + 1, (int)fabsf(p.y - last_.y) + 2 * m + 1}));
    if (!typed_) {  // no PC: the handwriting goes straight onto the page
      const Pt a = toPage(last_), b = toPage(p);
      pc.fillCapsule(a.x, a.y, b.x, b.y, rPage_, col_);
      mark(Rect{(int)fminf(a.x, b.x) - 3, (int)fminf(a.y, b.y) - 3, (int)fabsf(b.x - a.x) + 7, (int)fabsf(b.y - a.y) + 7});
    }
    if (p.x + rb > inkRight_) inkRight_ = p.x + rb;
    last_ = p;
  }
}

void Writer::record(float x, float y, bool newStroke) {
  Word& w = word_;
  const int16_t px = (int16_t)lroundf(x - BOX_INK_X), py = (int16_t)lroundf(y - (boxY() + BOX_BASE));
  if (newStroke) {
    if (w.strokes >= WORD_STROKES || w.points >= WORD_POINTS) return;
    w.start[w.strokes++] = (uint16_t)w.points;
  } else {
    if (w.strokes == 0 || w.points >= WORD_POINTS) return;
    const int l = w.points - 1;
    if (l >= w.start[w.strokes - 1] && abs(w.x[l] - px) + abs(w.y[l] - py) < 2) return;  // hardly moved
  }
  w.x[w.points] = px;
  w.y[w.points] = py;
  ++w.points;
}

void Writer::begin(float x, float y, const Color& col, int size) {
  drawing_ = false;
  if (!hasInk()) {
    if (cur_.x > PAPER_W - 100) {  // no room left on this line
      newLine();
      if (full_) return;
    }
    typed_ = typedNext_;
    word_.points = word_.strokes = 0;
    word_.right = -1;
    clearChips();
  }
  col_ = col;
  rPage_ = WRITE_SIZES[size < 0 ? 0 : (size >= SIZE_COUNT ? SIZE_COUNT - 1 : size)];
  drawing_ = true;
  record(x, y, true);
  Pt pts[Smoother::MAX_OUT];
  sm_.start(x, y, pts);
  last_ = pts[0];
  ink(pts, 1);
}

void Writer::move(float x, float y) {
  if (!drawing_) return;
  record(x, y, false);
  Pt pts[Smoother::MAX_OUT];
  ink(pts, sm_.add(x, y, pts));
}

bool Writer::end(uint32_t now) {
  if (!drawing_) return false;
  Pt pts[Smoother::MAX_OUT];
  ink(pts, sm_.finish(pts));
  drawing_ = false;
  liftedAt_ = now;
  if (inkRight_ > BOX_SLIDE_X) return commit();
  return false;
}

bool Writer::tick(uint32_t now) {
  if (!hasInk() || drawing_ || (int32_t)(now - liftedAt_) < (int32_t)PAUSE_MS) return false;
  return commit();
}

bool Writer::commit() {
  if (!hasInk()) return false;
  if (typed_) {  // the word goes to the PC; typeWord / placeHandwriting puts it on the page
    word_.right = inkRight_ - BOX_INK_X;
    word_.color = col_;
    word_.radius = rPage_;
    wordReady_ = true;
    clearBox();
    return true;
  }
  markCursor();
  cur_.x += (inkRight_ - BOX_INK_X) / SCALE + 2;
  clearBox();
  if (cur_.x > PAPER_W - 40) newLine();
  markCursor();
  return true;
}

bool Writer::takeWord(Word& out) {
  if (!wordReady_) return false;
  wordReady_ = false;
  out = word_;
  return true;
}

// Moves to the next line when a piece w pixels wide doesn't fit any more. false = the page is full.
bool Writer::roomFor(float w) {
  if (cur_.x + w <= PAPER_W - MARGIN || cur_.x <= MARGIN + 1) return true;
  newLine();
  return !full_;
}

bool Writer::typeWord(const char* text, const Color& col) {
  if (!text || !*text) return true;
  const float w = Canvas::textWidth(gfont::NOTE, text);
  if (!roomFor(w)) return false;
  markCursor();
  if (page_) {
    Canvas pc;
    pc.begin(page_, PAPER_W, PAPER_H, PAPER_X, PAPER_Y);
    pc.text(gfont::NOTE, text, PAPER_X + cur_.x, PAPER_Y + baseline(cur_.line), col);
  }
  mark(Rect{(int)(PAPER_X + cur_.x) - 2, PAPER_Y + baseline(cur_.line) - 26, (int)w + 6, 36});
  cur_.x += w + Canvas::textWidth(gfont::NOTE, " ");
  markCursor();
  return true;
}

bool Writer::placeHandwriting(const Word& w) {
  if (w.points == 0) return true;
  const float width = w.right / SCALE + 2;
  if (!roomFor(width)) return false;
  markCursor();
  if (page_) {
    Canvas pc;
    pc.begin(page_, PAPER_W, PAPER_H, PAPER_X, PAPER_Y);
    const float ox = PAPER_X + cur_.x, oy = (float)(PAPER_Y + baseline(cur_.line));
    for (int s = 0; s < w.strokes; ++s) {
      const int a = w.start[s], b = s + 1 < w.strokes ? w.start[s + 1] : w.points;
      Smoother sm;
      Pt pts[Smoother::MAX_OUT];
      Pt last = {ox + w.x[a] / SCALE, oy + w.y[a] / SCALE};
      sm.start(last.x, last.y, pts);
      pc.fillCircle(last.x, last.y, w.radius, w.color);
      for (int i = a + 1; i <= b; ++i) {
        const int n = i < b ? sm.add(ox + w.x[i] / SCALE, oy + w.y[i] / SCALE, pts) : sm.finish(pts);
        for (int k = 0; k < n; ++k) {
          pc.fillCapsule(last.x, last.y, pts[k].x, pts[k].y, w.radius, w.color);
          last = pts[k];
        }
      }
    }
  }
  mark(Rect{(int)(PAPER_X + cur_.x) - 4, PAPER_Y + baseline(cur_.line) - 44, (int)width + 8, 64});
  cur_.x += width;
  markCursor();
  return true;
}

void Writer::setChips(const char* const* texts, int n) {
  chipCount_ = 0;
  for (int i = 0; i < n && chipCount_ < CHIPS; ++i) {
    if (!texts[i] || !*texts[i]) continue;
    snprintf(chips_[chipCount_], sizeof chips_[0], "%s", texts[i]);
    ++chipCount_;
  }
  mark(boxRect());
}

void Writer::clearChips() {
  if (!chipCount_) return;
  chipCount_ = 0;
  mark(boxRect());
}

constexpr float CHIP_Y = 8, CHIP_H = 32, CHIP_GAP = 10, CHIP_PAD = 16;

float Writer::chipX(int i) const {
  float x = BOX_INK_X + Canvas::textWidth(gfont::SMALL, "Or:") + 12;
  for (int k = 0; k < i; ++k) x += Canvas::textWidth(gfont::NOTE, chips_[k]) + 2 * CHIP_PAD + CHIP_GAP;
  return x;
}

int Writer::chipAt(int x, int y) const {
  const float by = (float)boxY();
  if (y < by + CHIP_Y - 6 || y >= by + CHIP_Y + CHIP_H + 8) return -1;
  for (int i = 0; i < chipCount_; ++i) {
    const float cx = chipX(i), w = Canvas::textWidth(gfont::NOTE, chips_[i]) + 2 * CHIP_PAD;
    if (x >= cx - 4 && x < cx + w + 4) return i;
  }
  return -1;
}

void Writer::clearBox() {
  if (box_)
    for (size_t i = 0; i < (size_t)PAPER_W * BOX_H; ++i) box_[i] = BOX_BG.c;
  inkRight_ = -1;
  drawing_ = false;
  mark(boxRect());
}

void Writer::space() {
  commit();
  clearChips();
  markCursor();
  cur_.x += SPACE;
  if (cur_.x > PAPER_W - 40) newLine();
  markCursor();
}

void Writer::newLine() {
  commit();
  clearChips();
  if (cur_.line + 1 >= LINES) {
    full_ = true;
    return;
  }
  setCursor(Cursor(cur_.line + 1, MARGIN));
}

void Writer::setCursor(const Cursor& c) {
  markCursor();
  const bool wasTop = boxOnTop();
  cur_ = c;
  if (boxOnTop() != wasTop) flip_ = true;
  markCursor();
}

void Writer::placeAt(int x, int y) {
  commit();
  int line = (y - PAPER_Y - (FIRST_LINE - 22)) / LINE_H;
  if (line < 0) line = 0;
  if (line >= LINES) line = LINES - 1;
  float px = (float)(x - PAPER_X);
  if (px < MARGIN) px = MARGIN;
  if (px > PAPER_W - 100) px = PAPER_W - 100;
  setCursor(Cursor(line, px));
}

void Writer::findCursor() {
  Cursor c;
  if (page_) {
    for (int line = LINES - 1; line >= 0; --line) {
      const int top = gfx::imax(0, baseline(line) - 24), bottom = gfx::imin(PAPER_H, baseline(line) + 12);
      int right = -1;
      for (int y = top; y < bottom; ++y) {
        const uint16_t* row = page_ + (size_t)y * PAPER_W;
        for (int x = PAPER_W - 1; x > right; --x)
          if (row[x] != PAPER_COLOR.c) {
            right = x;
            break;
          }
      }
      if (right < 0) continue;
      c = Cursor(line, right + 10.0f);
      if (c.x > PAPER_W - 100) c = line + 1 < LINES ? Cursor(line + 1, MARGIN) : Cursor(line, PAPER_W - 100);
      break;
    }
  }
  inkRight_ = -1;
  chipCount_ = 0;
  cur_ = c;
  flip_ = true;
}

void Writer::draw(Canvas& c) const {
  drawPaper(c, page_, true);
  // the writing spot
  const float mx = PAPER_X + cur_.x - 6, my = PAPER_Y + baseline(cur_.line) - 32;
  const float mw = (BOX_INK_RIGHT - BOX_INK_X) / SCALE + 12;
  c.fillRRect(mx, my, mw, 42, 6, ACCENT, 0.10f);
  c.fillRect(mx, my + 40, mw, 2, ACCENT);
  // the box
  const Rect r = boxRect();
  if (gfx::intersect(c.area(), r).empty()) return;
  c.fillRect(r.x, r.y, r.w, r.h, BOX_BG);
  c.fillRect(r.x, boxOnTop() ? r.bottom() - 2 : r.y, r.w, 2, ACCENT);
  if (chipCount_) {  // other guesses for the last word: tap one to swap it in
    c.text(gfont::SMALL, "Or:", BOX_INK_X, (int)(r.y + CHIP_Y + 22), MUTED);
    for (int i = 0; i < chipCount_; ++i) {
      const float x = chipX(i), w = Canvas::textWidth(gfont::NOTE, chips_[i]) + 2 * CHIP_PAD;
      c.fillRRect(x, r.y + CHIP_Y, w, CHIP_H, CHIP_H / 2, BORDER);
      c.text(gfont::NOTE, chips_[i], x + CHIP_PAD, (int)(r.y + CHIP_Y + 23), TEXT);
    }
  } else {
    c.text(gfont::SMALL, "Write here, big.", BOX_INK_X, r.y + 26, MUTED);
  }
  const float gy = (float)(r.y + BOX_BASE);
  for (float x = BOX_INK_X; x < BOX_INK_RIGHT; x += 14) c.fillRect(x, gy, 7, 2, GUIDE);
  for (float y = r.y + 36; y < r.bottom() - 8; y += 12) c.fillRect(BOX_SLIDE_X, y, 2, 6, GUIDE);
  // the big writing on top
  if (box_) {
    const Rect a = gfx::intersect(c.area(), r);
    for (int y = a.y; y < a.bottom(); ++y) {
      const uint16_t* src = box_ + (size_t)(y - r.y) * PAPER_W + (a.x - r.x);
      uint16_t* dst = c.row(y) + (a.x - c.originX());
      for (int x = 0; x < a.w; ++x)
        if (src[x] != BOX_BG.c) dst[x] = src[x];
    }
  }
  c.fillRRect(BTN_X, r.y + SPACE_Y, BTN_W, BTN_H, BTN_H / 2, BORDER);
  centered(c, gfont::SMALL, "space", BTN_X + BTN_W / 2, (int)(r.y + SPACE_Y + 24), TEXT);
  c.fillRRect(BTN_X, r.y + NEWLINE_Y, BTN_W, BTN_H, BTN_H / 2, BORDER);
  centered(c, gfont::SMALL, "new line", BTN_X + BTN_W / 2, (int)(r.y + NEWLINE_Y + 24), TEXT);
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

Unpacker Unpacker::forThumb(uint16_t* thumb, int rows) {
  Unpacker u(thumb, rows);
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
  const uint32_t total = (uint32_t)PAPER_W * rows_;
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
