// Saves and loads sketches in Gorgi's flash (LittleFS, the 1.5 MB "spiffs"
// partition). Each sketch is one small packed file, /sketch/<id>.gsk; the
// newest MAX_SKETCHES are kept.
#pragma once

#include <stdint.h>

namespace sketchstore {

// Mounts the file system (formats it the very first time, which takes a few seconds).
bool begin();

// Saved sketches, newest first. Returns how many (at most max).
int list(uint32_t* ids, int max);

bool load(uint32_t id, uint16_t* picture);   // PAPER_W x PAPER_H
bool loadThumb(uint32_t id, uint16_t* thumb);  // THUMB_W x THUMB_H
// Saves, then removes the oldest sketches beyond the limit (never this one).
bool save(uint32_t id, const uint16_t* picture);
void remove(uint32_t id);

}  // namespace sketchstore
