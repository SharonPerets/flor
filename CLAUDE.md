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
- **Updates go over Wi-Fi only** (the user asked, after 3.8.90): ship every change as a normal release
  (3.9, 3.9.1, ...), not a USB test build.
- The user tests on the real device and reports back. You can't see the device: ask for photos or
  the Serial log when something's off.
- Personality: Gorgi is a cheerful buddy, English. The user does **not** want greetings or break reminders.

## Building and releasing (no local toolchain needed)

- The sandbox can't reach the PlatformIO registry, so firmware is **compiled by GitHub Actions**:
  push to the branch and check the "Gorgi firmware" workflow (GitHub MCP tools).
- Visual work: `esp32/ai-companion/tools/pc-tests/run.sh` (plain g++, no LovyanGFX) draws the Time screen
  into pictures and tests the timer/reminder engine with a fake clock (Arduino stand-ins in `stub/`).
  The same trick works for any pure-C++ screen (`weather.cpp`, `music_screen.cpp`): they only need
  `gfx.cpp` + `gfont_data.cpp`. Helper: `python esp32/gorgi-helper/test_timekeeper.py`.
- The board compiles as **gnu++11**: no default member initializers in structs built with `{a, b}`
  (give them a constructor). Check with `g++ -std=gnu++11 -fsyntax-only`.
- GitHub's full build logs can't be downloaded from the sandbox; the firmware workflow prints compile
  errors as annotations: `gh api repos/SharonPerets/flor/check-runs/<job id>/annotations`.
- Pushing a change to `esp32/VERSION` on any branch publishes a release. Push code first with the old
  version to let the builds check it, then bump the version.
- **Release** = change `esp32/VERSION` and set the same version in `GORGI_VERSION`
  (`esp32/ai-companion/src/pclink.h`) and `VERSION` (`esp32/gorgi-helper/gorgi_helper.py`), then push.
  `gorgi-release.yml` builds firmware + helper installer and publishes GitHub release `gorgi-v<ver>`.
  Gorgi Helper finds it and offers both updates (tray menu); **update the helper first, then Gorgi**.
  Updates go over Wi-Fi; no cable.
- **Test builds** (`gorgi-test.yml`, triggered by `esp32/TEST_VERSION`) publish a GitHub *pre-release*
  that the helper never offers; flashed by USB. Use a version between releases (e.g. 3.8.90) so the next
  real release is still "newer". Only with the user's OK (they prefer Wi-Fi releases now).
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

## Where things stand (version 3.9 released: the Time screen; waiting for the user to test it)

3.9 = the Time screen (step 1 below): `wallclock` (internet time, Israel TZ, PC time as backup),
`alarms` (timers/focus/reminders saved in NVS namespace "alarms", due list, missed after restart),
`time_screen` (drawing + taps), helper `timekeeper.py` (AI tags -> `time_add`/`time_cancel`, list in the
AI context, `due` -> pop-up + spoken on the PC). Screens: **Music ← Face → Weather → Time**.


Done: face with 12 moods, talk by tapping the face (follow-up listening ~5 s), media control + volume,
long-term memory (`memory.json` on the PC), Wi-Fi updates with progress window on the PC, screens by
swiping sideways, instant switch: **Music ← Face → Weather**. Music screen shows album art (helper sends
240x240 RGB565) and song progress. Weather from Open-Meteo for 16 Israeli cities (Nahariya default),
also given to the AI. Screens return to the face after 45 s.

## Next steps (chosen by the user, in this order; plan each one with the user before coding)

Planned screen order when all exist: **Music ← Face → Weather → Time → Sketch** (dots in the top bar).

1. **Time screen** - ONE screen for both (the user chose this over two screens):
   - **Timers** ("set a timer for 10 minutes", or by touch) with big countdown rings.
   - **Focus sessions** (e.g. 25 minutes), Gorgi cheers when one ends.
   - **Reminders**: "remind me in 20 minutes to take the laundry out", "every day at 8 remind me...".
     A list of what's coming up; add/delete by touch too.
   - Set by voice through the AI (tags like the memory tags, e.g. `[timer: ...]`, `[remind: ...]`).
   - Gorgi has no speaker: when something is due, Gorgi wakes up, the face reacts and the screen shows it;
     the PC says it out loud if the helper is connected. Decide with the user where reminders are stored
     (on Gorgi so they fire without the PC, or on the PC).
2. **Sketch pad** screen: draw with a finger, a few colours, eraser, save ~10 sketches.
   Optional bonus: "Gorgi, what did I draw?" - the AI guesses (needs a model that can see pictures; check first).
3. **Night clock**: at night Gorgi shows a big dim clock instead of just sleeping.
4. **"Hey Gorgi" wake word**: talk without tapping. Local on the PC (voice detection + Whisper checking
   for the name and sound-alikes), with an on/off setting.

## Ideas list (discussed, not chosen yet - offer them again when asked "what next?")

- **Petting reactions**: rub the face = happy, poke = grumpy.
- **PC status screen**: CPU, memory, temperature, network speed as live gauges (from the helper).
- **Photo frame**: photos from a PC folder when Gorgi is idle/asleep.
- **Calendar**: today's events from the PC calendar; Gorgi mentions what's next.
- **Games**: e.g. tic-tac-toe against Gorgi, with happy/sad faces when it wins/loses.
- **Claude Code usage screen** (usage of a couple of Claude Code accounts): **shelved** - the user said
  "forget about this for now". Notes if it comes back: token/cost totals are reliable from local
  Claude Code logs on that PC; plan-limit percentages (like /usage) would need an unofficial API.
- **Smooth sliding between screens**: tried twice (3.7 slide, 3.7.9x new driver) and dropped; see lessons.

Not wanted (don't suggest again): greetings, break reminders.

**Always feel free to suggest new ideas** when the user asks what to build next: give a short list with a
recommendation, then let the user pick, then plan it.
