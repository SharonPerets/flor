#include "sketch_store.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "sketch.h"

namespace sketchstore {
namespace {

constexpr const char* DIR = "/sketch";
constexpr int ROWS_PER_PIECE = 8;
bool mounted = false;

String pathOf(uint32_t id) { return String(DIR) + "/" + id + ".gsk"; }

bool readFile(uint32_t id, sketch::Unpacker& u) {
  if (!mounted) return false;
  File f = LittleFS.open(pathOf(id), "r");
  if (!f) return false;
  uint8_t head[8];
  if (f.read(head, 8) != 8 || memcmp(head, "GSK1", 4) != 0 || (head[4] | head[5] << 8) != sketch::PAPER_W ||
      (head[6] | head[7] << 8) != sketch::PAPER_H) {
    f.close();
    return false;
  }
  uint8_t buf[2048];
  int n;
  while ((n = f.read(buf, sizeof buf)) > 0) u.feed(buf, n);
  f.close();
  return u.complete();
}

}  // namespace

bool begin() {
  const uint32_t t0 = millis();
  mounted = LittleFS.begin(true);  // formats the partition if it was never used
  if (!mounted) {
    Serial.println("sketch: file system not available");
    return false;
  }
  if (!LittleFS.exists(DIR)) LittleFS.mkdir(DIR);
  Serial.printf("sketch: files ready (%u KB used of %u KB, %u ms)\n", (unsigned)(LittleFS.usedBytes() / 1024),
                (unsigned)(LittleFS.totalBytes() / 1024), (unsigned)(millis() - t0));
  return true;
}

int list(uint32_t* ids, int max) {
  if (!mounted) return 0;
  int n = 0;
  File dir = LittleFS.open(DIR);
  if (!dir) return 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();  // "12.gsk" (or a full path on some versions)
    f.close();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (!name.endsWith(".gsk")) continue;
    const uint32_t id = (uint32_t)name.toInt();
    if (!id) continue;
    // keep the newest `max`, sorted newest first
    int at = n;
    while (at > 0 && ids[at - 1] < id) --at;
    if (at >= max) continue;
    for (int k = (n < max ? n : max - 1); k > at; --k) ids[k] = ids[k - 1];
    ids[at] = id;
    if (n < max) ++n;
  }
  dir.close();
  return n;
}

bool load(uint32_t id, uint16_t* picture) {
  sketch::Unpacker u(picture);
  return readFile(id, u);
}

bool loadThumb(uint32_t id, uint16_t* thumb) {
  sketch::Unpacker u = sketch::Unpacker::forThumb(thumb);
  return readFile(id, u);
}

bool save(uint32_t id, const uint16_t* picture) {
  if (!mounted) return false;
  const uint32_t t0 = millis();
  static uint8_t* piece = nullptr;  // room for the worst case of ROWS_PER_PIECE rows
  if (!piece) piece = (uint8_t*)malloc((size_t)ROWS_PER_PIECE * sketch::PAPER_W * 4);
  if (!piece) return false;
  const String tmp = String(DIR) + "/new.tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const uint8_t head[8] = {'G', 'S', 'K', '1', (uint8_t)sketch::PAPER_W, (uint8_t)(sketch::PAPER_W >> 8),
                           (uint8_t)sketch::PAPER_H, (uint8_t)(sketch::PAPER_H >> 8)};
  bool ok = f.write(head, 8) == 8;
  size_t total = 8;
  for (int y = 0; ok && y < sketch::PAPER_H; y += ROWS_PER_PIECE) {
    const int y1 = y + ROWS_PER_PIECE < sketch::PAPER_H ? y + ROWS_PER_PIECE : sketch::PAPER_H;
    const size_t n = sketch::packRows(picture, y, y1, piece);
    ok = f.write(piece, n) == n;
    total += n;
  }
  f.close();
  if (!ok) {
    LittleFS.remove(tmp);
    Serial.println("sketch: saving failed (full?)");
    return false;
  }
  LittleFS.remove(pathOf(id));
  LittleFS.rename(tmp, pathOf(id));
  Serial.printf("sketch: saved %u (%u KB, %u ms)\n", (unsigned)id, (unsigned)(total / 1024), (unsigned)(millis() - t0));

  // keep only the newest ones
  uint32_t ids[sketch::MAX_SKETCHES + 4];
  const int n = list(ids, sketch::MAX_SKETCHES + 4);
  for (int i = sketch::MAX_SKETCHES; i < n; ++i)
    if (ids[i] != id) remove(ids[i]);
  return true;
}

void remove(uint32_t id) {
  if (!mounted) return;
  LittleFS.remove(pathOf(id));
  Serial.printf("sketch: removed %u\n", (unsigned)id);
}

}  // namespace sketchstore
