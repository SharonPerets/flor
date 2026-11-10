#include "music_browse.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bidi.h"
#include "gfont.h"

namespace browse {

const char* const MOODS[MOOD_COUNT] = {"Energize", "Relax", "Feel good", "Workout", "Focus", "Party", "Sleep"};

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::Pt;
using gfx::Rect;
using gfx::rgb;

const Color TEXT = rgb(0xE8EEF4), MUTED = rgb(0x9AA7B5), FAINT = rgb(0x5A6776), ACCENT = rgb(0x4FE3D6),
            ACCENT_DOWN = rgb(0x8FF0E8), DARK = rgb(0x05201E), PANEL = rgb(0x10151C), BTN = rgb(0x1C2733),
            BTN_DOWN = rgb(0x34465A), ROW_DOWN = rgb(0x16202B), KEY_BG = rgb(0x1A2430), SEL = rgb(0x2A3646);

const char* const ROW1 = "qwertyuiop";
const char* const ROW2 = "asdfghjkl";
const char* const ROW3 = "zxcvbnm";
const char* const SYM1 = "1234567890";
const char* const SYM2 = "-/:;()&@'";
const char* const SYM3 = ".,?!#%+";
constexpr float KW = 74, KH = 56, KSTEP = 78.8f;

// Top row
constexpr Rect SWITCH{16, 52, 232, 42};
constexpr Rect RADIO_BTN{500, 52, 148, 42};
constexpr Rect LYRICS_BTN{660, 52, 124, 42};
constexpr Rect SEARCH_BAR{262, 52, 466, 42};
constexpr float MIC_X = 758, MIC_Y = 73, MIC_R = 22;
constexpr Rect BACK_BTN{262, 52, 100, 42};
// Liked songs header (home)
constexpr Rect HOME_PLAY{596, 154, 88, 32};
constexpr Rect HOME_SHUFFLE{692, 154, 92, 32};
constexpr int HOME_ROW_Y = 192, PAGE_ROW_Y = 154;
// Keyboard
constexpr Rect KB_FIELD{16, 54, 768, 56};
constexpr Rect KB_CANCEL{16, 416, 200, 52};
constexpr Rect KB_VOICE{228, 416, 556, 52};

enum Btn { B_SWITCH_PLAYING, B_SWITCH_BROWSE, B_RADIO, B_LYRICS, B_MIC, B_BACK, B_PLAY, B_SHUFFLE, B_MOOD0 = 20 };

float textW(const gfont::Font& f, const char* s) { return Canvas::textWidth(f, s); }
bool inside(const Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.right() && y < r.bottom(); }
void centered(Canvas& c, const gfont::Font& f, const char* s, float cx, int base, const Color& col) {
  c.text(f, s, cx - textW(f, s) / 2, base, col);
}

// Copies s into out, cut (letter-safe) with "..." so it fits maxW
void fit(const gfont::Font& f, const char* s, float maxW, char* out, int size) {
  snprintf(out, size, "%s", s);
  if (textW(f, out) <= maxW) return;
  size_t n = strlen(out);
  char tmp[160];
  while (n > 0) {
    n = bidi::dropLast(out, n);
    out[n] = 0;
    while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
    snprintf(tmp, sizeof tmp, "%s...", out);
    if (textW(f, tmp) <= maxW) break;
  }
  snprintf(out, size, "%s", tmp);
}

// Text that may be Hebrew: right-aligned in its box when it reads right to left
void line(Canvas& c, const gfont::Font& f, const char* s, float x, float w, int base, const Color& col) {
  char buf[160];
  fit(f, s, w, buf, sizeof buf);
  c.textIn(f, buf, x, w, base, col);
}

void pill(Canvas& c, const Rect& r, const char* label, bool lit, bool down) {
  c.fillRRect(r.x, r.y, r.w, r.h, r.h / 2.0f, lit ? (down ? ACCENT_DOWN : ACCENT) : (down ? BTN_DOWN : BTN));
  centered(c, gfont::SMALL, label, r.x + r.w / 2.0f, r.y + r.h / 2 + 6, lit ? DARK : TEXT);
}

void mic(Canvas& c, float x, float y, const Color& col) {
  c.fillCapsule(x, y - 8, x, y + 1, 5.0f, col);
  c.strokeArc(x, y, 9.5f, 2.2f, 90, 270, col);
  c.fillRect(x - 1.2f, y + 9, 2.4f, 6, col);
  c.fillRect(x - 5, y + 14, 10, 2.4f, col);
}

void magnifier(Canvas& c, float x, float y, const Color& col) {
  c.strokeArc(x, y, 7, 2.4f, 0, 360, col);
  c.fillCapsule(x + 5, y + 5, x + 10, y + 10, 1.4f, col);
}

void note(Canvas& c, float cx, float cy, float s, const Color& col) {
  c.fillCircle(cx - 4 * s, cy + 5 * s, 3 * s, col);
  c.fillCapsule(cx - 1.2f * s, cy - 7 * s, cx - 1.2f * s, cy + 5 * s, 0.9f * s, col);
  c.fillCapsule(cx - 1.2f * s, cy - 7 * s, cx + 4 * s, cy - 4 * s, 0.9f * s, col);
}

void shuffleIcon(Canvas& c, float x, float y, const Color& col) {
  c.fillCapsule(x - 8, y + 5, x + 6, y - 5, 1.3f, col);
  c.fillCapsule(x - 8, y - 5, x - 2, y - 0.5f, 1.3f, col);
  c.fillCapsule(x + 2, y + 2, x + 6, y + 5, 1.3f, col);
  const Pt a[3] = {{x + 9, y - 5}, {x + 4, y - 9}, {x + 4, y - 1}};
  const Pt b[3] = {{x + 9, y + 5}, {x + 4, y + 1}, {x + 4, y + 9}};
  c.fillPolygon(a, 3, col);
  c.fillPolygon(b, 3, col);
}

void radioIcon(Canvas& c, float x, float y, const Color& col) {
  c.fillCircle(x, y, 3, col);
  c.strokeArc(x, y, 8, 2, 220, 320, col);
  c.strokeArc(x, y, 8, 2, 40, 140, col);
  c.strokeArc(x, y, 13, 2, 225, 315, col);
  c.strokeArc(x, y, 13, 2, 45, 135, col);
}

void playIcon(Canvas& c, float x, float y, float r, const Color& col) {
  const Pt t[3] = {{x - r * 0.6f, y - r}, {x - r * 0.6f, y + r}, {x + r, y}};
  c.fillPolygon(t, 3, col, 1, 1.2f);
}

// A pill with an icon in front of its label
void iconPill(Canvas& c, const Rect& r, const char* label, int icon, bool lit, bool down) {
  c.fillRRect(r.x, r.y, r.w, r.h, r.h / 2.0f, lit ? (down ? ACCENT_DOWN : ACCENT) : (down ? BTN_DOWN : BTN));
  const Color col = lit ? DARK : TEXT;
  const float w = textW(gfont::SMALL, label) + 26;
  const float x = r.x + (r.w - w) / 2.0f, cy = r.y + r.h / 2.0f;
  if (icon == 0) playIcon(c, x + 7, cy, 6, col);
  if (icon == 1) shuffleIcon(c, x + 8, cy, col);
  if (icon == 2) radioIcon(c, x + 8, cy, col);
  c.text(gfont::SMALL, label, x + 26, (int)(cy + 6), col);
}

// A list picture with rounded corners (a circle for an artist)
void drawThumb(Canvas& c, const uint16_t* px, int x0, int y0, bool round) {
  const Rect box = gfx::intersect(c.area(), Rect{x0, y0, THUMB, THUMB});
  if (box.empty()) return;
  const float R = round ? THUMB / 2.0f : 8.0f;
  for (int y = box.y; y < box.bottom(); ++y) {
    uint16_t* out = c.row(y) + (box.x - c.originX());
    const uint16_t* in = px + (size_t)(y - y0) * THUMB + (box.x - x0);
    const float py = y - y0 + 0.5f;
    const float dy = fmaxf(fmaxf(R - py, py - (THUMB - R)), 0.0f);
    for (int x = box.x; x < box.right(); ++x, ++out, ++in) {
      const float pxx = x - x0 + 0.5f;
      const float dx = fmaxf(fmaxf(R - pxx, pxx - (THUMB - R)), 0.0f);
      if (dx > 0 && dy > 0) {
        const float a = 0.5f - (sqrtf(dx * dx + dy * dy) - R);
        if (a <= 0) continue;
        if (a < 1) {
          *out = gfx::blend565(*out, *in, (uint32_t)(a * 255));
          continue;
        }
      }
      *out = *in;
    }
  }
}

bool isHome(const View& v) { return v.page == nullptr; }
const List* shown(const View& v) { return v.page ? v.page : v.home; }

char tabKind(const View& v) {
  const List* l = shown(v);
  if (!l || !l->tabs[0]) return 0;
  const int n = (int)strlen(l->tabs);
  return l->tabs[v.tab < n ? v.tab : 0];
}

int rowsShown(const View& v) { return isHome(v) ? (388 - HOME_ROW_Y) / ROW_STEP : (388 - PAGE_ROW_Y) / ROW_STEP; }
int rowY(const View& v) { return isHome(v) ? HOME_ROW_Y : PAGE_ROW_Y; }

// The list index of the n-th row of the current tab, or -1
int itemOfRow(const View& v, int n) {
  const List* l = shown(v);
  if (!l) return -1;
  const char k = tabKind(v);
  for (int i = 0; i < l->count; ++i) {
    if (k && l->items[i].kind != k) continue;
    if (n-- == 0) return i;
  }
  return -1;
}

const char* tabName(char k) {
  switch (k) {
    case SONG: return "Songs";
    case ALBUM: return "Albums";
    case ARTIST: return "Artists";
    case PLAYLIST: return "Playlists";
    default: return "";
  }
}

void tabRects(const View& v, Rect out[4], int& n) {
  const List* l = shown(v);
  n = 0;
  float x = 16;
  for (int i = 0; l && l->tabs[i] && i < 4; ++i) {
    const int w = (int)textW(gfont::SMALL, tabName(l->tabs[i])) + 32;
    out[n++] = Rect((int)x, 108, w, 36);
    x += w + 8;
  }
}

// The page buttons on the right of the tabs row (artist: Shuffle + Radio, album: Play + Shuffle)
int pageButtons(const View& v, Rect out[2], int icons[2], const char* labels[2], int btns[2]) {
  const List* l = v.page;
  if (!l || (l->kind != 'r' && l->kind != 'a')) return 0;
  out[0] = Rect(560, 106, 108, 40);
  out[1] = Rect(676, 106, 108, 40);
  if (l->kind == 'a') {
    labels[0] = "Play";
    icons[0] = 0;
    btns[0] = B_PLAY;
    labels[1] = "Shuffle";
    icons[1] = 1;
    btns[1] = B_SHUFFLE;
  } else {
    labels[0] = "Shuffle";
    icons[0] = 1;
    btns[0] = B_SHUFFLE;
    labels[1] = "Radio";
    icons[1] = 2;
    btns[1] = B_PLAY;  // an artist's "Play" is their radio
  }
  return 2;
}

void moodRects(Rect out[MOOD_COUNT]) {
  float x = 16;
  for (int i = 0; i < MOOD_COUNT; ++i) {
    const int w = (int)textW(gfont::SMALL, MOODS[i]) + 30;
    out[i] = Rect((int)x, 108, w, 36);
    x += w + 10;
  }
}

void drawSwitch(Canvas& c, const View& v) {
  const Rect& s = SWITCH;
  c.fillRRect(s.x, s.y, s.w, s.h, s.h / 2.0f, PANEL);
  const bool browse = v.mode == BROWSE;
  const float half = s.w / 2.0f;
  c.fillRRect(s.x + 3 + (browse ? half : 0), s.y + 3, half - 6, s.h - 6, (s.h - 6) / 2.0f, SEL);
  centered(c, gfont::SMALL, "Playing", s.x + half / 2, s.y + 27, browse ? MUTED : TEXT);
  centered(c, gfont::SMALL, "Browse", s.x + half * 1.5f, s.y + 27, browse ? TEXT : MUTED);
}

void drawHead(Canvas& c, const View& v) {
  drawSwitch(c, v);
  if (v.mode == PLAYING) {
    if (v.canRadio) iconPill(c, RADIO_BTN, "Song radio", 2, false, v.pressed == B_RADIO);
    pill(c, LYRICS_BTN, "Lyrics", v.lyricsOn, v.pressed == B_LYRICS);
    return;
  }
  if (v.page && v.page->kind != 'q') {  // an artist / album page: Back + its name
    const Rect& b = BACK_BTN;
    c.fillRRect(b.x, b.y, b.w, b.h, b.h / 2.0f, v.pressed == B_BACK ? BTN_DOWN : BTN);
    const Pt arrow[3] = {{(float)b.x + 20, (float)b.y + 21}, {(float)b.x + 30, (float)b.y + 13}, {(float)b.x + 30, (float)b.y + 29}};
    c.fillPolygon(arrow, 3, TEXT, 1, 1);
    c.text(gfont::SMALL, "Back", b.x + 40, b.y + 27, TEXT);
    line(c, gfont::TITLE, v.page->title, BACK_BTN.right() + 16, 780 - BACK_BTN.right() - 16, 81, TEXT);
    return;
  }
  const Rect& s = SEARCH_BAR;
  const bool listening = v.voice != VOICE_IDLE;
  if (listening) c.fillRRect(s.x - 2, s.y - 2, s.w + 4, s.h + 4, s.h / 2.0f + 2, ACCENT, 0.35f);
  c.fillRRect(s.x, s.y, s.w, s.h, s.h / 2.0f, BTN);
  if (listening) {
    for (int i = 0; i < 6; ++i) {
      const float h = 6 + 12 * fabsf(sinf(i * 1.3f));
      c.fillRRect(s.x + 22 + i * 8, s.y + 21 - h / 2, 4, h, 2, ACCENT);
    }
    c.text(gfont::NOTE, v.voice == VOICE_LISTENING ? "Listening... say a song or a band" : "Searching...", s.x + 80,
           s.y + 29, TEXT);
  } else {
    magnifier(c, s.x + 24, s.y + 19, MUTED);
    if (v.query[0]) {
      line(c, gfont::NOTE, v.query, s.x + 46, s.w - 96, s.y + 29, TEXT);
      // x: clear the search, back to Liked songs
      const float cx = s.right() - 24, cy = s.y + s.h / 2.0f;
      c.fillCircle(cx, cy, 13, PANEL);
      c.fillCapsule(cx - 5, cy - 5, cx + 5, cy + 5, 1.4f, MUTED);
      c.fillCapsule(cx + 5, cy - 5, cx - 5, cy + 5, 1.4f, MUTED);
    } else {
      c.text(gfont::NOTE, "Search songs, bands, albums", s.x + 46, s.y + 29, FAINT);
    }
  }
  c.fillCircle(MIC_X, MIC_Y, MIC_R, v.pressed == B_MIC ? ACCENT_DOWN : ACCENT);
  mic(c, MIC_X, MIC_Y - 2, DARK);
}

void drawChips(Canvas& c, const View& v) {
  if (isHome(v)) {
    Rect r[MOOD_COUNT];
    moodRects(r);
    for (int i = 0; i < MOOD_COUNT; ++i) pill(c, r[i], MOODS[i], false, v.pressed == B_MOOD0 + i);
    return;
  }
  Rect t[4];
  int n;
  tabRects(v, t, n);
  for (int i = 0; i < n; ++i) {
    const bool on = i == v.tab;
    c.fillRRect(t[i].x, t[i].y, t[i].w, t[i].h, t[i].h / 2.0f, on ? TEXT : BTN);
    centered(c, gfont::SMALL, tabName(v.page->tabs[i]), t[i].x + t[i].w / 2.0f, t[i].y + 24, on ? rgb(0x05070A) : TEXT);
  }
  if (!n && v.page->sub[0]) line(c, gfont::SMALL, v.page->sub, 20, 520, 132, MUTED);  // album: "Muse - 2003"
  Rect b[2];
  int icons[2], btns[2];
  const char* labels[2];
  const int nb = pageButtons(v, b, icons, labels, btns);
  for (int i = 0; i < nb; ++i) iconPill(c, b[i], labels[i], icons[i], i == 0, v.pressed == btns[i]);
}

void drawRow(Canvas& c, const View& v, const List& l, int index, int top, ThumbFn thumb) {
  const Item& it = l.items[index];
  if (v.pressedRow == index) c.fillRRect(10, top - 2, 780, ROW_H + 4, 12, ROW_DOWN);
  const int tx = 20, ty = top + (ROW_H - THUMB) / 2;
  const uint16_t* px = it.img && thumb ? thumb(it.img) : nullptr;
  const bool round = it.kind == ARTIST;
  if (px) {
    drawThumb(c, px, tx, ty, round);
  } else if (l.kind == 'a' && it.kind == SONG) {  // an album's songs: track numbers
    char num[8];
    snprintf(num, sizeof num, "%d", index + 1);
    centered(c, gfont::TITLE, num, tx + THUMB / 2.0f, ty + 32, MUTED);
  } else {
    if (round)
      c.fillCircle(tx + THUMB / 2.0f, ty + THUMB / 2.0f, THUMB / 2.0f, BTN);
    else
      c.fillRRect(tx, ty, THUMB, THUMB, 8, BTN);
    note(c, tx + THUMB / 2.0f, ty + THUMB / 2.0f, 1.6f, FAINT);
  }
  const float x = tx + THUMB + 16;
  const float rw = it.right[0] ? textW(gfont::SMALL, it.right) : 0;
  const float w = 780 - x - (rw ? rw + 16 : 0);
  line(c, gfont::NOTE, it.title, x, w, top + 23, TEXT);
  if (it.sub[0]) line(c, gfont::SMALL, it.sub, x, w, top + 46, MUTED);
  if (rw) c.text(gfont::SMALL, it.right, 780 - rw, top + 34, MUTED);
}

void drawList(Canvas& c, const View& v, ThumbFn thumb) {
  const List* l = shown(v);
  if (isHome(v)) {
    c.text(gfont::SMALL, "LIKED SONGS", 20, 176, ACCENT);
    if (l && l->total > 0) {
      char n[16];
      snprintf(n, sizeof n, "%d", l->total);
      c.text(gfont::SMALL, n, 24 + textW(gfont::SMALL, "LIKED SONGS"), 176, FAINT);
      iconPill(c, HOME_PLAY, "Play", 0, false, v.pressed == B_PLAY);
      iconPill(c, HOME_SHUFFLE, "Shuffle", 1, true, v.pressed == B_SHUFFLE);
    }
  }
  const int y0 = rowY(v), rows = rowsShown(v);
  int drawn = 0;
  if (l) {
    for (int r = 0; r < rows; ++r) {
      const int i = itemOfRow(v, v.first + r);
      if (i < 0) break;
      drawRow(c, v, *l, i, y0 + r * ROW_STEP, thumb);
      ++drawn;
    }
    // a thin scroll bar when there are more rows
    const int total = rowCount(v);
    if (total > rows) {
      const float h = 388 - y0 - 8, bh = fmaxf(24, h * rows / total);
      const float by = y0 + (h - bh) * v.first / (float)(total - rows);
      c.fillRRect(793, by, 4, bh, 2, SEL);
    }
  }
  if (!drawn) {
    const char* msg = v.status[0] ? v.status : isHome(v) ? "Connect YouTube Music in Gorgi Helper's Settings" : "Nothing here";
    centered(c, gfont::NOTE, msg, 400, y0 + 70, MUTED);
  } else if (v.status[0]) {
    // a short note over the list ("Playing...")
    const float w = textW(gfont::SMALL, v.status) + 36;
    c.fillRRect(400 - w / 2, 340, w, 36, 18, SEL);
    centered(c, gfont::SMALL, v.status, 400, 364, TEXT);
  }
}

void drawLyrics(Canvas& c, const View& v) {
  const Lyrics& l = v.lyrics;
  const Rect& a = LYRICS_AREA;
  if (l.loading || l.count == 0) {
    c.text(gfont::NOTE, l.loading ? "Looking for the lyrics..." : "No lyrics for this song", a.x + 14, a.y + 56, MUTED);
    return;
  }
  constexpr int LINES = 4, STEP = 31;
  int first = l.synced ? l.current - 1 : l.first;
  if (first > l.count - LINES) first = l.count - LINES;
  if (first < 0) first = 0;
  for (int i = 0; i < LINES && first + i < l.count; ++i) {
    const int n = first + i;
    const bool now = l.synced && n == l.current;
    const Color col = !l.synced ? TEXT : now ? TEXT : n < l.current ? FAINT : MUTED;
    const char* s = l.lines[n].text;
    const gfont::Font& f = textW(gfont::NOTE, s) <= a.w - 12 ? gfont::NOTE : gfont::SMALL;
    if (now) c.fillRRect(a.x, a.y + 4 + i * STEP, 4, 24, 2, ACCENT);
    line(c, f, s[0] ? s : "~", a.x + 14, a.w - 14, a.y + 24 + i * STEP, col);
  }
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
  if (key == KEY_GO) {
    c.fillRRect(r.x, r.y, r.w, r.h, 10, down ? ACCENT_DOWN : ACCENT);
    centered(c, gfont::SUBTITLE, "Search", r.x + r.w / 2.0f, r.y + 37, DARK);
    return;
  }
  const bool special = key == KEY_SHIFT || key == KEY_DELETE || key == KEY_SYMBOLS;
  const bool lit = key == KEY_SHIFT && kb.shift && !kb.symbols;
  c.fillRRect(r.x, r.y, r.w, r.h, 10, down ? BTN_DOWN : lit ? ACCENT : special ? SEL : KEY_BG);
  const float cx = r.x + r.w / 2.0f;
  if (key == KEY_SHIFT) {
    if (!kb.symbols) centered(c, gfont::SMALL, "Shift", cx, r.y + 34, lit ? DARK : TEXT);
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

void drawKeyboard(Canvas& c, const View& v) {
  const Keyboard& kb = v.kb;
  const Rect& f = KB_FIELD;
  c.fillRRect(f.x, f.y, f.w, f.h, 14, PANEL);
  c.fillRRect(f.x, f.bottom() - 2, f.w, 2, 1, ACCENT);
  magnifier(c, f.x + 26, f.y + 26, MUTED);
  const char* shown = kb.text;
  while (*shown && textW(gfont::NOTE, shown) > f.w - 80) ++shown;
  if (kb.text[0])
    c.text(gfont::NOTE, shown, f.x + 50, f.y + 36, TEXT);
  else
    c.text(gfont::NOTE, "A song, a band or an album...", f.x + 50, f.y + 36, FAINT);
  const float cx = f.x + 52 + (kb.text[0] ? textW(gfont::NOTE, shown) : 0);
  c.fillRect(cx, f.y + 14, 2, 28, ACCENT);
  c.text(gfont::SMALL, "Tip: \"Muse\", \"Hysteria by Muse\", \"Absolution album\"", 20, 146, FAINT);
  for (int k = 0; k < KEY_COUNT; ++k) drawKey(c, kb, k);
  const Rect& b = KB_CANCEL;
  c.fillRRect(b.x, b.y, b.w, b.h, b.h / 2.0f, BTN);
  centered(c, gfont::SMALL, "Cancel", b.x + b.w / 2.0f, b.y + 32, TEXT);
  const Rect& vb = KB_VOICE;
  c.fillRRect(vb.x, vb.y, vb.w, vb.h, vb.h / 2.0f, BTN);
  const char* label = "Say it instead";
  const float w = textW(gfont::SMALL, label) + 28, x = vb.x + (vb.w - w) / 2;
  mic(c, x + 6, vb.y + 25, ACCENT);
  c.text(gfont::SMALL, label, x + 28, vb.y + 32, TEXT);
}

}  // namespace

int rowCount(const View& v) {
  const List* l = shown(v);
  if (!l) return 0;
  const char k = tabKind(v);
  if (!k) return l->count;
  int n = 0;
  for (int i = 0; i < l->count; ++i)
    if (l->items[i].kind == k) ++n;
  return n;
}

int visibleItems(const View& v, int out[], int max) {
  if (v.mode != BROWSE || v.kb.open) return 0;
  int n = 0;
  for (int r = 0; r < rowsShown(v) && n < max; ++r) {
    const int i = itemOfRow(v, v.first + r);
    if (i < 0) break;
    out[n++] = i;
  }
  return n;
}

void draw(Canvas& c, const View& v, ThumbFn thumb) {
  if (v.kb.open) {
    drawKeyboard(c, v);
    return;
  }
  drawHead(c, v);
  if (v.mode == PLAYING) {
    if (v.lyricsOn) drawLyrics(c, v);
    return;
  }
  drawChips(c, v);
  drawList(c, v, thumb);
}

int pressedCode(const TapResult& t) {
  switch (t.what) {
    case Tap::Radio: return B_RADIO;
    case Tap::Lyrics: return B_LYRICS;
    case Tap::Mic: return B_MIC;
    case Tap::Back: return B_BACK;
    case Tap::Play: return B_PLAY;
    case Tap::Shuffle: return B_SHUFFLE;
    case Tap::Mood: return B_MOOD0 + t.index;
    default: return -1;
  }
}

bool inList(int x, int y, const View& v) {
  (void)x;
  return v.mode == BROWSE && !v.kb.open && y >= rowY(v) - 4 && y < 388;
}

TapResult tap(int x, int y, const View& v) {
  if (v.kb.open) {
    for (int k = 0; k < KEY_COUNT; ++k)
      if (inside(gfx::inflate(keyRect(k), 2), x, y)) return TapResult(Tap::KbKey, k);
    if (inside(gfx::inflate(KB_CANCEL, 4), x, y)) return TapResult(Tap::KbCancel);
    if (inside(gfx::inflate(KB_VOICE, 4), x, y)) return TapResult(Tap::KbVoice);
    if (inside(KB_FIELD, x, y)) return TapResult();
    return TapResult();
  }
  if (inside(gfx::inflate(SWITCH, 4), x, y))
    return TapResult(x < SWITCH.x + SWITCH.w / 2 ? Tap::ToPlaying : Tap::ToBrowse);
  if (v.mode == PLAYING) {
    if (v.canRadio && inside(gfx::inflate(RADIO_BTN, 4), x, y)) return TapResult(Tap::Radio);
    if (inside(gfx::inflate(LYRICS_BTN, 4), x, y)) return TapResult(Tap::Lyrics);
    return TapResult();
  }
  // Browse
  if (y < HEAD_AREA.bottom()) {
    if (v.page && v.page->kind != 'q') return inside(gfx::inflate(BACK_BTN, 6), x, y) ? TapResult(Tap::Back) : TapResult();
    if (v.voice != VOICE_IDLE || hypotf(x - MIC_X, y - MIC_Y) < MIC_R + 10) return TapResult(Tap::Mic);
    if (inside(SEARCH_BAR, x, y)) {
      if (v.query[0] && x > SEARCH_BAR.right() - 50) return TapResult(Tap::Clear);
      return TapResult(Tap::Search);
    }
    return TapResult();
  }
  if (y < CHIPS_AREA.bottom()) {
    if (isHome(v)) {
      Rect r[MOOD_COUNT];
      moodRects(r);
      for (int i = 0; i < MOOD_COUNT; ++i)
        if (inside(gfx::inflate(r[i], 4), x, y)) return TapResult(Tap::Mood, i);
      return TapResult();
    }
    Rect b[2];
    int icons[2], btns[2];
    const char* labels[2];
    const int nb = pageButtons(v, b, icons, labels, btns);
    for (int i = 0; i < nb; ++i)
      if (inside(gfx::inflate(b[i], 4), x, y)) return TapResult(btns[i] == B_PLAY ? Tap::Play : Tap::Shuffle);
    Rect t[4];
    int n;
    tabRects(v, t, n);
    for (int i = 0; i < n; ++i)
      if (inside(gfx::inflate(t[i], 4), x, y)) return TapResult(Tap::Tab, i);
    return TapResult();
  }
  if (isHome(v) && y < HOME_ROW_Y - 2) {
    if (inside(gfx::inflate(HOME_PLAY, 6), x, y)) return TapResult(Tap::Play);
    if (inside(gfx::inflate(HOME_SHUFFLE, 6), x, y)) return TapResult(Tap::Shuffle);
    return TapResult();
  }
  const int r = (y - rowY(v)) / ROW_STEP;
  if (y >= rowY(v) && r < rowsShown(v)) {
    const int i = itemOfRow(v, v.first + r);
    if (i >= 0) return TapResult(Tap::Row, i);
  }
  return TapResult();
}

Rect keyRect(int key) {
  if (key >= 0 && key < 10) return Rect((int)(4 + key * KSTEP), 168, (int)KW, (int)KH);
  if (key >= 10 && key < 19) return Rect((int)(43 + (key - 10) * KSTEP), 228, (int)KW, (int)KH);
  if (key >= 20 && key < 27) return Rect((int)(122 + (key - 20) * KSTEP), 288, (int)KW, (int)KH);
  switch (key) {
    case KEY_SHIFT: return Rect(4, 288, 114, (int)KH);
    case KEY_DELETE: return Rect(676, 288, 120, (int)KH);
    case KEY_SYMBOLS: return Rect(4, 348, 114, (int)KH);
    case KEY_SPACE: return Rect(122, 348, 430, (int)KH);
    case KEY_GO: return Rect(556, 348, 240, (int)KH);
    default: return Rect();
  }
}

Press press(Keyboard& kb, int key) {
  const int len = (int)strlen(kb.text);
  if (key == KEY_GO) return Press::Go;
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
    kb.text[len - 1] = 0;
    kb.shift = len == 1;
    return kb.shift != wasShift ? Press::Keys : Press::Text;
  }
  const char ch = key == KEY_SPACE ? ' ' : keyChar(kb, key);
  if (!ch || len >= (int)sizeof kb.text - 1) return Press::None;
  if (ch == ' ' && (len == 0 || kb.text[len - 1] == ' ')) return Press::None;
  kb.text[len] = ch;
  kb.text[len + 1] = 0;
  kb.shift = false;  // a capital only at the start
  return kb.shift != wasShift ? Press::Keys : Press::Text;
}

bool followLyrics(Lyrics& l, uint32_t songMs) {
  if (!l.synced || l.count == 0) return false;
  int cur = -1;
  for (int i = 0; i < l.count; ++i) {
    if (l.lines[i].ms > songMs) break;
    cur = i;
  }
  if (cur == l.current) return false;
  l.current = cur;
  return true;
}

}  // namespace browse
