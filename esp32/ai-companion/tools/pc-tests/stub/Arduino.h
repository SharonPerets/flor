#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <algorithm>
#include <type_traits>
using std::max; using std::min;
extern uint32_t fakeMillis;
inline uint32_t millis() { return fakeMillis; }
inline long random(long n) { return rand() % n; }
struct SerialT { template <class... A> void printf(const char* f, A... a) { ::printf(f, a...); } void println(const char* s) { puts(s); } };
extern SerialT Serial;
class String {
 public:
  std::string s;
  String() {}
  String(const char* p) : s(p ? p : "") {}
  String(const std::string& x) : s(x) {}
  template <class T, class = typename std::enable_if<std::is_arithmetic<T>::value>::type> explicit String(T v) : s(std::to_string(v)) {}
  unsigned length() const { return s.size(); }
  const char* c_str() const { return s.c_str(); }
  char operator[](unsigned i) const { return s[i]; }
  void setCharAt(unsigned i, char c) { s[i] = c; }
  void toLowerCase() { for (auto& c : s) c = tolower(c); }
  bool endsWith(const String& e) const { return s.size() >= e.s.size() && s.compare(s.size() - e.s.size(), e.s.size(), e.s) == 0; }
  void trim() { while (!s.empty() && s.back() == ' ') s.pop_back(); while (!s.empty() && s[0] == ' ') s.erase(0, 1); }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator!=(const String& o) const { return s != o.s; }
};
inline String operator+(const String& a, const String& b) { return String(a.s + b.s); }
inline String operator+(const String& a, const char* b) { return String(a.s + b); }
template <class T, class = typename std::enable_if<std::is_arithmetic<T>::value>::type>
inline String operator+(const String& a, T v) { return String(a.s + std::to_string(v)); }
