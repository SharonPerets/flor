#include <assert.h>
#include <time.h>
#include "alarms.h"
uint32_t fakeMillis = 1000;  // test clock (see stub/Arduino.h)
SerialT Serial;
extern time_t fakeNow;
static time_t local(int y, int mo, int d, int h, int mi) { struct tm t = {}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1; return mktime(&t); }
static void step(uint32_t ms) { // move both clocks forward, running the loop like Gorgi does
  for (uint32_t t = 0; t < ms; t += 250) { fakeMillis += 250; if (fakeMillis % 1000 < 250) fakeNow += 1; alarms::loop(fakeMillis); }
}
static void dueAll(const char* label) { alarms::Due d; while (alarms::takeDue(d)) printf("  [%s] DUE kind=%d: %s\n", label, (int)d.kind, d.text.c_str()); }
static void show(const char* label) {
  timescreen::View v; alarms::fillView(v, fakeMillis);
  printf("--- %s: %d rings, %d rows (+%d), focusToday %d, full %d\n", label, v.ringCount, v.rowCount, v.moreRows, v.focusToday, v.timersFull);
  for (int i = 0; i < v.ringCount; ++i) printf("  ring %u kind %d '%s' left %u/%u paused %d\n", v.rings[i].id, v.rings[i].kind, v.rings[i].label, v.rings[i].left, v.rings[i].total, v.rings[i].paused);
  for (int i = 0; i < v.rowCount; ++i) printf("  row %u '%s' | '%s'\n", v.rows[i].id, v.rows[i].when, v.rows[i].text);
}
int main(int argc, char** argv) {
  setenv("TZ", "IST-2IDT,M3.4.4/26,M10.5.0", 1); tzset();
  const char mode = argv[1][0];
  if (mode == 'a') {
    remove("prefs.bin");
    fakeNow = local(2026, 10, 21, 9, 0);  // Wednesday 09:00
    alarms::begin(); alarms::loop(fakeMillis);
    assert(alarms::addTimer(5, "pasta") == alarms::Result::Ok);
    assert(alarms::addTimer(600, nullptr) == alarms::Result::Ok);
    assert(alarms::addTimer(60, "egg timer") == alarms::Result::Ok);
    assert(alarms::addTimer(60, "x") == alarms::Result::Full);
    assert(alarms::startFocus(3) == alarms::Result::Ok);
    assert(alarms::startFocus(3) == alarms::Result::Full);
    assert(alarms::remindAt(fakeNow + 90, "take the laundry out") == alarms::Result::Ok);
    assert(alarms::remindAt(local(2026, 10, 21, 17, 0), "call mom") == alarms::Result::Ok);
    assert(alarms::remindAt(local(2026, 10, 22, 8, 0), "dentist") == alarms::Result::Ok);
    assert(alarms::remindAt(local(2026, 10, 24, 8, 0), "market") == alarms::Result::Ok);
    assert(alarms::remindAt(local(2026, 11, 3, 8, 0), "pay bills") == alarms::Result::Ok);
    assert(alarms::remindRepeat(8 * 60, 0x7F, "take vitamins") == alarms::Result::Ok);
    assert(alarms::remindRepeat(7 * 60 + 30, 0x1F, "gym") == alarms::Result::Ok);
    assert(alarms::remindRepeat(20 * 60, 0x0A, "trash") == alarms::Result::Ok);
    assert(alarms::remindAt(fakeNow - 3600, "past") == alarms::Result::Bad);
    show("start");
    step(6000); dueAll("6s");
    show("after 6 s");
    // pause the 10 min timer, wait, resume
    timescreen::View v; alarms::fillView(v, fakeMillis);
    uint32_t tid = v.rings[0].id;
    alarms::togglePause(tid); step(5000); alarms::fillView(v, fakeMillis); printf("paused left %u\n", v.rings[0].left);
    alarms::togglePause(tid);
    step(95000); dueAll("95s");
    show("after laundry");
    JsonDocument doc; alarms::toJson(doc, fakeMillis); { std::string out; serializeJson(doc, out); printf("%s\n", out.c_str()); }
    printf("focusToday counted\n");
  } else if (mode == 'b') {
    // Gorgi restarted much later: Thursday 10:00 (clock unknown at first)
    fakeNow = 0; fakeMillis = 500;
    alarms::begin(); alarms::loop(fakeMillis); show("restart, no clock yet");
    fakeNow = local(2026, 10, 22, 10, 0); step(500); dueAll("clock known");
    show("restart, clock known");
  } else if (mode == 'c') {
    // DST: Israel winter time starts Sun 25 Oct 2026 02:00. Daily 08:00 must stay 08:00.
    remove("prefs.bin");
    fakeNow = local(2026, 10, 24, 9, 0);
    alarms::begin(); alarms::loop(fakeMillis);
    alarms::remindRepeat(8 * 60, 0x7F, "vitamins");
    for (int day = 0; day < 3; ++day) {
      timescreen::View v; alarms::fillView(v, fakeMillis);
      JsonDocument doc; alarms::toJson(doc, fakeMillis);
      fakeNow = local(2026, 10, 25 + day, 7, 59); step(1000); step(60000);
      alarms::Due d; bool got = alarms::takeDue(d); struct tm lt; localtime_r(&fakeNow, &lt);
      printf("day %d: due=%d at local %02d:%02d %s\n", day, got, lt.tm_hour, lt.tm_min, got ? d.text.c_str() : "");
    }
  }
}
