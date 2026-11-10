// Draws the Music screen's Browse view (search, lists, artist / album pages, keyboard, lyrics) into .ppm pictures.
// (The song itself and the deck are drawn by music_screen.cpp / ui.cpp, which need the board: a stand-in deck is drawn here.)
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <vector>
#include <map>
#include "gfont.h"
#include "tabs.h"
#include "panel.h"
#include "music_browse.h"
using gfx::Canvas; using gfx::Color;

const Color BG = gfx::rgb(0x05070A), PANEL = gfx::rgb(0x10151C), BORDER = gfx::rgb(0x1F2935), BTN = gfx::rgb(0x1C2733),
  TEXT = gfx::rgb(0xF2F6FA), MUTED = gfx::rgb(0x8E9AA7), ACCENT = gfx::rgb(0x4FE3D6), LINE = gfx::rgb(0x161D27);
static std::map<uint32_t, std::vector<uint16_t>> thumbs;
static const uint16_t* thumbFn(uint32_t id) { auto it = thumbs.find(id); return it == thumbs.end() ? nullptr : it->second.data(); }
static void makeThumb(uint32_t id, uint32_t a, uint32_t b) {
  std::vector<uint16_t> px(browse::THUMB * browse::THUMB);
  Canvas c; c.begin(px.data(), browse::THUMB, browse::THUMB, 0, 0); c.clear(gfx::rgb(a));
  c.fillCircle(30, 18, 12, gfx::rgb(b), 0.85f); c.fillCircle(14, 34, 9, gfx::rgb(b), 0.6f);
  thumbs[id] = px;
}
static void thumbIcon(Canvas& c, float cx, float cy, bool up, const Color& col, float size = 22) {
  const float k = size / 24;
  auto P = [&](float x, float y) { if (!up) { x = 24 - x; y = 24 - y; } return gfx::Pt{cx + (x - 12) * k, cy + (y - 12) * k}; };
  const gfx::Pt cuff[4] = {P(1, 9.5f), P(5, 9.5f), P(5, 21), P(1, 21)};
  const gfx::Pt finger[6] = {P(7, 9.2f), P(13.4f, 2.2f), P(14.9f, 2.4f), P(15.8f, 3.8f), P(14.8f, 9), P(7, 11)};
  const gfx::Pt palm[8] = {P(7, 9.5f), P(14, 8.2f), P(21.2f, 8.2f), P(23, 10), P(23, 12), P(19.9f, 19.9f), P(18, 21), P(7, 21)};
  c.fillPolygon(cuff, 4, col, 1, 0.8f * k); c.fillPolygon(finger, 6, col, 1, 1.0f * k); c.fillPolygon(palm, 8, col, 1, 1.2f * k);
}
static void deck(Canvas& c) {
  const int cy = 434;
  c.fillRRect(16, cy - 42, 768, 84, 20, BORDER); c.fillRRect(17, cy - 41, 766, 82, 19, PANEL);
  c.fillCircle(80, cy, 28, BTN); c.fillCircle(146, cy, 32, ACCENT); c.fillCircle(212, cy, 28, BTN);
  c.fillRRect(72, cy - 10, 4, 20, 1, TEXT); { gfx::Pt t[3] = {{90, (float)cy - 10}, {90, (float)cy + 10}, {77, (float)cy}}; c.fillPolygon(t, 3, TEXT, 1, 1); }
  c.fillRRect(138, cy - 11, 6, 22, 2, BG); c.fillRRect(149, cy - 11, 6, 22, 2, BG);
  c.fillRRect(216, cy - 10, 4, 20, 1, TEXT); { gfx::Pt t[3] = {{202, (float)cy - 10}, {202, (float)cy + 10}, {215, (float)cy}}; c.fillPolygon(t, 3, TEXT, 1, 1); }
  c.fillCircle(292, cy, 26, ACCENT); thumbIcon(c, 292, cy, true, BG, 24);
  c.fillCircle(352, cy, 26, BTN); thumbIcon(c, 352, cy, false, TEXT, 24);
  const int s0 = 440, s1 = 690; const float kx = s0 + (s1 - s0) * 0.45f;
  c.fillRRect(s0, cy - 5, s1 - s0, 10, 5, gfx::rgb(0x2A3441)); c.fillRRect(s0, cy - 5, kx - s0, 10, 5, ACCENT);
  c.fillCircle(kx, cy, 14, TEXT); c.text(gfont::SMALL, "45%", 734, cy + 7, MUTED);
}
static void save(const char* name, const browse::View& v, bool withDeck = true) {
  const int W = 800, H = 480; std::vector<uint16_t> buf(W * H);
  Canvas c; c.begin(buf.data(), W, H, 0, 0); c.clear(BG);
  c.fillRect(0, tabs::LINE_Y, 800, 1, LINE); c.text(gfont::TITLE, "20:14", 20, 30, gfx::rgb(0xC9D3DD));
  const uint8_t icons[] = {tabs::MUSIC, tabs::FACE, tabs::TIME, tabs::TODO, tabs::CLAUDE};
  tabs::draw(c, icons, 5, 0, 0); panel::drawHandle(c, false);
  browse::draw(c, v, thumbFn);
  if (withDeck && !v.kb.open) deck(c);
  FILE* f = fopen(name, "wb"); fprintf(f, "P6 %d %d 255\n", W, H);
  for (uint16_t p : buf) { unsigned char o[3] = {(unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p & 31) << 3)}; fwrite(o, 1, 3, f); }
  fclose(f);
}
static browse::Item items[4][browse::MAX_ITEMS];
static void add(browse::List& l, char k, uint32_t img, const char* t, const char* s, const char* r) {
  browse::Item& it = l.items[l.count++]; it.kind = k; it.img = img;
  snprintf(it.title, sizeof it.title, "%s", t); snprintf(it.sub, sizeof it.sub, "%s", s); snprintf(it.right, sizeof it.right, "%s", r);
}
static void init(browse::List& l, int slot, char kind, const char* title, const char* sub, const char* tabs) {
  memset(&l, 0, sizeof l); l.items = items[slot]; l.kind = kind; snprintf(l.title, sizeof l.title, "%s", title);
  snprintf(l.sub, sizeof l.sub, "%s", sub); snprintf(l.tabs, sizeof l.tabs, "%s", tabs);
}
int main() {
  makeThumb(1, 0x4A1F5C, 0xF0A040); makeThumb(2, 0x8A2A1A, 0xE0A070); makeThumb(3, 0xE07A2E, 0xF6D58C); makeThumb(4, 0x2A3E4C, 0x9DB8C6);
  makeThumb(5, 0xC0202A, 0xF2E8E0); makeThumb(6, 0x6A6A70, 0xD8D8D8); makeThumb(7, 0x1E2A44, 0xE0C060); makeThumb(8, 0x3A5A30, 0xC0E080);
  browse::List liked; init(liked, 0, 'l', "Liked songs", "", "");
  add(liked, 's', 1, "Supermassive Black Hole", "Muse", "3:32");
  add(liked, 's', 2, "You Don't Know Nothing About Love", "Chris Cornell", "4:12");
  add(liked, 's', 3, "Simon Says", "Pharoahe Monch", "3:47");
  add(liked, 's', 4, "Hurt", "Nine Inch Nails", "6:13");
  liked.total = 214;
  browse::View v; v.canRadio = true; v.home = &liked;
  save("1-playing.ppm", v);
  browse::LyricLine lines[8] = {{0, "Oh baby, don't you know I suffer?"}, {6000, "Oh baby, can you hear me moan?"},
    {12000, "You caught me under false pretenses"}, {18000, "How long before you let me go?"}, {24000, "Ooh, you set my soul alight"}, {30000, "Ooh, you set my soul alight"}};
  v.lyrics.lines = lines; v.lyrics.count = 6; v.lyrics.synced = true; browse::followLyrics(v.lyrics, 13000);
  v.lyricsOn = true; save("2-lyrics.ppm", v);
  v.lyricsOn = false; v.mode = browse::BROWSE; save("3-browse.ppm", v);
  browse::List res; init(res, 1, 'q', "Muse", "", "sar");
  add(res, 's', 7, "Uprising", "Muse - The Resistance", "5:05"); add(res, 's', 1, "Supermassive Black Hole", "Muse - Black Holes and Revelations", "3:32");
  add(res, 's', 1, "Starlight", "Muse - Black Holes and Revelations", "4:00"); add(res, 's', 6, "Hysteria", "Muse - Absolution", "3:48");
  add(res, 'a', 6, "Absolution", "Muse - 2003", ""); add(res, 'a', 1, "Black Holes and Revelations", "Muse - 2006", "");
  add(res, 'r', 5, "Muse", "Artist - 15.4M monthly listeners", ""); add(res, 'r', 0, "Muse Dash", "Artist", "");
  v.page = &res; v.depth = 0; snprintf(v.query, sizeof v.query, "Muse"); v.tab = 2; save("4-results.ppm", v);
  browse::List art2; init(art2, 2, 'r', "Muse", "", "as");
  add(art2, 'a', 8, "Will of the People", "Album - 2022", ""); add(art2, 'a', 4, "Simulation Theory", "Album - 2018", "");
  add(art2, 'a', 7, "The Resistance", "Album - 2009", ""); add(art2, 'a', 6, "Absolution", "Album - 2003", "");
  add(art2, 's', 7, "Uprising", "The Resistance", "5:05");
  v.page = &art2; v.depth = 1; v.tab = 0; save("5-artist.ppm", v);
  browse::List alb; init(alb, 3, 'a', "Absolution", "Muse - 2003 - 14 songs", "");
  const char* t[] = {"Intro", "Apocalypse Please", "Time Is Running Out", "Sing for Absolution", "Stockholm Syndrome"};
  const char* d[] = {"0:22", "4:13", "3:56", "4:54", "4:58"};
  for (int i = 0; i < 5; i++) add(alb, 's', 0, t[i], "Muse", d[i]);
  v.page = &alb; v.depth = 2; save("6-album.ppm", v);
  v.page = nullptr; v.query[0] = 0; v.voice = browse::VOICE_LISTENING; save("7-listening.ppm", v);
  v.voice = 0; v.kb.open = true; snprintf(v.kb.text, sizeof v.kb.text, "Alice in ch"); v.kb.shift = false; save("8-keyboard.ppm", v);
}
