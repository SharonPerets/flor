#include "ui.h"

namespace ui {
namespace {

constexpr uint16_t BG = rgb565(0x05070A);
constexpr uint16_t PANEL = rgb565(0x10151C);
constexpr uint16_t BORDER = rgb565(0x1F2935);
constexpr uint16_t BTN = rgb565(0x151C25);
constexpr uint16_t BTN_PRESSED = rgb565(0x2A3646);
constexpr uint16_t TILE = rgb565(0x1C2733);
constexpr uint16_t TEXT = rgb565(0xE8EEF4);
constexpr uint16_t TEXT_TITLE = rgb565(0xF2F6FA);
constexpr uint16_t TEXT_MUTED = rgb565(0x9AA7B5);
constexpr uint16_t TRACK = rgb565(0x2A3441);
constexpr uint16_t PLAY_ICON = rgb565(0x05201E);
constexpr uint16_t ACCENT_PRESSED = rgb565(0x8FF0E8);

// Subtitle area
constexpr int SUB_X = 16, SUB_Y = 318, SUB_W = 768, SUB_H = 58;
// Media bar
constexpr int BAR_X = 16, BAR_Y = 376, BAR_W = 768, BAR_H = 88;
constexpr int BAR_CY = BAR_Y + BAR_H / 2;
constexpr int TEXT_X = 106, TEXT_W = 250;
constexpr int PREV_X = 396, PLAY_X = 462, NEXT_X = 528;
constexpr int VOL_X0 = 598, VOL_X1 = 738;

LGFX* lcd = nullptr;
LGFX_Sprite subSprite;

String clockText = "Gorgi";
String statusText = "";
uint16_t statusColor = COLOR_TEXT_DIM;
bool statusPulse = false;
String subtitle = "";
MediaInfo media;
Target pressed = Target::None;
uint32_t pressedUntil = 0;

bool dirtyClock = true, dirtyStatus = true, dirtySub = true;
bool dirtyText = true, dirtyButtons = true, dirtyVolume = true;
bool pulseOn = true;
uint32_t nextPulseMs = 0;

// ---------- icons ----------

void iconPrev(lgfx::LovyanGFX& g, int cx, int cy, uint16_t c) {
  g.fillRect(cx - 9, cy - 8, 3, 16, c);
  g.fillTriangle(cx + 8, cy - 8, cx + 8, cy + 8, cx - 5, cy, c);
}

void iconNext(lgfx::LovyanGFX& g, int cx, int cy, uint16_t c) {
  g.fillRect(cx + 7, cy - 8, 3, 16, c);
  g.fillTriangle(cx - 8, cy - 8, cx - 8, cy + 8, cx + 5, cy, c);
}

void iconPlay(lgfx::LovyanGFX& g, int cx, int cy, uint16_t c) {
  g.fillTriangle(cx - 7, cy - 11, cx - 7, cy + 11, cx + 11, cy, c);
}

void iconPause(lgfx::LovyanGFX& g, int cx, int cy, uint16_t c) {
  g.fillRoundRect(cx - 9, cy - 10, 6, 20, 1, c);
  g.fillRoundRect(cx + 3, cy - 10, 6, 20, 1, c);
}

void iconSpeaker(lgfx::LovyanGFX& g, int cx, int cy, uint16_t c, bool loud) {
  g.fillRect(cx - 9, cy - 4, 5, 8, c);
  g.fillTriangle(cx - 6, cy, cx + 1, cy - 8, cx + 1, cy + 8, c);
  if (loud) {
    g.fillArc(cx + 2, cy, 7, 5, 300, 420, c);
    g.fillArc(cx + 2, cy, 12, 10, 300, 420, c);
  }
}

void iconMic(lgfx::LovyanGFX& g, int cx, int cy, uint16_t c) {
  g.fillRoundRect(cx - 4, cy - 10, 8, 13, 4, c);
  g.fillArc(cx, cy - 2, 8, 6, 0, 180, c);
  g.fillRect(cx - 1, cy + 6, 2, 4, c);
}

void iconNote(lgfx::LovyanGFX& g, int cx, int cy, uint16_t c) {
  g.fillCircle(cx - 8, cy + 9, 5, c);
  g.fillCircle(cx + 8, cy + 6, 5, c);
  g.fillRect(cx - 4, cy - 10, 3, 19, c);
  g.fillRect(cx + 10, cy - 13, 3, 19, c);
  g.fillTriangle(cx - 4, cy - 10, cx + 13, cy - 13, cx + 13, cy - 8, c);
  g.fillTriangle(cx - 4, cy - 10, cx + 13, cy - 8, cx - 4, cy - 5, c);
}

// Cut text to fit a width, adding "..." when needed
String fit(lgfx::LovyanGFX& g, const String& s, int maxW) {
  if (g.textWidth(s.c_str()) <= maxW) return s;
  String t = s;
  while (t.length() > 0 && g.textWidth((t + "...").c_str()) > maxW) t.remove(t.length() - 1);
  return t + "...";
}

// ---------- parts ----------

void drawClock() {
  lcd->fillRect(0, 0, 300, 44, BG);
  lcd->setFont(&fonts::FreeSansBold12pt7b);
  lcd->setTextDatum(textdatum_t::middle_left);
  lcd->setTextColor(COLOR_TEXT_DIM);
  lcd->drawString(clockText.c_str(), 20, 24);
}

void drawStatus() {
  lcd->fillRect(420, 0, 380, 44, BG);
  if (statusText.length() == 0) return;
  lcd->setFont(&fonts::FreeSansBold9pt7b);
  int tw = lcd->textWidth(statusText.c_str());
  int w = tw + 52;
  int x = 780 - w;
  lcd->fillRoundRect(x, 8, w, 32, 16, PANEL);
  lcd->drawRoundRect(x, 8, w, 32, 16, BORDER);
  iconMic(*lcd, x + 20, 24, (statusPulse && !pulseOn) ? BORDER : statusColor);
  lcd->setTextDatum(textdatum_t::middle_left);
  lcd->setTextColor(statusColor);
  lcd->drawString(statusText.c_str(), x + 36, 24);
}

void drawSubtitle() {
  subSprite.fillScreen(BG);
  subSprite.setFont(&fonts::FreeSansBold12pt7b);
  subSprite.setTextColor(TEXT);
  subSprite.setTextDatum(textdatum_t::middle_center);

  // Word wrap into at most two lines
  const int maxW = 680;
  String lines[2];
  int line = 0;
  String word;
  String text = subtitle + " ";
  bool truncated = false;
  for (size_t i = 0; i < text.length(); i++) {
    char ch = text[i];
    if (ch != ' ' && ch != '\n') {
      word += ch;
      continue;
    }
    if (word.length() == 0) continue;
    String candidate = lines[line].length() ? lines[line] + " " + word : word;
    if (subSprite.textWidth(candidate.c_str()) <= maxW) {
      lines[line] = candidate;
    } else if (line == 0) {
      line = 1;
      lines[1] = word;
    } else {
      truncated = true;
      break;
    }
    word = "";
  }
  if (truncated) lines[1] = fit(subSprite, lines[1] + " ...", maxW);

  int lineH = 27;
  if (lines[1].length() == 0) {
    subSprite.drawString(lines[0].c_str(), SUB_W / 2, SUB_H / 2);
  } else {
    subSprite.drawString(lines[0].c_str(), SUB_W / 2, SUB_H / 2 - lineH / 2);
    subSprite.drawString(lines[1].c_str(), SUB_W / 2, SUB_H / 2 + lineH / 2);
  }
  subSprite.pushSprite(lcd, SUB_X, SUB_Y);
}

void drawBarFrame() {
  lcd->fillRect(0, BAR_Y - 4, 800, 480 - BAR_Y + 4, BG);
  lcd->fillRoundRect(BAR_X, BAR_Y, BAR_W, BAR_H, 20, PANEL);
  lcd->drawRoundRect(BAR_X, BAR_Y, BAR_W, BAR_H, 20, BORDER);
  lcd->fillRoundRect(30, BAR_CY - 30, 60, 60, 12, TILE);
  iconNote(*lcd, 60, BAR_CY, COLOR_ACCENT);
}

void drawText() {
  lcd->fillRect(TEXT_X, BAR_Y + 6, TEXT_W, BAR_H - 12, PANEL);
  lcd->setTextDatum(textdatum_t::middle_left);
  lcd->setFont(&fonts::FreeSansBold12pt7b);
  lcd->setTextColor(TEXT_TITLE);
  lcd->drawString(fit(*lcd, media.title, TEXT_W - 4).c_str(), TEXT_X, BAR_CY - 12);
  lcd->setFont(&fonts::FreeSans9pt7b);
  lcd->setTextColor(TEXT_MUTED);
  lcd->drawString(fit(*lcd, media.artist, TEXT_W - 4).c_str(), TEXT_X, BAR_CY + 16);
}

void drawButtons() {
  lcd->fillCircle(PREV_X, BAR_CY, 26, pressed == Target::Prev ? BTN_PRESSED : BTN);
  iconPrev(*lcd, PREV_X, BAR_CY, TEXT);
  lcd->fillCircle(PLAY_X, BAR_CY, 30, pressed == Target::PlayPause ? ACCENT_PRESSED : COLOR_ACCENT);
  if (media.playing)
    iconPause(*lcd, PLAY_X, BAR_CY, PLAY_ICON);
  else
    iconPlay(*lcd, PLAY_X + 2, BAR_CY, PLAY_ICON);
  lcd->fillCircle(NEXT_X, BAR_CY, 26, pressed == Target::Next ? BTN_PRESSED : BTN);
  iconNext(*lcd, NEXT_X, BAR_CY, TEXT);
}

void drawVolume() {
  lcd->fillRect(564, BAR_Y + 10, 210, BAR_H - 20, PANEL);
  iconSpeaker(*lcd, 578, BAR_CY, TEXT_MUTED, false);
  int v = constrain(media.volume, 0, 100);
  int kx = VOL_X0 + (VOL_X1 - VOL_X0) * v / 100;
  lcd->fillRoundRect(VOL_X0, BAR_CY - 3, VOL_X1 - VOL_X0, 6, 3, TRACK);
  lcd->fillRoundRect(VOL_X0, BAR_CY - 3, kx - VOL_X0 + 1, 6, 3, COLOR_ACCENT);
  lcd->fillCircle(kx, BAR_CY, 10, TEXT);
  iconSpeaker(*lcd, 760, BAR_CY, TEXT_MUTED, true);
}

}  // namespace

void begin(LGFX& display) {
  lcd = &display;
  lcd->fillScreen(BG);
  subSprite.setColorDepth(16);
  subSprite.setPsram(true);
  subSprite.createSprite(SUB_W, SUB_H);
  drawBarFrame();
  dirtyClock = dirtyStatus = dirtySub = dirtyText = dirtyButtons = dirtyVolume = true;
}

void setClock(const String& text) {
  if (text == clockText) return;
  clockText = text;
  dirtyClock = true;
}

void setStatus(const String& text, uint16_t color, bool pulse) {
  if (text == statusText && color == statusColor && pulse == statusPulse) return;
  statusText = text;
  statusColor = color;
  statusPulse = pulse;
  pulseOn = true;
  dirtyStatus = true;
}

void setSubtitle(const String& text) {
  String t = toAscii(text);
  if (t == subtitle) return;
  subtitle = t;
  dirtySub = true;
}

void setMedia(const MediaInfo& info) {
  String title = toAscii(info.title), artist = toAscii(info.artist);
  if (title != media.title || artist != media.artist) dirtyText = true;
  if (info.playing != media.playing) dirtyButtons = true;
  if (info.volume != media.volume) dirtyVolume = true;
  media = info;
  media.title = title;
  media.artist = artist;
}

void setPlaying(bool playing) {
  if (playing == media.playing) return;
  media.playing = playing;
  dirtyButtons = true;
}

void setVolume(int volume) {
  volume = constrain(volume, 0, 100);
  if (volume == media.volume) return;
  media.volume = volume;
  dirtyVolume = true;
}

void showPressed(Target t) {
  if (t != Target::Prev && t != Target::PlayPause && t != Target::Next) return;
  pressed = t;
  pressedUntil = millis() + 180;
  dirtyButtons = true;
}

void update(uint32_t now) {
  if (pressed != Target::None && now >= pressedUntil) {
    pressed = Target::None;
    dirtyButtons = true;
  }
  if (statusPulse && now >= nextPulseMs) {
    pulseOn = !pulseOn;
    nextPulseMs = now + 450;
    dirtyStatus = true;
  }
  if (dirtyClock) { drawClock(); dirtyClock = false; }
  if (dirtyStatus) { drawStatus(); dirtyStatus = false; }
  if (dirtySub) { drawSubtitle(); dirtySub = false; }
  if (dirtyText) { drawText(); dirtyText = false; }
  if (dirtyButtons) { drawButtons(); dirtyButtons = false; }
  if (dirtyVolume) { drawVolume(); dirtyVolume = false; }
}

Target hitTest(int x, int y) {
  if (y >= BAR_Y - 8) {
    auto near = [&](int cx, int r) { return abs(x - cx) <= r && abs(y - BAR_CY) <= r; };
    if (near(PREV_X, 32)) return Target::Prev;
    if (near(PLAY_X, 34)) return Target::PlayPause;
    if (near(NEXT_X, 32)) return Target::Next;
    if (x >= VOL_X0 - 20 && x <= VOL_X1 + 20) return Target::Volume;
    return Target::None;
  }
  if (y >= FACE_Y && y < SUB_Y + SUB_H) return Target::Face;
  return Target::None;
}

int volumeFromX(int x) {
  return constrain((x - VOL_X0) * 100 / (VOL_X1 - VOL_X0), 0, 100);
}

String toAscii(const String& in) {
  String out;
  out.reserve(in.length());
  const uint8_t* s = (const uint8_t*)in.c_str();
  size_t n = in.length();
  for (size_t i = 0; i < n;) {
    uint8_t c = s[i];
    uint32_t cp;
    int len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
    else { i++; continue; }
    if (i + len > n) break;
    for (int k = 1; k < len; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    i += len;

    if (cp == '\r' || cp == '\t') out += ' ';
    else if (cp < 0x80) out += (char)cp;
    else if (cp == 0x2018 || cp == 0x2019) out += '\'';
    else if (cp == 0x201C || cp == 0x201D) out += '"';
    else if (cp == 0x2013 || cp == 0x2014) out += '-';
    else if (cp == 0x2026) out += "...";
    else if (cp == 0xA0) out += ' ';
    // anything else (emoji, accents) is dropped
  }
  return out;
}

}  // namespace ui
