# Notes for Claude

This repo holds a few unrelated things. The active project is **Gorgi**, in `esp32/`
(the HTML/JS files at the root are a separate room-planner site; leave them alone).

## Gorgi in one paragraph

An AI desk companion on a **Waveshare ESP32-S3-Touch-LCD-7** (800x480 RGB LCD, GT911 touch,
CH422G IO expander, 8 MB flash, 8 MB octal PSRAM). Gorgi shows an animated face and screens;
a Windows app, **Gorgi Helper**, does the listening (PC mic + Whisper), thinking (Ollama Cloud,
default model `gpt-oss:20b`), talking (Edge TTS `en-US-AnaNeural`, PC speakers), music control
and updates. They talk over a WebSocket (Gorgi is the server, port 81, found via mDNS `gorgi.local`).

- `esp32/ai-companion/` - firmware (PlatformIO, Arduino-ESP32 **2.x**, LovyanGFX 1.1.16). README there.
- `esp32/gorgi-helper/` - Python helper (asyncio), built with PyInstaller + Inno Setup. README there.
- `esp32/VERSION` - the release version. `esp32/TEST_VERSION` - only for USB test builds.

## Working with the user

- **Plan first, get approval, then code.** The user asked for full plans before any code is written,
  and likes to choose between options (give a recommendation). Step by step, one release per step.
- The user is not a programmer: explain in plain language, short, outcome first. Mobile app.
- The user tests on the real device and reports back. You can't see the device: ask for photos or
  the Serial log when something's off.
- Personality: Gorgi is a cheerful buddy, English. The user does **not** want greetings or break reminders.

## Building and releasing (no local toolchain needed)

- The sandbox can't reach the PlatformIO registry, so firmware is **compiled by GitHub Actions**:
  push to the branch and check the "Gorgi firmware" workflow (GitHub MCP tools).
- Visual work: a PC preview of the drawing code (LovyanGFX on Linux) renders screenshots. It lived in the
  old session's scratchpad and is gone; rebuild if needed (LovyanGFX `examples_for_PC`, compile
  `face/ui/gfx/display/weather/music_screen/gfont_data.cpp` against a sprite-based `LGFX`).
- **Release** = change `esp32/VERSION` and set the same version in `GORGI_VERSION`
  (`esp32/ai-companion/src/pclink.h`) and `VERSION` (`esp32/gorgi-helper/gorgi_helper.py`), then push.
  `gorgi-release.yml` builds firmware + helper installer and publishes GitHub release `gorgi-v<ver>`.
  Gorgi Helper finds it and offers both updates (tray menu); **update the helper first, then Gorgi**.
  Updates go over Wi-Fi; no cable.
- **Test builds** (`gorgi-test.yml`, triggered by `esp32/TEST_VERSION`) publish a GitHub *pre-release*
  that the helper never offers; flashed by USB. Use a version between releases (e.g. 3.8.90) so the next
  real release is still "newer". Only with the user's OK.
- Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and the session link.
  Develop on branch `claude/esp32-touch-screen-software-emaips` (or what the session says).

## Hard-won lessons (don't repeat)

- **Don't change the base software (Arduino core / screen driver) without asking.** Version 3.7.91/92
  tried ESP-IDF 5 double buffering (bounce buffers starved Wi-Fi and flickered; direct DMA worked but
  looked worse to the user). Reverted in 3.8. Switching base software needs a USB flash.
- The LCD's picture memory is in PSRAM and read non-stop by DMA. Heavy writes make the picture drift
  or tear. So: draw in bands in internal RAM (`display.cpp`), redraw only what changed, keep 14 MHz pclk.
  **Full-screen animations (slides) tear** - the user chose instant screen switching instead.
- While new firmware is written to flash, the screen jumps: Gorgi turns the backlight off during updates.
- WebSocket messages to Gorgi are limited to ~15 KB (album art is sent in 8 KB pieces).
- Tkinter windows in the helper must run in their own process (`--settings`, `--progress`).
- Windows volume COM objects are created once on one thread (crashes otherwise).
- `millis()` comparisons must be wrap-safe (`msSince`).
- Names: `pclink` (not `link`, clashes with POSIX), `gfont` (not `fonts`, clashes with LovyanGFX).

## Where things stand (version 3.8)

Done: face with 12 moods, talk by tapping the face (follow-up listening ~5 s), media control + volume,
long-term memory (`memory.json` on the PC), Wi-Fi updates with progress window on the PC, screens by
swiping sideways, instant switch: **Music ← Face → Weather**. Music screen shows album art (helper sends
240x240 RGB565) and song progress. Weather from Open-Meteo for 16 Israeli cities (Nahariya default),
also given to the AI. Screens return to the face after 45 s.

## Next steps (agreed order; plan each one with the user before coding)

1. **Time screen** (one screen, after Weather): timers, 25-min focus sessions, and reminders, set by
   voice ("remind me in 20 minutes...", via AI tags like the memory tags) or by touch. Gorgi has no
   speaker: when due, the face reacts and the PC says it out loud if the helper is connected.
2. **Sketch pad** screen: draw with a finger, a few colours, eraser, save sketches.
3. **Night clock** (big dim clock at night) and **"Hey Gorgi" wake word** (local: VAD + Whisper name check,
   on/off setting).
- Shelved: Claude Code usage screen (user said "forget about this for now").
