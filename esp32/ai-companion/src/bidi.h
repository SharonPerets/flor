// Hebrew (right-to-left) text helpers: reading UTF-8 letters and putting a line in drawing order.
//
// Text is kept in reading order everywhere (as typed or spoken). Only when a line is drawn is it
// turned into drawing order (left to right on the screen): Hebrew runs reversed, English words and
// numbers inside them kept readable. A small version of the Unicode bidi rules, enough for one line.
//
// Pure C++ (no Arduino), so the PC previews use it too.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bidi {

// Reads one UTF-8 letter at s and moves s past it. Returns 0 at the end of the text.
uint32_t next(const char*& s);

// Length in bytes after taking the last letter off a text of len bytes (never cuts a letter in half).
size_t dropLast(const char* s, size_t len);

// Length without a half letter at the end (when a copy ran out of room in the middle of one).
size_t trimPartial(const char* s, size_t len);

// The largest length <= max that does not cut a letter in half.
size_t safeCut(const char* s, size_t max);

bool isHebrew(uint32_t cp);
bool hasRtl(const char* s);  // any Hebrew letter
bool isRtl(const char* s);   // the first real letter is Hebrew: the line reads right to left

// Writes the line in drawing order into out (size n, always ends with 0). Returns out.
// Text without Hebrew is copied as it is.
const char* visual(const char* s, char* out, size_t n);

}  // namespace bidi
