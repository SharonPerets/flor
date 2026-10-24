#include "todo_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bidi.h"
#include "gfont.h"

namespace todoscreen {
namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::Pt;
using gfx::Rect;
using gfx::rgb;

const Color TEXT = rgb(0xE8EEF4), MUTED = rgb(0x9AA7B5), FAINT = rgb(0x4A5766), ACCENT = rgb(0x4FE3D6),
            DARK = rgb(0x05201E), ROW_BG = rgb(0x0E141B), FRESH_BG = rgb(0x0F2A2A), BORDER = rgb(0x1F2935),
            RED = rgb(0xFF7A7A), STAR_C = rgb(0xFFC94D), KEY_BG = rgb(0x1A2430), KEY_DOWN = rgb(0x3A4A5E),
            SEL = rgb(0x2A3646), DANGER = rgb(0x5A2328);

const char* const TAB_NAMES[3] = {"Today", "Upcoming", "Done"};
const char* const WHEN_NAMES[WHEN_COUNT] = {"", "Today", "Tomorrow", "Next week", "Someday"};
const char* const ROW1 = "qwertyuiop";
const char* const ROW2 = "asdfghjkl";
const char* const ROW3 = "zxcvbnm";
const char* const SYM1 = "1234567890";
const char* const SYM2 = "-/:;()&@'";
const char* const SYM3 = ".,?!#%+";

constexpr float KW = 74, KH = 56;
constexpr float KSTEP = 78.8f;
constexpr Rect UNDO_CHIP{180, 350, 440, 44};
constexpr Rect VOICE_BTN{16, 410, 560, 60};
constexpr Rect TYPE_BTN{588, 410, 196, 60};
constexpr Rect KB_FIELD{16, 54, 768, 56};
constexpr Rect KB_CANCEL{16, 416, 200, 52};
constexpr Rect KB_VOICE{228, 416, 556, 52};
constexpr float KB_STAR_X = 760, KB_STAR_Y = 138;

float textW(const gfont::Font& f, const char* s) { return Canvas::textWidth(f, s); }

void centered(Canvas& c, const gfont::Font& f, const char* s, float cx, int base, const Color& col) {
  c.text(f, s, cx - textW(f, s) / 2, base, col);
}

bool inside(const Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.right() && y < r.bottom(); }

// Copies s into out, cut with "..." so it fits maxW
void fit(const gfont::Font& f, const char* s, float maxW, char* out, int size) {
  snprintf(out, size, "%s", s);
  if (textW(f, out) <= maxW) return;
  size_t n = strlen(out);
  while (n > 0) {
    n = bidi::dropLast(out, n);  // a whole letter at a time (Hebrew letters are 2 bytes)
    out[n] = 0;
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s...", out);
    if (textW(f, tmp) <= maxW) {
      snprintf(out, size, "%s", tmp);
      return;
    }
  }
}

void star(Canvas& c, float cx, float cy, float r, const Color& col, const Color& inner, bool filled) {
  Pt p[10];
  for (int i = 0; i < 10; ++i) {
    const float a = -1.5708f + i * 0.6283f, rr = (i & 1) ? r * 0.45f : r;
    p[i] = Pt{cx + rr * cosf(a), cy + rr * sinf(a)};
  }
  Pt in[5];
  for (int i = 0; i < 5; ++i) in[i] = p[i * 2 + 1];
  c.fillPolygon(in, 5, col);
  for (int i = 0; i < 5; ++i) {
    const Pt t[3] = {p[(i * 2 + 9) % 10], p[i * 2], p[i * 2 + 1]};
    c.fillPolygon(t, 3, col);
  }
  if (!filled) star(c, cx, cy + 0.6f, r * 0.55f, inner, inner, true);
}

void tick(Canvas& c, float cx, float cy, float s, const Color& col) {
  c.fillCapsule(cx - s, cy, cx - s * 0.3f, cy + s * 0.7f, 2.2f, col);
  c.fillCapsule(cx - s * 0.3f, cy + s * 0.7f, cx + s, cy - s * 0.6f, 2.2f, col);
}

void mic(Canvas& c, float x, float y, const Color& col) {
  c.fillCapsule(x, y - 8, x, y + 1, 5.0f, col);
  c.strokeArc(x, y, 9.5f, 2.2f, 90, 270, col);
  c.fillRect(x - 1.2f, y + 9, 2.4f, 6, col);
  c.fillRect(x - 5, y + 14, 10, 2.4f, col);
}

void keyboardIcon(Canvas& c, float x, float y, const Color& col, const Color& bg) {
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 4; ++j)
      c.fillRRect(x + j * 8, y + i * 7, 6, 5, 1.5f, (i == 2 && j > 0 && j < 3) ? bg : col);
  c.fillRRect(x + 8, y + 14, 14, 5, 1.5f, col);
}

// Tabs in the header, right-aligned: their left x and width
void segments(const View& v, float xs[3], float ws[3], char labels[3][20]) {
  float x = 784;
  for (int i = 2; i >= 0; --i) {
    if (i > 0 && v.counts[i] > 0) snprintf(labels[i], 20, "%s %d", TAB_NAMES[i], v.counts[i]);
    else snprintf(labels[i], 20, "%s", TAB_NAMES[i]);
    ws[i] = textW(gfont::SMALL, labels[i]) + 32;
    x -= ws[i];
    xs[i] = x;
    x -= 8;
  }
}

// The buttons of an open row, right to left: their x, width and what they do
int rowActions(const View& v, const Row& r, float xs[4], float ws[4], int acts[4]) {
  int list[4];
  int n = 0;
  if (r.done) {
    list[n++] = ACT_NOT_DONE;
    list[n++] = ACT_DELETE;
  } else {
    list[n++] = ACT_STAR;
    if (v.tab == todos::UPCOMING) list[n++] = ACT_TODAY;
    list[n++] = ACT_TOMORROW;
    list[n++] = ACT_DELETE;
  }
  float x = 776;
  for (int i = n - 1; i >= 0; --i) {
    const char* label = list[i] == ACT_STAR ? (r.star ? "Unstar" : "Star")
                        : list[i] == ACT_TODAY ? "Today"
                        : list[i] == ACT_TOMORROW ? "Tomorrow"
                        : list[i] == ACT_NOT_DONE ? "Not done" : "Delete";
    ws[i] = textW(gfont::SMALL, label) + 30;
    x -= ws[i];
    xs[i] = x;
    acts[i] = list[i];
    x -= 8;
  }
  return n;
}

const char* actionLabel(int act, bool starred) {
  switch (act) {
    case ACT_STAR: return starred ? "Unstar" : "Star";
    case ACT_TODAY: return "Today";
    case ACT_TOMORROW: return "Tomorrow";
    case ACT_NOT_DONE: return "Not done";
    default: return "Delete";
  }
}

void drawHeader(Canvas& c, const View& v) {
  const char* title = TAB_NAMES[v.tab];
  c.text(gfont::HEADLINE, title, 20, 90, TEXT);
  char sub[24] = "";
  if (v.tab == todos::TODAY) {
    if (v.counts[0] > 0) snprintf(sub, sizeof sub, "%d left", v.counts[0]);
  } else if (v.tab == todos::FINISHED) {
    snprintf(sub, sizeof sub, "last 7 days");
  }
  c.text(gfont::SMALL, sub, 30 + textW(gfont::HEADLINE, title), 88, MUTED);
  float xs[3], ws[3];
  char labels[3][20];
  segments(v, xs, ws, labels);
  for (int i = 0; i < 3; ++i) {
    const bool on = i == v.tab;
    c.fillRRect(xs[i], 60, ws[i], 36, 18, on ? ACCENT : BORDER);
    centered(c, gfont::SMALL, labels[i], xs[i] + ws[i] / 2, 84, on ? DARK : MUTED);
  }
}

void drawRow(Canvas& c, const View& v, int i) {
  const Row& r = v.rows[i];
  const float y = ROW_Y + i * ROW_STEP, cy = y + ROW_H / 2.0f;
  const bool open = v.openRow == i;
  const Color& bg = r.fresh ? FRESH_BG : (open ? SEL : ROW_BG);
  c.fillRRect(16, y, 768, ROW_H, 14, bg);
  if (r.fresh) c.fillRRect(16, y, 4, ROW_H, 2, ACCENT);
  if (r.done) {
    c.fillCircle(48, cy, 14, ACCENT);
    tick(c, 48, cy, 6.5f, DARK);
  } else {
    c.fillCircle(48, cy, 14, MUTED);
    c.fillCircle(48, cy, 11.6f, bg);
  }
  float right = 736;  // where the text must stop
  if (open) {
    float xs[4], ws[4];
    int acts[4];
    const int n = rowActions(v, r, xs, ws, acts);
    for (int k = 0; k < n; ++k) {
      const bool del = acts[k] == ACT_DELETE;
      c.fillRRect(xs[k], cy - 18, ws[k], 36, 18, del ? DANGER : BORDER);
      centered(c, gfont::SMALL, actionLabel(acts[k], r.star), xs[k] + ws[k] / 2, (int)cy + 6, del ? RED : TEXT);
    }
    right = xs[0] - 10;
  } else {
    if (!r.done) star(c, 756, cy, 13, r.star ? STAR_C : FAINT, bg, r.star);
    if (r.chip[0]) {
      const float w = textW(gfont::SMALL, r.chip) + 24;
      const Color& col = r.chipKind == 1 ? RED : r.chipKind == 2 ? ACCENT : MUTED;
      c.fillRRect(726 - w, cy - 14, w, 28, 14, BORDER);
      centered(c, gfont::SMALL, r.chip, 726 - w / 2, (int)cy + 6, col);
      right = 726 - w - 10;
    } else if (r.done) {
      right = 770;
    }
  }
  char text[todos::TEXT_MAX + 8];
  fit(gfont::NOTE, r.text, right - 78, text, sizeof text);
  // Hebrew tasks sit at the right end of their space
  const float tw = textW(gfont::NOTE, text);
  const float tx = bidi::isRtl(text) ? right - tw : 78;
  c.text(gfont::NOTE, text, tx, (int)cy + 7, r.done ? MUTED : TEXT);
  if (r.done) c.fillRect(tx, cy, tw, 1.5f, MUTED);
}

void drawEmpty(Canvas& c, const View& v) {
  const char* big;
  const char* small;
  bool happy = false;
  if (v.tab == todos::TODAY) {
    if (v.allDone) {
      big = "All done for today!";
      small = "Nice work. Time for a little break.";
      happy = true;
    } else {
      big = "Nothing on your list";
      small = "Tap Add by voice and say a task, like \"buy milk tomorrow\"";
    }
  } else if (v.tab == todos::UPCOMING) {
    big = "Nothing coming up";
    small = "Say a day with the task, like \"call mom on Friday\"";
  } else {
    big = "Nothing finished yet";
    small = "Tap the circle next to a task when it's done";
  }
  if (happy) {  // a little happy face
    c.fillCircle(400, 180, 34, ACCENT);
    c.fillRRect(386, 166, 8, 14, 4, DARK);
    c.fillRRect(406, 166, 8, 14, 4, DARK);
    c.strokeArc(400, 186, 15, 4, 25, 155, DARK);
  }
  centered(c, gfont::TITLE, big, 400, happy ? 254 : 236, happy ? ACCENT : TEXT);
  centered(c, gfont::SMALL, small, 400, happy ? 284 : 266, MUTED);
}

void drawList(Canvas& c, const View& v) {
  if (v.rowCount == 0) drawEmpty(c, v);
  for (int i = 0; i < v.rowCount; ++i) drawRow(c, v, i);
  if (v.total > ROWS) {  // a thin scroll bar on the right
    const float top = ROW_Y, h = ROWS * ROW_STEP - 4;
    c.fillRRect(790, top, 4, h, 2, BORDER);
    const float th = h * ROWS / v.total, ty = top + (h - th) * v.first / (v.total - ROWS);
    c.fillRRect(790, ty, 4, th, 2, MUTED);
  }
  if (v.toast[0]) {
    const Rect& u = UNDO_CHIP;
    c.fillRRect(u.x, u.y, u.w, u.h, u.h / 2.0f, SEL);
    char text[72];
    fit(gfont::SMALL, v.toast, u.w - (v.toastUndo ? 110 : 44), text, sizeof text);
    if (v.toastUndo) {
      c.textIn(gfont::SMALL, text, u.x + 22, u.w - 110, u.y + 28, TEXT);
      c.text(gfont::SMALL, "Undo", u.right() - 70, u.y + 28, ACCENT);
    } else {
      centered(c, gfont::SMALL, text, u.x + u.w / 2.0f, u.y + 28, TEXT);
    }
  }
}

void drawBottom(Canvas& c, const View& v) {
  const Rect& b = VOICE_BTN;
  if (v.voice != VOICE_IDLE) {
    c.fillRRect(16, b.y, 768, b.h, b.h / 2.0f, ACCENT);
    mic(c, 56, b.y + 28, DARK);
    const bool listening = v.voice == VOICE_LISTENING;
    c.text(gfont::SUBTITLE, listening ? "Listening..." : "Writing it down...", 82, b.y + 39, DARK);
    if (listening) {
      c.text(gfont::SMALL, "say a task, like \"buy milk tomorrow\"", 252, b.y + 36, DARK);
      centered(c, gfont::SMALL, "Stop", 730, b.y + 36, DARK);
    }
    return;
  }
  c.fillRRect(b.x, b.y, b.w, b.h, b.h / 2.0f, ACCENT);
  const float w = textW(gfont::SUBTITLE, "Add by voice") + 34, x = b.x + (b.w - w) / 2;
  mic(c, x + 8, b.y + 28, DARK);
  c.text(gfont::SUBTITLE, "Add by voice", x + 34, b.y + 39, DARK);
  const Rect& t = TYPE_BTN;
  c.fillRRect(t.x, t.y, t.w, t.h, t.h / 2.0f, BORDER);
  keyboardIcon(c, t.x + 38, t.y + 20, TEXT, BORDER);
  c.text(gfont::SUBTITLE, "Type", t.x + 88, t.y + 39, TEXT);
}

// ---- keyboard

char keyChar(const Keyboard& kb, int key) {
  const char* r1 = kb.symbols ? SYM1 : ROW1;
  const char* r2 = kb.symbols ? SYM2 : ROW2;
  const char* r3 = kb.symbols ? SYM3 : ROW3;
  char ch = 0;
  if (key >= 0 && key < 10) ch = r1[key];
  else if (key >= 10 && key < 19) ch = r2[key - 10];
  else if (key >= 20 && key < 27) ch = r3[key - 20];
  if (ch >= 'a' && ch <= 'z' && kb.shift) ch -= 32;
  return ch;
}

void drawKey(Canvas& c, const Keyboard& kb, int key) {
  const Rect r = keyRect(key);
  const bool down = kb.pressed == key;
  if (key == KEY_ADD) {
    c.fillRRect(r.x, r.y, r.w, r.h, 10, down ? rgb(0x8FF0E8) : ACCENT);
    centered(c, gfont::SUBTITLE, "Add task", r.x + r.w / 2.0f, r.y + 37, DARK);
    return;
  }
  const bool special = key == KEY_SHIFT || key == KEY_DELETE || key == KEY_SYMBOLS;
  const bool lit = key == KEY_SHIFT && kb.shift && !kb.symbols;
  c.fillRRect(r.x, r.y, r.w, r.h, 10, down ? KEY_DOWN : lit ? ACCENT : special ? SEL : KEY_BG);
  const float cx = r.x + r.w / 2.0f;
  if (key == KEY_SHIFT) {
    if (kb.symbols) return;  // no capitals among the signs
    centered(c, gfont::SMALL, "Shift", cx, r.y + 34, lit ? DARK : TEXT);
  } else if (key == KEY_DELETE) {
    centered(c, gfont::SMALL, "Delete", cx, r.y + 34, TEXT);
  } else if (key == KEY_SYMBOLS) {
    centered(c, gfont::SMALL, kb.symbols ? "ABC" : "123 ?!", cx, r.y + 34, TEXT);
  } else if (key == KEY_SPACE) {
    centered(c, gfont::SMALL, "space", cx, r.y + 34, MUTED);
  } else {
    const char s[2] = {keyChar(kb, key), 0};
    centered(c, gfont::SUBTITLE, s, cx, r.y + 37, TEXT);
  }
}

void whenChips(float xs[WHEN_COUNT], float ws[WHEN_COUNT]) {
  float x = 84;
  for (int i = 1; i < WHEN_COUNT; ++i) {
    ws[i] = textW(gfont::SMALL, WHEN_NAMES[i]) + 32;
    xs[i] = x;
    x += ws[i] + 8;
  }
}

void drawKeyboard(Canvas& c, const View& v) {
  const Keyboard& kb = v.kb;
  // the text being typed (the end of it, when it's long)
  const Rect& f = KB_FIELD;
  c.fillRRect(f.x, f.y, f.w, f.h, 14, ROW_BG);
  c.fillRRect(f.x, f.bottom() - 2, f.w, 2, 1, ACCENT);
  const char* shown = kb.text;
  while (*shown && textW(gfont::NOTE, shown) > f.w - 44) bidi::next(shown);
  if (kb.text[0]) {
    c.text(gfont::NOTE, shown, f.x + 18, f.y + 36, TEXT);
  } else {
    c.text(gfont::NOTE, "Type a task...", f.x + 18, f.y + 36, FAINT);
  }
  const float cx = f.x + 20 + (kb.text[0] ? textW(gfont::NOTE, shown) : 0);
  c.fillRect(cx, f.y + 14, 2, 28, ACCENT);

  c.text(gfont::SMALL, "When:", 20, 144, MUTED);
  float xs[WHEN_COUNT], ws[WHEN_COUNT];
  whenChips(xs, ws);
  for (int i = 1; i < WHEN_COUNT; ++i) {
    const bool on = kb.when == i;
    c.fillRRect(xs[i], 120, ws[i], 36, 18, on ? ACCENT : BORDER);
    centered(c, gfont::SMALL, WHEN_NAMES[i], xs[i] + ws[i] / 2, 144, on ? DARK : MUTED);
  }
  star(c, KB_STAR_X, KB_STAR_Y, 15, kb.star ? STAR_C : FAINT, rgb(0x05070A), kb.star);

  for (int k = 0; k < KEY_COUNT; ++k) drawKey(c, kb, k);

  const Rect& b = KB_CANCEL;
  c.fillRRect(b.x, b.y, b.w, b.h, b.h / 2.0f, BORDER);
  centered(c, gfont::SMALL, "Cancel", b.x + b.w / 2.0f, b.y + 32, TEXT);
  const Rect& vb = KB_VOICE;
  const bool busy = kb.voice != VOICE_IDLE;
  c.fillRRect(vb.x, vb.y, vb.w, vb.h, vb.h / 2.0f, busy ? ACCENT : BORDER);
  const char* label = kb.voice == VOICE_LISTENING ? "Listening... (tap to stop)"
                      : kb.voice == VOICE_THINKING ? "Writing it down..."
                      : kb.note && kb.note[0] ? kb.note : "Say it instead";
  const float w = textW(gfont::SMALL, label) + 28, x = vb.x + (vb.w - w) / 2;
  mic(c, x + 6, vb.y + 25, busy ? DARK : ACCENT);
  c.text(gfont::SMALL, label, x + 28, vb.y + 32, busy ? DARK : TEXT);
}

}  // namespace

Rect keyRect(int key) {
  if (key >= 0 && key < 10) return Rect((int)(4 + key * KSTEP), 168, (int)KW, (int)KH);
  if (key >= 10 && key < 19) return Rect((int)(43 + (key - 10) * KSTEP), 228, (int)KW, (int)KH);
  if (key >= 20 && key < 27) return Rect((int)(122 + (key - 20) * KSTEP), 288, (int)KW, (int)KH);
  switch (key) {
    case KEY_SHIFT: return Rect(4, 288, 114, (int)KH);
    case KEY_DELETE: return Rect(676, 288, 120, (int)KH);
    case KEY_SYMBOLS: return Rect(4, 348, 114, (int)KH);
    case KEY_SPACE: return Rect(122, 348, 430, (int)KH);
    case KEY_ADD: return Rect(556, 348, 240, (int)KH);
    default: return Rect();
  }
}

int keyAt(int x, int y) {
  for (int k = 0; k < KEY_COUNT; ++k) {
    const Rect r = gfx::inflate(keyRect(k), 2);  // the gaps count too
    if (inside(r, x, y)) return k;
  }
  return -1;
}

Press press(Keyboard& kb, int key) {
  const int len = (int)strlen(kb.text);
  if (key == KEY_ADD) return Press::Add;
  if (key == KEY_SHIFT) {
    if (kb.symbols) return Press::None;
    kb.shift = !kb.shift;
    return Press::Keys;
  }
  if (key == KEY_SYMBOLS) {
    kb.symbols = !kb.symbols;
    return Press::Keys;
  }
  const bool wasShift = kb.shift;
  if (key == KEY_DELETE) {
    if (!len) return Press::None;
    kb.text[bidi::dropLast(kb.text, len)] = 0;  // a whole letter
    kb.shift = len == 1;  // empty again: a capital first
    return kb.shift != wasShift ? Press::Keys : Press::Text;
  }
  char ch = key == KEY_SPACE ? ' ' : keyChar(kb, key);
  if (!ch || len >= todos::TEXT_MAX) return Press::None;
  if (ch == ' ' && (len == 0 || kb.text[len - 1] == ' ')) return Press::None;
  kb.text[len] = ch;
  kb.text[len + 1] = 0;
  // a capital after ". " or "? ", otherwise small letters
  kb.shift = ch == ' ' && len >= 1 && (kb.text[len - 1] == '.' || kb.text[len - 1] == '?' || kb.text[len - 1] == '!');
  return kb.shift != wasShift ? Press::Keys : Press::Text;
}

void fill(View& v, int32_t today, uint32_t freshId) {
  const todos::Task* list[todos::MAX_OPEN + todos::MAX_DONE];
  for (int s = 0; s < 3; ++s) v.counts[s] = todos::collect((todos::Section)s, today, list, todos::MAX_OPEN + todos::MAX_DONE);
  const int n = todos::collect((todos::Section)v.tab, today, list, todos::MAX_OPEN + todos::MAX_DONE);
  v.total = n;
  if (v.first > n - ROWS) v.first = n - ROWS;
  if (v.first < 0) v.first = 0;
  v.rowCount = 0;
  for (int i = v.first; i < n && v.rowCount < ROWS; ++i) {
    const todos::Task& t = *list[i];
    Row& r = v.rows[v.rowCount++];
    r.id = t.id;
    snprintf(r.text, sizeof r.text, "%s", t.text);
    todos::chipFor(t, today, r.chip, sizeof r.chip, r.chipKind);
    if (v.tab == todos::TODAY && strcmp(r.chip, "Today") == 0) r.chip[0] = 0;  // (it's the Today list)
    r.star = t.star();
    r.done = t.done();
    r.fresh = freshId && t.id == freshId;
  }
  if (v.openRow >= v.rowCount) v.openRow = -1;
}

void draw(Canvas& c, const View& v) {
  if (v.kb.open) {
    drawKeyboard(c, v);
    return;
  }
  drawHeader(c, v);
  drawList(c, v);
  drawBottom(c, v);
}

TapResult tap(int x, int y, const View& v) {
  if (v.kb.open) {
    const int k = keyAt(x, y);
    if (k >= 0) return TapResult(Tap::KbKey, k);
    if (y >= 116 && y < 164) {
      if (fabsf(x - KB_STAR_X) < 30) return TapResult(Tap::KbStar);
      float xs[WHEN_COUNT], ws[WHEN_COUNT];
      whenChips(xs, ws);
      for (int i = 1; i < WHEN_COUNT; ++i)
        if (x >= xs[i] - 4 && x < xs[i] + ws[i] + 4) return TapResult(Tap::KbWhen, i);
    }
    if (inside(gfx::inflate(KB_CANCEL, 4), x, y)) return TapResult(Tap::KbCancel);
    if (inside(gfx::inflate(KB_VOICE, 4), x, y)) return TapResult(Tap::KbVoice);
    return TapResult();
  }
  if (v.voice != VOICE_IDLE && y >= VOICE_BTN.y - 6) return TapResult(Tap::Voice);  // stop listening
  if (inside(gfx::inflate(VOICE_BTN, 4), x, y)) return TapResult(Tap::Voice);
  if (inside(gfx::inflate(TYPE_BTN, 4), x, y)) return TapResult(Tap::Type);
  if (v.toast[0] && inside(UNDO_CHIP, x, y)) return v.toastUndo ? TapResult(Tap::Undo) : TapResult(Tap::Elsewhere);
  if (y >= HEADER_AREA.y && y < HEADER_AREA.bottom()) {
    float xs[3], ws[3];
    char labels[3][20];
    segments(v, xs, ws, labels);
    for (int i = 0; i < 3; ++i)
      if (x >= xs[i] - 4 && x < xs[i] + ws[i] + 4) return TapResult(Tap::Tab, i);
    return TapResult(Tap::Elsewhere);
  }
  for (int i = 0; i < v.rowCount; ++i) {
    const int top = ROW_Y + i * ROW_STEP;
    if (y < top - 2 || y >= top + ROW_H + 2) continue;
    const Row& r = v.rows[i];
    if (x < 80) return TapResult(Tap::Check, i);
    if (v.openRow == i) {
      float xs[4], ws[4];
      int acts[4];
      const int n = rowActions(v, r, xs, ws, acts);
      for (int k = 0; k < n; ++k)
        if (x >= xs[k] - 4 && x < xs[k] + ws[k] + 4) return TapResult(Tap::Action, i, acts[k]);
      return TapResult(Tap::Row, i);
    }
    if (!r.done && x >= 736) return TapResult(Tap::Star, i);
    return TapResult(Tap::Row, i);
  }
  return TapResult(Tap::Elsewhere);
}

}  // namespace todoscreen
