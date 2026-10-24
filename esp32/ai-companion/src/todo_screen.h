// The To-do screen: Today / Upcoming / Done lists with round checkboxes, a big
// "Add by voice" button and an on-screen keyboard as a backup.
//
// This part only draws and handles taps, so it also runs in the PC preview.
// todos.cpp keeps the tasks; main.cpp ties them together.
#pragma once

#include <stdint.h>

#include "gfx.h"
#include "todos.h"

namespace todoscreen {

constexpr int ROWS = 5;  // rows shown at once (drag up / down for more)

// The whole screen below the top bar (no music bar on this screen)
constexpr gfx::Rect AREA{0, 44, 800, 436};
constexpr gfx::Rect HEADER_AREA{0, 44, 800, 60};
constexpr gfx::Rect LIST_AREA{0, 104, 800, 300};
constexpr gfx::Rect BOTTOM_AREA{0, 404, 800, 76};
constexpr int ROW_Y = 106, ROW_STEP = 58, ROW_H = 54;

// Keyboard parts
constexpr gfx::Rect KB_FIELD_AREA{0, 44, 800, 72};
constexpr gfx::Rect KB_WHEN_AREA{0, 116, 800, 48};
constexpr gfx::Rect KB_KEYS_AREA{0, 164, 800, 244};
constexpr gfx::Rect KB_BOTTOM_AREA{0, 408, 800, 72};

enum Voice : uint8_t { VOICE_IDLE, VOICE_LISTENING, VOICE_THINKING };

struct Row {
  uint32_t id;
  char text[todos::TEXT_MAX + 1];
  char chip[16];
  uint8_t chipKind;  // 0 plain, 1 late (red), 2 reminder time
  bool star, done, fresh;
};

// Keys: 0-9 top row, 10-18 middle, 19 Shift, 20-26 bottom letters, 27 Delete,
// 28 "123" / "ABC", 29 space, 30 "Add task"
constexpr int KEY_COUNT = 31;
constexpr int KEY_SHIFT = 19, KEY_DELETE = 27, KEY_SYMBOLS = 28, KEY_SPACE = 29, KEY_ADD = 30;
// The "When" choices on the keyboard: 0 = none picked
enum When : uint8_t { WHEN_NONE, WHEN_TODAY, WHEN_TOMORROW, WHEN_NEXT_WEEK, WHEN_SOMEDAY, WHEN_COUNT };

struct Keyboard {
  bool open = false;
  char text[todos::TEXT_MAX + 1] = "";
  uint8_t when = WHEN_NONE;
  bool star = false;
  bool shift = true;      // the next letter is a capital
  bool symbols = false;   // numbers and signs instead of letters
  int pressed = -1;       // key under the finger
  uint8_t voice = VOICE_IDLE;
  const char* note = "";  // shown in the voice button for a moment ("PC not connected")
};

struct View {
  uint8_t tab = todos::TODAY;
  int counts[3] = {0, 0, 0};  // tasks in Today / Upcoming / Done
  Row rows[ROWS];
  int rowCount = 0;
  int first = 0;       // scrolled: the first row shown
  int total = 0;       // rows in this list
  int openRow = -1;    // the row showing its buttons (Star, Tomorrow, Delete...)
  bool allDone = false;  // Today is empty because everything got ticked today
  char toast[72] = "";   // a short message over the list ("Done: Buy milk")
  bool toastUndo = false;  // ...with an Undo button
  uint8_t voice = VOICE_IDLE;
  Keyboard kb;
};

// Fills the rows of the current tab from the list (keeps tab, first, openRow; fixes them if out of range)
void fill(View& v, int32_t today, uint32_t freshId);

void draw(gfx::Canvas& c, const View& v);

enum class Tap {
  None, Tab, Check, Star, Row, Action, Undo, Voice, Type, Elsewhere,
  KbKey, KbWhen, KbStar, KbCancel, KbVoice
};
// Row buttons (when a row is open)
enum Action : uint8_t { ACT_STAR, ACT_TODAY, ACT_TOMORROW, ACT_DELETE, ACT_NOT_DONE };
struct TapResult {
  Tap what;
  int index;  // tab, row (0 = first row shown), key, when choice
  int action;
  TapResult(Tap w = Tap::None, int i = 0, int a = 0) : what(w), index(i), action(a) {}
};
TapResult tap(int x, int y, const View& v);

// The key at (x, y), or -1 (the keyboard must be open)
int keyAt(int x, int y);
gfx::Rect keyRect(int key);

// What a key press does to the text. Returns true for "Add task".
enum class Press { None, Text, Keys, Add };  // Keys: the key labels changed (Shift, 123)
Press press(Keyboard& kb, int key);

}  // namespace todoscreen
