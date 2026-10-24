// Checks the right-to-left line ordering (bidi.cpp). Prints each line in drawing order.
#include <stdio.h>
#include <string.h>

#include "bidi.h"

static int fails = 0;

static void check(const char* in, const char* want) {
  char out[512];
  bidi::visual(in, out, sizeof out);
  bool ok = strcmp(out, want) == 0;
  if (!ok) ++fails;
  printf("%s  [%s] -> [%s]%s%s\n", ok ? "ok  " : "FAIL", in, out, ok ? "" : "  want ", ok ? "" : want);
}

int main() {
  check("Hello world", "Hello world");
  check("שלום", "םולש");
  check("שלום עולם", "םלוע םולש");
  check("Now playing: שיר", "Now playing: ריש");
  check("לקנות חלב ב-17:00", "17:00-ב בלח תונקל");
  check("להתקשר ל-Dana היום", "םויה Dana-ל רשקתהל");
  check("עומר אדם - Hello World 2", "Hello World 2 - םדא רמוע");
  check("(שלום)", "(םולש)");
  check("יש לי 3 משימות", "תומישמ 3 יל שי");
  check("Song (מילים) end", "Song (םילימ) end");
  // letter-safe cutting
  const char* h = "שלום";
  printf("%s  dropLast\n", bidi::dropLast(h, strlen(h)) == 6 ? "ok  " : (++fails, "FAIL"));
  printf("%s  safeCut\n", bidi::safeCut(h, 5) == 4 ? "ok  " : (++fails, "FAIL"));
  printf("%s  isRtl\n", bidi::isRtl("12 שלום Hi") && !bidi::isRtl("Hi שלום") ? "ok  " : (++fails, "FAIL"));
  printf(fails ? "%d FAILED\n" : "all bidi checks passed\n", fails);
  return fails ? 1 : 0;
}
