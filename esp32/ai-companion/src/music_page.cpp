#include "music_page.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "display.h"
#include "music_browse.h"
#include "music_screen.h"
#include "pclink.h"

namespace musicpage {
namespace {

using browse::Tap;
using browse::TapResult;

constexpr gfx::Rect AREA{0, 44, 800, 344};    // the Music screen above the deck
constexpr gfx::Rect TOP_PART{0, 44, 800, 106};  // the top row + chips / tabs
constexpr gfx::Rect KB_FIELD_AREA{0, 44, 800, 72};
constexpr int PAGES = 3;          // search -> artist -> album
constexpr int THUMB_SLOTS = 48;   // pictures kept
constexpr int THUMB_PX = browse::THUMB * browse::THUMB;
constexpr uint32_t ASK_AGAIN_MS = 15000;  // a picture that didn't come is asked for again after this

browse::View v;
browse::List home;
browse::List pages[PAGES];
int pageCount = 0;
int savedTab[PAGES + 1], savedFirst[PAGES + 1];  // per level (0 = home): where you were

struct Slot {
  uint32_t id, used;
};
Slot slots[THUMB_SLOTS];
uint16_t* thumbPixels = nullptr;
uint32_t useCounter = 0;
struct Asked {
  uint32_t id, at;
};
Asked asked[24];

bool fullDirty = true, topDirty = false, listDirty = false, lyricsDirty = false, kbDirty = false, kbFieldDirty = false;
int kbKeyDirty = -1;
int progressKey = -2;
uint32_t statusUntil = 0;
bool homeAsked = false;
bool voiceActive = false;  // the search mic is listening (dictation goes here, not to the To-do screen)
String songTitle, songArtist, lyricsAsked;
bool helperOn = false;

// the touch
int downX = 0, downY = 0, startFirst = 0;
bool scrolling = false;
TapResult downTap;

template <class T>
T* allocate(size_t n) {
  T* p = (T*)heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
  if (!p) p = (T*)malloc(n * sizeof(T));
  return p;
}

// Copies text for the screen: Hebrew kept (song names), other letters made plain, cut letter-safe
void copyText(char* out, size_t size, const char* in) {
  const String s = ui::toAscii(String(in ? in : ""), true);
  size_t n = s.length();
  if (n > size - 1) {
    n = size - 1;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) --n;  // not in the middle of a letter
  }
  memcpy(out, s.c_str(), n);
  out[n] = 0;
}

const browse::List* shownList() { return pageCount ? &pages[pageCount - 1] : &home; }

void syncView() {
  v.page = pageCount ? &pages[pageCount - 1] : nullptr;
  v.depth = pageCount ? pageCount - 1 : 0;
}

void setStatus(const char* text, uint32_t ms) {
  copyText(v.status, sizeof v.status, text);
  statusUntil = text[0] && ms ? millis() + ms : 0;
  listDirty = true;
}

// ---- messages to the helper

void sendMusic(const char* what, const char* key = nullptr, const char* value = nullptr, int list = -1, int index = -1) {
  if (!pclink::helperConnected()) {
    setStatus("My PC helper isn't connected", 4000);
    return;
  }
  JsonDocument doc;
  doc["type"] = "music";
  doc["do"] = what;
  if (key) doc[key] = value;
  if (list >= 0) doc["list"] = list;
  if (index >= 0) doc["index"] = index;
  pclink::send(doc);
}

void askHome() {
  if (!pclink::helperConnected()) return;
  homeAsked = true;
  if (home.count == 0) setStatus("Loading your Liked songs...", 0);
  sendMusic("home");
}

void askLyrics() {
  if (!songTitle.length()) return;
  lyricsAsked = songTitle;
  v.lyrics.count = 0;
  v.lyrics.loading = true;
  v.lyrics.current = -1;
  v.lyrics.first = 0;
  JsonDocument doc;
  doc["type"] = "music";
  doc["do"] = "lyrics";
  doc["title"] = songTitle;
  doc["artist"] = songArtist;
  pclink::send(doc);
}

void search(const char* q) {
  copyText(v.query, sizeof v.query, q);
  if (!v.query[0]) return;
  sendMusic("search", "q", v.query);
}

// ---- pictures

const uint16_t* thumbOf(uint32_t id) {
  if (!thumbPixels || !id) return nullptr;
  for (int i = 0; i < THUMB_SLOTS; ++i)
    if (slots[i].id == id) {
      slots[i].used = ++useCounter;
      return thumbPixels + (size_t)i * THUMB_PX;
    }
  return nullptr;
}

void askThumbs() {
  if (!pclink::helperConnected() || !thumbPixels) return;
  int idx[8];
  const int n = browse::visibleItems(v, idx, 8);
  const browse::List* l = shownList();
  JsonDocument doc;
  JsonArray ids;
  const uint32_t now = millis();
  for (int i = 0; i < n; ++i) {
    const uint32_t id = l->items[idx[i]].img;
    if (!id || thumbOf(id)) continue;
    int free = -1, oldest = 0;
    bool recent = false;
    for (int k = 0; k < 24; ++k) {
      if (asked[k].id == id && now - asked[k].at < ASK_AGAIN_MS) recent = true;
      if (asked[k].at < asked[oldest].at) oldest = k;
      if (!asked[k].id && free < 0) free = k;
    }
    if (recent) continue;
    asked[free >= 0 ? free : oldest] = {id, now ? now : 1};
    if (ids.isNull()) ids = doc["ids"].to<JsonArray>();
    ids.add(id);
  }
  if (ids.isNull()) return;
  doc["type"] = "music";
  doc["do"] = "thumbs";
  pclink::send(doc);
}

// ---- the keyboard

void openKeyboard() {
  v.kb.open = true;
  copyText(v.kb.text, sizeof v.kb.text, v.query);
  v.kb.shift = v.kb.text[0] == 0;
  v.kb.symbols = false;
  v.kb.pressed = -1;
  ui::setBottomHidden(true);
  fullDirty = true;
}

void closeKeyboard() {
  if (!v.kb.open) return;
  v.kb.open = false;
  ui::setBottomHidden(false);
  fullDirty = true;
}

void startMic() {
  if (voiceActive) {
    pclink::sendSimple("cancel");
    voiceActive = false;
    v.voice = browse::VOICE_IDLE;
    topDirty = true;
    return;
  }
  if (!pclink::helperConnected()) {
    setStatus("My PC helper isn't connected", 4000);
    return;
  }
  pclink::sendText("{\"type\":\"dictate\",\"mode\":\"search\"}");
  voiceActive = true;
  v.voice = browse::VOICE_LISTENING;
  topDirty = true;
}

// ---- pages

void pushPage() {
  savedTab[pageCount] = v.tab;
  savedFirst[pageCount] = v.first;
  if (pageCount == PAGES) {  // full: the oldest goes
    browse::Item* keep = pages[0].items;
    for (int i = 0; i + 1 < PAGES; ++i) {
      pages[i] = pages[i + 1];
      savedTab[i + 1] = savedTab[i + 2];
      savedFirst[i + 1] = savedFirst[i + 2];
    }
    pages[PAGES - 1].items = keep;
    pageCount = PAGES - 1;
  }
  ++pageCount;
}

void back() {
  if (!pageCount) return;
  --pageCount;
  v.tab = savedTab[pageCount];
  v.first = savedFirst[pageCount];
  if (pageCount == 0) v.query[0] = 0;
  syncView();
  fullDirty = true;
}

void act(const TapResult& t) {
  const browse::List* l = shownList();
  const int listId = l == &home ? 1 : (int)l->id;
  switch (t.what) {
    case Tap::ToPlaying:
      if (v.mode != browse::PLAYING) {
        v.mode = browse::PLAYING;
        fullDirty = true;
      }
      break;
    case Tap::ToBrowse:
      if (v.mode != browse::BROWSE) {
        v.mode = browse::BROWSE;
        if (!homeAsked || home.count == 0) askHome();
        fullDirty = true;
      }
      break;
    case Tap::Radio:
      sendMusic("radio");
      break;
    case Tap::Lyrics:
      v.lyricsOn = !v.lyricsOn;
      if (v.lyricsOn && lyricsAsked != songTitle) askLyrics();
      fullDirty = true;
      break;
    case Tap::Search:
      openKeyboard();
      break;
    case Tap::Mic:
      startMic();
      break;
    case Tap::Clear:
      pageCount = 0;
      v.query[0] = 0;
      v.tab = savedTab[0];
      v.first = savedFirst[0];
      syncView();
      fullDirty = true;
      break;
    case Tap::Mood:
      if (t.index >= 0 && t.index < browse::MOOD_COUNT) sendMusic("mood", "name", browse::MOODS[t.index]);
      break;
    case Tap::Back:
      back();
      break;
    case Tap::Tab:
      v.tab = t.index;
      v.first = 0;
      fullDirty = true;
      break;
    case Tap::Play:
    case Tap::Shuffle:
      sendMusic(t.what == Tap::Play ? "play" : "shuffle", nullptr, nullptr, listId);
      break;
    case Tap::Row:
      sendMusic("pick", nullptr, nullptr, listId, t.index);
      break;
    case Tap::KbKey: {
      const browse::Press p = browse::press(v.kb, t.index);
      if (p == browse::Press::Go) {
        char q[48];
        snprintf(q, sizeof q, "%s", v.kb.text);
        int n = strlen(q);
        while (n > 0 && q[n - 1] == ' ') q[--n] = 0;
        if (!q[0]) break;
        closeKeyboard();
        search(q);
      } else if (p == browse::Press::Keys) {
        kbDirty = true;
      } else if (p == browse::Press::Text) {
        kbFieldDirty = true;
      }
      break;
    }
    case Tap::KbCancel:
      closeKeyboard();
      break;
    case Tap::KbVoice:
      closeKeyboard();
      startMic();
      break;
    default:
      break;
  }
}

float positionNow(const ui::MediaInfo& s, uint32_t now) {
  float p = s.pos;
  if (s.playing) p += (int32_t)(now - s.posAtMs) / 1000.0f;
  if (p < 0) p = 0;
  if (s.dur > 0 && p > s.dur) p = s.dur;
  return p;
}

}  // namespace

bool begin() {
  browse::Item* items = allocate<browse::Item>((size_t)browse::MAX_ITEMS * (PAGES + 1));
  v.lyrics.lines = allocate<browse::LyricLine>(browse::MAX_LYRICS);
  thumbPixels = allocate<uint16_t>((size_t)THUMB_SLOTS * THUMB_PX);
  if (!items || !v.lyrics.lines || !thumbPixels) return false;
  memset(&home, 0, sizeof home);
  home.items = items;
  home.id = 1;
  home.kind = 'l';
  for (int i = 0; i < PAGES; ++i) {
    memset(&pages[i], 0, sizeof pages[i]);
    pages[i].items = items + (size_t)browse::MAX_ITEMS * (i + 1);
  }
  memset(slots, 0, sizeof slots);
  memset(asked, 0, sizeof asked);
  for (int i = 0; i <= PAGES; ++i) savedTab[i] = savedFirst[i] = 0;
  v.home = &home;
  syncView();
  return true;
}

void shown() {
  fullDirty = true;
  progressKey = -2;
  if (v.mode == browse::BROWSE && (!homeAsked || home.count == 0)) askHome();
}

void hidden() {
  if (v.kb.open) {
    v.kb.open = false;  // (showPage sets the bottom bar for the next screen)
  }
  scrolling = false;
  v.pressed = v.pressedRow = -1;
}

void helperConnected(bool on) {
  helperOn = on;
  homeAsked = false;
  voiceActive = false;
  v.voice = browse::VOICE_IDLE;
  memset(asked, 0, sizeof asked);
  if (on && v.mode == browse::BROWSE) askHome();
  if (on && v.lyricsOn && songTitle.length()) askLyrics();
  fullDirty = true;
}

bool onMessage(const char* type, JsonObjectConst msg) {
  if (strcmp(type, "music_list") == 0) {
    const uint32_t id = msg["list"] | 0u;
    const char kind = (msg["kind"] | "q")[0];
    const int offset = msg["offset"] | 0;
    browse::List* l = nullptr;
    if (kind == 'l') {
      l = &home;
      if (offset == 0) home.count = 0;
      if (!pageCount && v.status[0] && statusUntil == 0) v.status[0] = 0;  // "Loading..."
    } else if (offset == 0) {
      if (kind == 'q') {  // a new search starts again from the top
        pageCount = 0;
        v.tab = savedTab[0];
        v.first = savedFirst[0];
      }
      pushPage();
      l = &pages[pageCount - 1];
      l->count = 0;
      v.tab = msg["tab"] | 0;
      v.first = 0;
      if (kind == 'q') copyText(v.query, sizeof v.query, msg["query"] | "");
      v.mode = browse::BROWSE;
    } else {
      for (int i = 0; i < pageCount; ++i)
        if (pages[i].id == id) l = &pages[i];
      if (!l) return true;  // the rest of a list that's gone
    }
    l->id = id;
    l->kind = kind;
    copyText(l->title, sizeof l->title, msg["title"] | "");
    copyText(l->sub, sizeof l->sub, msg["sub"] | "");
    copyText(l->tabs, sizeof l->tabs, msg["tabs"] | "");
    l->total = msg["total"] | 0;
    for (JsonArrayConst row : msg["items"].as<JsonArrayConst>()) {
      if (l->count >= browse::MAX_ITEMS) break;
      browse::Item& it = l->items[l->count++];
      it.kind = (row[0] | "s")[0];
      it.img = row[1] | 0u;
      copyText(it.title, sizeof it.title, row[2] | "");
      copyText(it.sub, sizeof it.sub, row[3] | "");
      copyText(it.right, sizeof it.right, row[4] | "");
    }
    syncView();
    if (v.mode == browse::BROWSE) fullDirty = true;
    return true;
  }
  if (strcmp(type, "music_status") == 0) {
    const char* text = msg["text"] | "";
    const size_t n = strlen(text);
    // "Searching..." stays until the answer; other notes go away by themselves
    setStatus(text, n >= 3 && strcmp(text + n - 3, "...") == 0 ? 15000 : 4000);
    return true;
  }
  if (strcmp(type, "lyrics") == 0) {
    if (lyricsAsked != String((const char*)(msg["title"] | ""))) return true;  // for an older song
    browse::Lyrics& ly = v.lyrics;
    ly.loading = false;
    ly.synced = msg["synced"] | false;
    ly.count = 0;
    ly.current = -1;
    ly.first = 0;
    for (JsonArrayConst line : msg["lines"].as<JsonArrayConst>()) {
      if (ly.count >= browse::MAX_LYRICS) break;
      browse::LyricLine& out = ly.lines[ly.count++];
      out.ms = line[0] | 0u;
      copyText(out.text, sizeof out.text, line[1] | "");
    }
    lyricsDirty = true;
    return true;
  }
  if (voiceActive && strcmp(type, "dictation") == 0) {
    if (strcmp(msg["mode"] | "", "search") == 0) {
      v.mode = browse::BROWSE;
      search(msg["text"] | "");
      fullDirty = true;
    }
    return true;
  }
  if (voiceActive && strcmp(type, "dictate_state") == 0) {
    const char* state = msg["state"] | "idle";
    v.voice = strcmp(state, "listening") == 0  ? browse::VOICE_LISTENING
              : strcmp(state, "thinking") == 0 ? browse::VOICE_THINKING
                                               : browse::VOICE_IDLE;
    if (v.voice == browse::VOICE_IDLE) {
      voiceActive = false;
      if (msg["problem"].is<const char*>()) setStatus(msg["problem"].as<const char*>(), 4000);
    }
    topDirty = true;
    return true;
  }
  return false;
}

void onThumb(uint32_t id, const uint8_t* data, size_t len) {
  if (!thumbPixels || len != (size_t)THUMB_PX * 2 || !id) return;
  int slot = 0;
  for (int i = 0; i < THUMB_SLOTS; ++i) {
    if (slots[i].id == id) {
      slot = i;
      break;
    }
    if (slots[i].used < slots[slot].used) slot = i;
  }
  slots[slot].id = id;
  slots[slot].used = ++useCounter;
  memcpy(thumbPixels + (size_t)slot * THUMB_PX, data, len);
  if (v.mode == browse::BROWSE && !v.kb.open) listDirty = true;
}

void onMedia(const ui::MediaInfo& song, bool newSong) {
  const bool canRadio = song.title.length() > 0;
  if (canRadio != v.canRadio) {
    v.canRadio = canRadio;
    topDirty = true;
  }
  songTitle = song.title;
  songArtist = song.artist;
  if (newSong) {
    v.lyrics.count = 0;
    v.lyrics.loading = false;
    lyricsAsked = "";
    if (v.lyricsOn && songTitle.length()) askLyrics();
    lyricsDirty = true;
  }
}

bool usesTouch(int x, int y) {
  if (v.kb.open) return true;
  if (v.mode == browse::PLAYING) {
    // the song's progress bar is main's (seek); lyrics and the top row are ours
    return y < browse::HEAD_AREA.bottom() || (v.lyricsOn && y >= browse::LYRICS_AREA.y && y < browse::LYRICS_AREA.bottom() && x >= browse::LYRICS_AREA.x);
  }
  return true;
}

void touchBegin(int x, int y) {
  downX = x;
  downY = y;
  scrolling = false;
  startFirst = v.mode == browse::PLAYING ? v.lyrics.first : v.first;
  downTap = browse::tap(x, y, v);
  if (downTap.what == Tap::Row) {
    v.pressedRow = downTap.index;
    listDirty = true;
  } else if (downTap.what == Tap::KbKey) {
    v.kb.pressed = downTap.index;
    kbKeyDirty = downTap.index;
  } else {
    v.pressed = browse::pressedCode(downTap);
    if (v.pressed >= 0) {
      if (y < TOP_PART.bottom()) topDirty = true;
      else listDirty = true;
    }
  }
}

void touchMove(int x, int y) {
  (void)x;
  const int dy = y - downY;
  const bool lyricsScroll = v.mode == browse::PLAYING && v.lyricsOn && !v.lyrics.synced &&
                            downY >= browse::LYRICS_AREA.y && downY < browse::LYRICS_AREA.bottom();
  if (!scrolling && abs(dy) > 14 && (browse::inList(downX, downY, v) || lyricsScroll)) {
    scrolling = true;
    if (v.pressedRow >= 0 || v.pressed >= 0) listDirty = true;
    v.pressedRow = v.pressed = -1;
  }
  if (!scrolling) return;
  if (lyricsScroll) {
    int first = startFirst - (int)lroundf(dy / 31.0f);
    if (first > v.lyrics.count - 4) first = v.lyrics.count - 4;
    if (first < 0) first = 0;
    if (first != v.lyrics.first) {
      v.lyrics.first = first;
      lyricsDirty = true;
    }
    return;
  }
  const int rows = browse::rowCount(v);
  const int shownRows = v.page ? 4 : 3;
  int first = startFirst - (int)lroundf(dy / (float)browse::ROW_STEP);
  if (first > rows - shownRows) first = rows - shownRows;
  if (first < 0) first = 0;
  if (first != v.first) {
    v.first = first;
    listDirty = true;
  }
}

void touchEnd(int x, int y) {
  (void)x;
  (void)y;
  if (v.kb.pressed >= 0) {
    kbKeyDirty = v.kb.pressed;
    v.kb.pressed = -1;
  }
  if (v.pressedRow >= 0 || v.pressed >= 0) {
    if (v.pressed >= 0 && downY < TOP_PART.bottom()) topDirty = true;
    else listDirty = true;
  }
  v.pressedRow = v.pressed = -1;
  if (scrolling) {
    scrolling = false;
    return;
  }
  act(downTap);
}

void touchCancel() {
  if (v.kb.pressed >= 0) kbKeyDirty = v.kb.pressed;
  if (v.pressedRow >= 0 || v.pressed >= 0) topDirty = listDirty = true;
  v.kb.pressed = v.pressedRow = v.pressed = -1;
  scrolling = false;
}

bool blocksSwipe() { return v.kb.open || scrolling; }

void markDirty() { fullDirty = true; }

void draw(uint32_t now, const ui::MediaInfo& song, const char* emptyTitle, const char* emptyHint) {
  if (statusUntil && (int32_t)(now - statusUntil) >= 0) {
    statusUntil = 0;
    v.status[0] = 0;
    listDirty = true;
  }
  auto browseOnly = [&](gfx::Canvas& c) { browse::draw(c, v, thumbOf); };

  if (v.kb.open) {
    if (fullDirty || kbDirty) {
      display::render(browse::KB_AREA, ui::BACKGROUND, browseOnly);
    } else {
      if (kbFieldDirty) display::render(KB_FIELD_AREA, ui::BACKGROUND, browseOnly);
      if (kbKeyDirty >= 0) display::render(gfx::inflate(browse::keyRect(kbKeyDirty), 2), ui::BACKGROUND, browseOnly);
    }
    fullDirty = kbDirty = kbFieldDirty = topDirty = listDirty = lyricsDirty = false;
    kbKeyDirty = -1;
    return;
  }
  kbDirty = kbFieldDirty = false;
  kbKeyDirty = -1;

  if (v.mode == browse::PLAYING) {
    auto both = [&](gfx::Canvas& c) {
      music::draw(c, song, now, emptyTitle, emptyHint, v.lyricsOn);
      browse::draw(c, v, thumbOf);
    };
    if (v.lyricsOn && browse::followLyrics(v.lyrics, (uint32_t)(positionNow(song, now) * 1000))) lyricsDirty = true;
    const int key = music::progressKey(song, now);
    if (fullDirty) {
      display::render(AREA, ui::BACKGROUND, both);
    } else {
      if (topDirty) display::render(browse::HEAD_AREA, ui::BACKGROUND, both);
      if (lyricsDirty && v.lyricsOn) display::render(browse::LYRICS_AREA, ui::BACKGROUND, both);
      if (key != progressKey) display::render(music::PROGRESS_AREA, ui::BACKGROUND, both);
    }
    progressKey = key;
    fullDirty = topDirty = listDirty = lyricsDirty = false;
    return;
  }

  progressKey = -2;
  if (fullDirty) {
    display::render(AREA, ui::BACKGROUND, browseOnly);
  } else {
    if (topDirty) display::render(TOP_PART, ui::BACKGROUND, browseOnly);
    if (listDirty) display::render(browse::LIST_AREA, ui::BACKGROUND, browseOnly);
  }
  if (fullDirty || listDirty) askThumbs();
  fullDirty = topDirty = listDirty = lyricsDirty = false;
}

}  // namespace musicpage
