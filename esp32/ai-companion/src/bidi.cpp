#include "bidi.h"

#include <string.h>

namespace bidi {

uint32_t next(const char*& s) {
  const uint8_t* p = (const uint8_t*)s;
  uint8_t c = *p;
  if (!c) return 0;
  uint32_t cp;
  int len;
  if (c < 0x80) { cp = c; len = 1; }
  else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
  else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
  else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
  else { s++; return 0xFFFD; }  // a stray byte
  for (int k = 1; k < len; ++k) {
    if ((p[k] & 0xC0) != 0x80) { s += k; return 0xFFFD; }  // cut short
    cp = (cp << 6) | (p[k] & 0x3F);
  }
  s += len;
  return cp;
}

size_t dropLast(const char* s, size_t len) {
  if (!len) return 0;
  size_t i = len - 1;
  while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) --i;  // back over continuation bytes
  return i;
}

size_t trimPartial(const char* s, size_t len) {
  if (!len) return 0;
  size_t i = len - 1;
  while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) --i;  // the last letter starts at i
  const uint8_t c = (uint8_t)s[i];
  const size_t need = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
  return i + need > len ? i : len;
}

size_t safeCut(const char* s, size_t max) {
  size_t n = strlen(s);
  if (n <= max) return n;
  size_t i = max;
  while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) --i;  // s[i] starts the letter that would be cut
  return i;
}

bool isHebrew(uint32_t cp) { return (cp >= 0x0590 && cp <= 0x05FF) || (cp >= 0xFB1D && cp <= 0xFB4F); }

static bool isLatinLetter(uint32_t cp) {
  return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= 0xC0 && cp < 0x0590 && cp != 0xD7 && cp != 0xF7);
}

bool hasRtl(const char* s) {
  for (const uint8_t* p = (const uint8_t*)s; *p; ++p)
    if (*p == 0xD6 || *p == 0xD7) {  // lead bytes of U+0580..U+05FF
      const char* q = (const char*)p;
      if (isHebrew(next(q))) return true;
    }
  return false;
}

bool isRtl(const char* s) {
  while (uint32_t cp = next(s)) {
    if (isHebrew(cp)) return true;
    if (isLatinLetter(cp)) return false;
  }
  return false;
}

namespace {

enum Kind : uint8_t { L, R, EN, N };

uint32_t mirror(uint32_t cp) {
  switch (cp) {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    case '<': return '>';
    case '>': return '<';
    default: return cp;
  }
}

size_t put(char* out, size_t at, size_t n, uint32_t cp) {
  char b[4];
  int len;
  if (cp < 0x80) { b[0] = (char)cp; len = 1; }
  else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); len = 2; }
  else if (cp < 0x10000) {
    b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); len = 3;
  } else {
    b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); len = 4;
  }
  if (at + len >= n) return at;  // no room: stop adding
  memcpy(out + at, b, len);
  return at + len;
}

}  // namespace

const char* visual(const char* s, char* out, size_t n) {
  if (!n) return out;
  if (!hasRtl(s)) {
    size_t len = safeCut(s, n - 1);
    memcpy(out, s, len);
    out[len] = 0;
    return out;
  }
  const int MAXC = 256;
  uint32_t cps[MAXC];
  uint8_t kind[MAXC], level[MAXC];
  int count = 0;
  for (const char* p = s; count < MAXC;) {
    uint32_t cp = next(p);
    if (!cp) break;
    cps[count] = cp;
    kind[count] = isHebrew(cp) ? R : (cp >= '0' && cp <= '9') ? EN : isLatinLetter(cp) ? L : N;
    ++count;
  }
  // The line's direction comes from its first real letter.
  Kind base = L;
  for (int i = 0; i < count; ++i)
    if (kind[i] == L || kind[i] == R) { base = (Kind)kind[i]; break; }

  // "17:00", "3.5", "1,000": a separator between two digits belongs to the number.
  for (int i = 1; i + 1 < count; ++i)
    if (kind[i] == N && kind[i - 1] == EN && kind[i + 1] == EN &&
        (cps[i] == ':' || cps[i] == '.' || cps[i] == ',' || cps[i] == '/'))
      kind[i] = EN;
  // Numbers after an English word act like English.
  Kind lastStrong = base;
  for (int i = 0; i < count; ++i) {
    if (kind[i] == L || kind[i] == R) lastStrong = (Kind)kind[i];
    else if (kind[i] == EN && lastStrong == L) kind[i] = L;
  }
  // Spaces and marks take the direction of what is around them if both sides agree, else the line's.
  for (int i = 0; i < count;) {
    if (kind[i] != N) { ++i; continue; }
    int j = i;
    while (j < count && kind[j] == N) ++j;
    Kind before = i > 0 ? (kind[i - 1] == EN ? R : (Kind)kind[i - 1]) : base;
    Kind after = j < count ? (kind[j] == EN ? R : (Kind)kind[j]) : base;
    Kind k = before == after ? before : base;
    for (int t = i; t < j; ++t) kind[t] = k;
    i = j;
  }
  // Levels: even = left to right, odd = right to left.
  uint8_t maxLevel = 0;
  for (int i = 0; i < count; ++i) {
    if (base == L) level[i] = kind[i] == R ? 1 : kind[i] == EN ? 2 : 0;
    else level[i] = kind[i] == R ? 1 : 2;
    if (level[i] > maxLevel) maxLevel = level[i];
  }
  // Reverse every run at each level, highest first.
  for (uint8_t lv = maxLevel; lv >= 1; --lv) {
    for (int i = 0; i < count;) {
      if (level[i] < lv) { ++i; continue; }
      int j = i;
      while (j < count && level[j] >= lv) ++j;
      for (int a = i, b = j - 1; a < b; ++a, --b) {
        uint32_t tc = cps[a]; cps[a] = cps[b]; cps[b] = tc;
        uint8_t tl = level[a]; level[a] = level[b]; level[b] = tl;
      }
      i = j;
    }
  }
  size_t at = 0;
  for (int i = 0; i < count; ++i) at = put(out, at, n, (level[i] & 1) ? mirror(cps[i]) : cps[i]);
  out[at] = 0;
  return out;
}

}  // namespace bidi
