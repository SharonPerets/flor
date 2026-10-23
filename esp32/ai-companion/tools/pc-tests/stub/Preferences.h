#pragma once
#include <map>
#include <string>
#include <vector>
#include <stdio.h>
// Keeps everything in a file, so a second run sees what the first one saved
class Preferences {
  std::map<std::string, std::vector<uint8_t>> m;
  void load() { m.clear(); FILE* f = fopen("prefs.bin", "rb"); if (!f) return; char k[32]; uint32_t n; while (fread(k, 1, 32, f) == 32 && fread(&n, 4, 1, f) == 1) { std::vector<uint8_t> v(n); fread(v.data(), 1, n, f); m[k] = v; } fclose(f); }
  void store() { FILE* f = fopen("prefs.bin", "wb"); for (auto& kv : m) { char k[32] = {}; snprintf(k, 32, "%s", kv.first.c_str()); uint32_t n = kv.second.size(); fwrite(k, 1, 32, f); fwrite(&n, 4, 1, f); fwrite(kv.second.data(), 1, n, f); } fclose(f); }
 public:
  bool begin(const char*, bool) { load(); return true; }
  size_t putBytes(const char* k, const void* p, size_t n) { m[k].assign((const uint8_t*)p, (const uint8_t*)p + n); store(); return n; }
  size_t getBytesLength(const char* k) { return m.count(k) ? m[k].size() : 0; }
  size_t getBytes(const char* k, void* p, size_t n) { if (!m.count(k)) return 0; memcpy(p, m[k].data(), std::min(n, m[k].size())); return n; }
  void putInt(const char* k, int v) { putBytes(k, &v, 4); }
  int getInt(const char* k, int d) { if (getBytesLength(k) != 4) return d; int v; getBytes(k, &v, 4); return v; }
};
