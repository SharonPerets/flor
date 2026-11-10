// The Music screen's top row (Playing / Browse switch), the Browse view (search,
// Liked songs, search results, an artist's or an album's page),
// the search keyboard and the lyrics. Since 4.8.19.
//
// The helper does the YouTube Music work: it sends lists of songs / albums /
// artists and small pictures; a tap here sends back "music_pick" {list, index}
// and the helper plays it or opens the next page.
//
// This part only draws and handles taps, so it also runs in the PC preview.
#pragma once

#include <stdint.h>

#include "gfx.h"

namespace browse {

// ---- the screen's parts (the Music screen's middle is y 44..388; the deck is under it)
constexpr gfx::Rect HEAD_AREA{0, 44, 800, 58};     // switch + search bar / Radio + Lyrics
constexpr gfx::Rect CHIPS_AREA{0, 102, 800, 48};   // "Liked songs" + Play / Shuffle, or Songs / Albums tabs
constexpr gfx::Rect LIST_AREA{0, 150, 800, 238};   // the list
constexpr gfx::Rect LYRICS_AREA{322, 152, 458, 128};  // the lyrics, under the song's name, above its progress bar
constexpr gfx::Rect KB_AREA{0, 44, 800, 436};      // the keyboard covers the deck too

constexpr int THUMB = 48;  // list pictures are THUMB x THUMB (RGB565)
constexpr int ROW_STEP = 58, ROW_H = 54;
constexpr int TEXT_MAX = 63;

enum Kind : uint8_t { SONG = 's', ALBUM = 'a', ARTIST = 'r', PLAYLIST = 'p' };
enum Mode : uint8_t { PLAYING, BROWSE };
enum Voice : uint8_t { VOICE_IDLE, VOICE_LISTENING, VOICE_THINKING };

struct Item {
  uint8_t kind;
  uint32_t img;  // picture id (0 = none)
  char title[TEXT_MAX + 1];
  char sub[TEXT_MAX + 1];
  char right[12];  // "3:48", "2004"
};

// A list from the helper: Liked songs, search results, an artist, an album
struct List {
  uint32_t id;
  uint8_t kind;  // 'l' liked, 'q' search, 'r' artist, 'a' album
  char title[TEXT_MAX + 1];
  char sub[TEXT_MAX + 1];
  char tabs[4];  // the kinds shown as tabs, in order ("sar"); empty = no tabs
  int total;     // items the helper has
  int count;     // items received so far
  Item* items;   // room for MAX_ITEMS
};
constexpr int MAX_ITEMS = 120;

struct Keyboard {
  bool open;
  char text[48];
  bool shift, symbols;
  int pressed;
  Keyboard() : open(false), shift(true), symbols(false), pressed(-1) { text[0] = 0; }
};

struct LyricLine {
  uint32_t ms;  // when it is sung (synced lyrics)
  char text[72];
};
constexpr int MAX_LYRICS = 120;

struct Lyrics {
  int count;     // 0 = none
  bool synced;
  bool loading;  // asked the helper, no answer yet
  int current;   // the line being sung (synced)
  int first;     // scrolled (not synced)
  LyricLine* lines;  // room for MAX_LYRICS
  Lyrics() : count(0), synced(false), loading(false), current(-1), first(0), lines(nullptr) {}
};

struct View {
  uint8_t mode;     // PLAYING / BROWSE
  bool lyricsOn;    // the Lyrics button (Playing view)
  bool canRadio;    // a song is playing: "Song radio" is shown
  const List* home;  // Liked songs (always there in Browse)
  const List* page;  // results / artist / album on top of it, or nullptr = home
  int depth;         // pages under this one (Back goes there)
  int tab;           // index into page->tabs
  int first;         // scrolled: the first row shown
  char query[48];    // the search shown in the bar
  char status[72];   // "Searching...", "Nothing found"
  uint8_t voice;     // the mic in the search bar
  int pressed;       // a button under the finger (Tap value), for feedback
  int pressedRow;
  Keyboard kb;
  Lyrics lyrics;
  View() : mode(PLAYING), lyricsOn(false), canRadio(false), home(nullptr), page(nullptr), depth(0), tab(0), first(0),
           voice(VOICE_IDLE), pressed(-1), pressedRow(-1) {
    query[0] = status[0] = 0;
  }
};

// Pictures come from main (a small cache); nullptr = not here (yet)
typedef const uint16_t* (*ThumbFn)(uint32_t img);

// Draws the parts of the Music screen this module owns. In the Playing view that's the
// top row and (with lyrics on) the lyrics; music::draw does the rest.
void draw(gfx::Canvas& c, const View& v, ThumbFn thumb);

// The rows shown right now, as indexes into the list (for asking for their pictures)
int visibleItems(const View& v, int out[], int max);
// Rows in the current list / tab
int rowCount(const View& v);

enum class Tap {
  None, ToPlaying, ToBrowse, Radio, Lyrics, Search, Mic, Clear, Back, Tab, Play, Shuffle, Row,
  KbKey, KbCancel, KbVoice
};
struct TapResult {
  Tap what;
  int index;  // tab, item (index into the list), key
  TapResult(Tap w = Tap::None, int i = 0) : what(w), index(i) {}
};
TapResult tap(int x, int y, const View& v);
// The value for View::pressed that lights up this button while the finger is on it (-1: none)
int pressedCode(const TapResult& t);
bool inList(int x, int y, const View& v);  // drags here scroll the list

// Keyboard: 0-9, 10-18, 19 Shift, 20-26, 27 Delete, 28 "123", 29 space, 30 Search
constexpr int KEY_COUNT = 31;
constexpr int KEY_SHIFT = 19, KEY_DELETE = 27, KEY_SYMBOLS = 28, KEY_SPACE = 29, KEY_GO = 30;
enum class Press { None, Text, Keys, Go };
Press press(Keyboard& kb, int key);
gfx::Rect keyRect(int key);

// The lyrics line to show at this point in the song; true if it changed
bool followLyrics(Lyrics& l, uint32_t songMs);

}  // namespace browse
