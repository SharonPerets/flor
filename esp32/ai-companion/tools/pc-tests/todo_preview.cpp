// Checks the to-do list (days, sorting, chips, saving) and draws the To-do screen into .ppm pictures.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "todo_screen.h"
#include "todos.h"

#include <Preferences.h>

uint32_t fakeMillis = 0;
SerialT Serial;

static void save(const char* name, const todoscreen::View& v) {
  const int W = 800, H = 480;
  std::vector<uint16_t> buf(W * H);
  gfx::Canvas c;
  c.begin(buf.data(), W, H, 0, 0);
  c.clear(gfx::rgb(0x05070A));
  c.fillRect(0, 43, 800, 1, gfx::rgb(0x161D27));  // the top bar's line
  todoscreen::draw(c, v);
  FILE* f = fopen(name, "wb");
  fprintf(f, "P6 %d %d 255\n", W, H);
  for (uint16_t p : buf) {
    unsigned char o[3] = {(unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2),
                          (unsigned char)((p & 31) << 3)};
    fwrite(o, 1, 3, f);
  }
  fclose(f);
}

int main() {
  setenv("TZ", "IST-2IDT,M3.4.4/26,M10.5.0", 1);
  tzset();
  remove("prefs.bin");
  // days
  assert(todos::dayFromDate(1970, 1, 1) == 0);
  assert(todos::weekday(0) == 4);                                // a Thursday
  const int32_t today = todos::dayFromDate(2026, 10, 24);        // a Saturday
  assert(todos::weekday(today) == 6);
  int y, m, d;
  todos::dateOf(today, y, m, d);
  assert(y == 2026 && m == 10 && d == 24);
  assert(todos::parseDate("2026-10-25") == today + 1);
  assert(todos::parseDate("tomorrow") == todos::NO_DAY);
  const time_t noon = 1792832400;  // 2026-10-24 12:00 Israel time (UTC+3)
  assert(todos::dayOf(noon) == today);

  todos::begin();
  uint32_t id = 0;
  using todos::add;
  assert(add("call the bank about the card", today, false, true, 0, &id) == todos::Result::Ok);
  add("Review Dana's pull request", today - 2, false, false, 0);
  add("Book the dentist", today, false, false, noon + 5 * 3600);  // reminder at 17:00
  add("Send the weekly report", todos::NO_DAY, false, false, 0, &id);
  todos::setDone(id, true, noon);
  add("Buy milk and eggs", today + 1, false, false, 0);
  add("Pay the electricity bill", today + 1 + 0, false, false, 0);
  add("Renew the car insurance", todos::dayFromDate(2026, 11, 2), false, true, 0);
  add("Plan Noa's birthday party with the whole family and friends from work", todos::dayFromDate(2026, 11, 9), false, false, 0);
  add("Learn to juggle", todos::NO_DAY, true, false, 0);
  add("Water the plants", todos::NO_DAY, false, false, 0);
  assert(add("   ", todos::NO_DAY, false, false, 0) == todos::Result::Bad);

  const todos::Task* list[64];
  int n = todos::collect(todos::TODAY, today, list, 64);
  printf("Today:");
  for (int i = 0; i < n; ++i) printf(" [%s]", list[i]->text);
  printf("\n");
  assert(n == 4 && strcmp(list[0]->text, "Call the bank about the card") == 0);  // starred first, a capital
  assert(strcmp(list[1]->text, "Review Dana's pull request") == 0);              // then late
  n = todos::collect(todos::UPCOMING, today, list, 64);
  printf("Upcoming:");
  for (int i = 0; i < n; ++i) printf(" [%s]", list[i]->text);
  printf("\n");
  assert(n == 5 && strcmp(list[4]->text, "Learn to juggle") == 0);  // someday last
  n = todos::collect(todos::FINISHED, today, list, 64);
  assert(n == 1);

  // chips
  char chip[16];
  uint8_t kind;
  n = todos::collect(todos::TODAY, today, list, 64);
  for (int i = 0; i < n; ++i) {
    todos::chipFor(*list[i], today, chip, sizeof chip, kind);
    printf("  %-40s chip '%s' (%d)\n", list[i]->text, chip, kind);
  }
  todos::chipFor(*list[1], today, chip, sizeof chip, kind);
  assert(!strcmp(chip, "Late") && kind == 1);
  todos::chipFor(*list[2], today, chip, sizeof chip, kind);
  assert(!strcmp(chip, "17:00") && kind == 2);

  // saved and loaded again
  todos::saveNow();
  const uint32_t ver = todos::version();
  todos::begin();
  assert(todos::openCount() == 9);
  (void)ver;
  // old finished tasks go after a week
  todos::tidy(noon + 8 * 24 * 3600);
  assert(todos::collect(todos::FINISHED, today, list, 64) == 0);

  // JSON for the helper
  JsonDocument doc;
  todos::toJson(doc, today);
  char json[4096];
  serializeJson(doc, json, sizeof json);
  printf("json: %.200s...\n", json);

  // keyboard
  todoscreen::Keyboard kb;
  const char* typed = "book";
  for (const char* p = typed; *p; ++p) {
    const char* row = strchr("qwertyuiop", *p) ? "qwertyuiop" : strchr("asdfghjkl", *p) ? "asdfghjkl" : "zxcvbnm";
    const int base = row[0] == 'q' ? 0 : row[0] == 'a' ? 10 : 20;
    todoscreen::press(kb, base + (int)(strchr(row, *p) - row));
  }
  assert(!strcmp(kb.text, "Book"));
  todoscreen::press(kb, todoscreen::KEY_SPACE);
  todoscreen::press(kb, todoscreen::KEY_SPACE);  // no double spaces
  assert(!strcmp(kb.text, "Book "));
  todoscreen::press(kb, todoscreen::KEY_DELETE);
  assert(!strcmp(kb.text, "Book"));
  assert(todoscreen::keyAt(40, 190) == 0 && todoscreen::keyAt(700, 370) == todoscreen::KEY_ADD);

  // pictures
  todoscreen::View v;
  todoscreen::fill(v, today, 0);
  snprintf(v.toast, sizeof v.toast, "Done: Send the weekly report");
  v.toastUndo = true;
  save("todo_today.ppm", v);
  v.toast[0] = 0;
  v.openRow = 2;
  save("todo_open.ppm", v);
  v.openRow = -1;
  v.tab = todos::UPCOMING;
  todoscreen::fill(v, today, list[0] ? 0 : 0);
  v.voice = todoscreen::VOICE_LISTENING;
  save("todo_upcoming.ppm", v);
  for (int i = 0; i < 6; ++i) add("Another thing to do", today + 3, false, false, 0);
  v.voice = todoscreen::VOICE_IDLE;
  v.first = 3;
  todoscreen::fill(v, today, 0);
  save("todo_scrolled.ppm", v);
  todoscreen::View e;
  e.allDone = true;
  save("todo_empty.ppm", e);
  todoscreen::View k;
  k.kb = kb;
  k.kb.open = true;
  k.kb.when = todoscreen::WHEN_TOMORROW;
  k.kb.pressed = 14;
  save("todo_keyboard.ppm", k);
  k.kb.symbols = true;
  k.kb.voice = todoscreen::VOICE_LISTENING;
  save("todo_keyboard_123.ppm", k);

  // 4.6 lists (fixed 56-byte text) load into the new format
  {
    struct V1 { uint32_t id; int32_t due; uint32_t remindAt, alarmId, doneAt; uint8_t flags; char text[57]; };
    uint8_t blob[8 + 2 * sizeof(V1)] = {};
    const uint32_t magic = 0x31444F54, next = 9;
    memcpy(blob, &magic, 4);
    memcpy(blob + 4, &next, 4);
    V1 a = {}, b = {};
    a.id = 3; a.due = today; a.flags = 1; snprintf(a.text, sizeof a.text, "Old task one");
    b.id = 7; b.due = todos::NO_DAY; snprintf(b.text, sizeof b.text, "Old task two");
    memcpy(blob + 8, &a, sizeof a);
    memcpy(blob + 8 + sizeof(V1), &b, sizeof b);
    Preferences p;
    p.begin("todo", false);
    p.putBytes("list", blob, sizeof blob);
    todos::begin();
    assert(todos::openCount() == 2);
    n = todos::collect(todos::TODAY, today, list, 64);
    assert(n == 2 && !strcmp(list[0]->text, "Old task one") && list[0]->star());  // (no-day tasks show on Today too)
    todos::begin();  // saved again in the new format
    assert(todos::openCount() == 2);
  }

  // Hebrew tasks: kept as UTF-8, long ones cut at a whole letter, drawn right to left
  remove("prefs.bin");
  todos::begin();
  add("\xD7\x9C\xD7\xA7\xD7\xA0\xD7\x95\xD7\xAA \xD7\x97\xD7\x9C\xD7\x91", today, false, false, 0);  // "לקנות חלב"
  add("להתקשר לבנק על הכרטיס", today, false, true, 0);
  add("לקבוע תור לרופא שיניים", today, false, false, noon + 5 * 3600);
  add("Send the PR to Dana", today, false, false, 0);
  add("להכין מצגת ל-Sprint review ב-10:30", today - 1, false, false, 0);
  char longText[300] = "";
  for (int i = 0; i < 12; ++i) strcat(longText, "משימה ");  // 72 letters, 132 bytes
  add(longText, today + 1, false, false, 0);
  n = todos::collect(todos::UPCOMING, today, list, 64);
  assert(n == 1 && strlen(list[0]->text) <= (size_t)todos::TEXT_MAX && strlen(list[0]->text) % 2 == 0);
  todos::saveNow();
  todos::begin();
  assert(todos::openCount() == 6);
  n = todos::collect(todos::TODAY, today, list, 64);
  assert(!strcmp(list[0]->text, "להתקשר לבנק על הכרטיס"));
  todoscreen::View h;
  todoscreen::fill(h, today, 0);
  snprintf(h.toast, sizeof h.toast, "Done: %s", "לשלם חשבון חשמל");
  h.toastUndo = true;
  save("todo_hebrew.ppm", h);
  printf("to-do: ALL OK (pictures todo_*.ppm)\n");
}
