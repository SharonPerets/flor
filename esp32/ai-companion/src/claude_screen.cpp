#include "claude_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfont.h"

namespace claudescreen {
namespace {

using gfx::Canvas;
using gfx::Color;

const Color PANEL = gfx::rgb(0x10151C);
const Color BORDER = gfx::rgb(0x1F2935);
const Color TRACK = gfx::rgb(0x222C38);
const Color TEXT = gfx::rgb(0xE8EEF4);
const Color MUTED = gfx::rgb(0x9AA7B5);
const Color ACCENT = gfx::rgb(0x4FE3D6);
const Color CLAUDE = gfx::rgb(0xD97757);
const Color WAITING_C = gfx::rgb(0xFF9F43);
const Color DONE_C = gfx::rgb(0x5DD879);
const Color IDLE_C = gfx::rgb(0x55616E);
const Color WARN = gfx::rgb(0xFFD166);
const Color DANGER = gfx::rgb(0xFF6B6B);

constexpr float L_X = 16, L_W = 508, P_Y = 56, P_H = 308;
constexpr float R_X = 540, R_W = 244;
constexpr float ROW_Y = 104, ROW_H = 51;

void panel(Canvas& c, float x, float w) {
  c.fillRRect(x, P_Y, w, P_H, 20, BORDER);
  c.fillRRect(x + 1, P_Y + 1, w - 2, P_H - 2, 19, PANEL);
}

void fit(const gfont::Font& f, const char* s, float maxW, char* out, int size) {
  snprintf(out, size, "%s", s);
  if (Canvas::textWidth(f, out) <= maxW) return;
  int n = (int)strlen(out);
  while (n > 0) {
    out[--n] = 0;
    char tmp[96];
    snprintf(tmp, sizeof tmp, "%s...", out);
    if (Canvas::textWidth(f, tmp) <= maxW) {
      snprintf(out, size, "%s", tmp);
      return;
    }
  }
}

const Color& stateColor(uint8_t s) {
  switch (s) {
    case WORKING: return ACCENT;
    case WAITING: return WAITING_C;
    case DONE: return DONE_C;
    default: return IDLE_C;
  }
}

void sparkIcon(Canvas& c, float cx, float cy, float r, const Color& col) {
  for (int i = 0; i < 6; ++i) {
    const float a = i * 0.5236f + 0.26f;
    c.fillCapsule(cx - cosf(a) * r, cy - sinf(a) * r, cx + cosf(a) * r, cy + sinf(a) * r, r * 0.17f, col);
  }
  c.fillCircle(cx, cy, r * 0.3f, col);
}

void sessionRow(Canvas& c, const Session& s, float y) {
  const float x = L_X + 20;
  const Color& col = stateColor(s.state);
  c.fillCircle(x + 7, y + 22, 7, col);
  if (s.state == WORKING) c.strokeArc(x + 7, y + 22, 11, 2, 0, 360, col, 0.35f);

  char buf[96], ago[24] = "";
  fit(gfont::TITLE, s.name, 200, buf, sizeof buf);
  c.text(gfont::TITLE, buf, x + 26, (int)y + 30, TEXT);

  if (s.ago >= 0) formatAgo(s.ago, ago, sizeof ago);
  const char* what = s.state == WORKING ? "Working" : s.state == WAITING ? "Waiting for you" : s.state == DONE ? "Done" : "Idle";
  if (s.state == DONE && s.at[0]) snprintf(buf, sizeof buf, "%s at %s", what, s.at);
  else if (s.state == WORKING && ago[0]) snprintf(buf, sizeof buf, "%s - %s", what, ago);
  else snprintf(buf, sizeof buf, "%s", what);
  if (s.text[0] && s.state == WAITING) snprintf(buf, sizeof buf, "%s", s.text);  // e.g. "Needs your OK"
  char line[96];
  fit(gfont::SMALL, buf, L_X + L_W - (x + 240) - 16, line, sizeof line);
  c.text(gfont::SMALL, line, x + 240, (int)y + 29, col);
}

void gauge(Canvas& c, const char* label, float pct, const char* reset, float y) {
  const float x = R_X + 18, w = R_W - 36;
  c.text(gfont::SMALL, label, x, (int)y, MUTED);
  char buf[16];
  snprintf(buf, sizeof buf, "%d%%", (int)(pct + 0.5f));
  const Color& col = pct >= 90 ? DANGER : pct >= 70 ? WARN : ACCENT;
  c.text(gfont::HEADLINE, buf, x + w - Canvas::textWidth(gfont::HEADLINE, buf), (int)y + 6, TEXT);
  c.fillRRect(x, y + 16, w, 12, 6, TRACK);
  const float p = pct < 0 ? 0 : pct > 100 ? 100 : pct;
  if (p > 0.5f) c.fillRRect(x, y + 16, w * p / 100.0f < 12 ? 12 : w * p / 100.0f, 12, 6, col);
  if (reset[0]) {
    char r[40];
    snprintf(r, sizeof r, "resets %s", reset);
    c.text(gfont::SMALL, r, x, (int)y + 50, MUTED);
  }
}

}  // namespace

void formatTokens(uint32_t n, char* out, int size) {
  if (n >= 10000000) snprintf(out, size, "%uM", (unsigned)(n / 1000000));
  else if (n >= 1000000) snprintf(out, size, "%u.%uM", (unsigned)(n / 1000000), (unsigned)(n / 100000 % 10));
  else if (n >= 1000) snprintf(out, size, "%uk", (unsigned)(n / 1000));
  else snprintf(out, size, "%u", (unsigned)n);
}

void formatAgo(int32_t s, char* out, int size) {
  if (s < 45) snprintf(out, size, "just now");
  else if (s < 3600) snprintf(out, size, "%d min", (int)((s + 30) / 60));
  else if (s < 86400) snprintf(out, size, "%d h", (int)(s / 3600));
  else snprintf(out, size, "%d days", (int)(s / 86400));
}

void draw(Canvas& c, const View& v) {
  // sessions
  panel(c, L_X, L_W);
  sparkIcon(c, L_X + 30, P_Y + 28, 11, CLAUDE);
  c.text(gfont::TITLE, "Claude Code", L_X + 50, (int)P_Y + 36, TEXT);
  const float hx = L_X + 26;
  if (!v.pc) {
    c.text(gfont::SUBTITLE, "PC not connected", hx, (int)ROW_Y + 40, TEXT);
    c.text(gfont::SMALL, "Run Gorgi Helper on your PC.", hx, (int)ROW_Y + 72, MUTED);
  } else if (v.count == 0) {
    c.text(gfont::SUBTITLE, "No sessions right now", hx, (int)ROW_Y + 40, TEXT);
    if (v.hooked) {
      c.text(gfont::SMALL, "Start Claude Code on your PC and it shows up here.", hx, (int)ROW_Y + 72, MUTED);
    } else {
      c.text(gfont::SMALL, "To connect: right-click Gorgi Helper's icon -> Settings...", hx, (int)ROW_Y + 72, MUTED);
      c.text(gfont::SMALL, "-> Connect Claude Code.", hx, (int)ROW_Y + 96, MUTED);
    }
  }
  for (int i = 0; i < v.count && i < MAX_SESSIONS; ++i) {
    const float y = ROW_Y + i * ROW_H;
    if (i > 0) c.fillRect(L_X + 20, y, L_W - 40, 1, BORDER);
    sessionRow(c, v.sessions[i], y);
  }

  // usage
  panel(c, R_X, R_W);
  c.text(gfont::TITLE, "Usage", R_X + 18, (int)P_Y + 36, TEXT);
  if (v.usage.plan[0]) {
    char plan[24];
    snprintf(plan, sizeof plan, "- %s", v.usage.plan);
    c.text(gfont::SMALL, plan, R_X + 26 + Canvas::textWidth(gfont::TITLE, "Usage"), (int)P_Y + 35, MUTED);
  }
  if (!v.usage.known) {
    const char* lines[] = {"Plan usage", "not available", "right now."};
    for (int i = 0; i < 3; ++i) c.text(gfont::SMALL, lines[i], R_X + 18, (int)ROW_Y + 26 + i * 24, MUTED);
    return;
  }
  if (v.usage.tokens) {  // amounts only (the plan % couldn't be read)
    const float x = R_X + 18;
    c.text(gfont::SMALL, "Last 5 hours", x, (int)ROW_Y + 14, MUTED);
    c.text(gfont::HEADLINE, v.usage.fiveTokens, x, (int)ROW_Y + 52, TEXT);
    c.text(gfont::SMALL, "tokens", x + Canvas::textWidth(gfont::HEADLINE, v.usage.fiveTokens) + 8, (int)ROW_Y + 52, MUTED);
    c.text(gfont::SMALL, "Last 7 days", x, (int)ROW_Y + 100, MUTED);
    c.text(gfont::HEADLINE, v.usage.weekTokens, x, (int)ROW_Y + 138, TEXT);
    c.text(gfont::SMALL, "tokens", x + Canvas::textWidth(gfont::HEADLINE, v.usage.weekTokens) + 8, (int)ROW_Y + 138, MUTED);
    c.text(gfont::SMALL, "Plan % not available", x, (int)ROW_Y + 176, MUTED, 0.7f);
  } else {
    gauge(c, "5-hour", v.usage.five, v.usage.fiveReset, ROW_Y + 14);
    gauge(c, "Weekly", v.usage.week, v.usage.weekReset, ROW_Y + 112);
  }
  if (v.usage.age >= 0) {
    char ago[24], buf[40];
    formatAgo(v.usage.age, ago, sizeof ago);
    snprintf(buf, sizeof buf, v.usage.age < 45 ? "updated %s" : "updated %s ago", ago);
    c.text(gfont::SMALL, buf, R_X + 18, (int)(P_Y + P_H - 16), MUTED, 0.7f);
  }
}

}  // namespace claudescreen
