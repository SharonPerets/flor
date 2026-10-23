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
- The backlight is only on/off (CH422G pin), it can't be dimmed in hardware: "dim" means darker drawing.
- 4.0 helper update: "Update Gorgi Helper" seemed to do nothing at first (no progress window visible);
  it sorted itself out. If it comes back, ask for `gorgi-helper.log` (tray -> Open settings folder).

## Where things stand (4.1 in progress: tabs, Claude screen, Teams call mode; 4.0 confirmed, merged to main)

Done and confirmed:
- 3.8 and before: face with 12 moods, talk by tapping the face (follow-up listening ~5 s), media control +
  volume, long-term memory (`memory.json` on the PC), Wi-Fi updates with progress window on the PC, screens
  by swiping sideways (instant switch). Music screen: album art (helper sends 240x240 RGB565) and song
  progress. Weather from Open-Meteo for 16 Israeli cities (Nahariya default), also given to the AI.
  Screens returned to the face after 45 s (removed in 4.0 at the user's request).
- **3.9 - Time screen** (step 1 of the plan, done): one screen for timers, focus sessions and reminders.
  - Firmware: `wallclock` (internet time, Israel TZ, PC time as backup), `alarms` (up to 3 timers + 1 focus
    + 20 reminders, saved in NVS namespace "alarms", survive restarts, "While I was off, I missed: ..."),
    `time_screen` (rings, 1/5/10 min buttons, focus length 15/25/45/60, "Next up" list, tap a reminder to
    delete it, **Ask me** button that jumps to the face and starts listening). Countdown chip next to the
    clock on every screen. When due: Gorgi wakes, shows it, nudges every 30 s, tap anywhere to stop
    (gives up after 10 min). No "Now playing" reactions during focus.
  - Helper: `timekeeper.py` turns AI tags (`[timer: 10m | pasta]`, `[focus: 25m]`, `[remind: at 17:00 | ...]`,
    `[remind: daily 08:00 | ...]`, `[cancel: ...]`) into `time_add` / `time_cancel`; Gorgi's `time_list`
    goes into the AI context; `due` -> Windows pop-up + spoken on the PC (music paused for a moment).
    If a tag can't be done (limit reached, old firmware) the problem is spoken instead of a fake "done".
  - 3.8.90 was a USB test build; after it the user asked for **Wi-Fi releases only**.
- **4.0 - Sketch pad** (step 2): `sketch` (toolbar 8 colours/eraser/3 sizes/undo/gallery, paper 680x320 at
  (112,50), packed RLE files, thumbnails) + `sketch_store` (LittleFS on the "spiffs" partition, /sketch/<id>.gsk,
  newest 10 kept, NVS "sketch": cur/next ids). Drawing goes into a PSRAM picture (+5 undo copies) and only the
  changed rectangle is redrawn. Saves after 20 s pause / leaving / gallery. User choices: swipe only on the
  toolbar on this screen; **no automatic return to the face on any screen** (removed in 4.0);
  no "what did I draw?" AI guess.
  Confirmed working on the device ("works great").
- **4.1 - work features** (user is a software dev: Claude Code all day, Teams meetings, lots of music):
  - **Icon tabs** in the top bar replace the dots (`tabs.cpp`; tap to jump, swipe still works; orange badge dot).
  - **Claude screen** (`claude_screen.cpp`): sessions (working/waiting/done) + usage (5-hour, weekly, resets).
    Helper `claude_setup.py` ("Connect Claude Code" in Settings) adds hooks (SessionStart, UserPromptSubmit,
    PostToolUse, Notification, Stop, SessionEnd) + a status line to `~/.claude/settings.json` (merge, backup);
    they run `curl.exe ... http://127.0.0.1:47811/claude/hook|status`. `claude_watch.py` = that tiny server,
    session states, alerts ("needs you" always, "finished" only after >= 20 s of work), usage from the status
    line's `rate_limits` (semi-official; the unofficial OAuth usage endpoint was NOT used). The user has one
    account and runs Claude Code on Windows directly.
  - **On a call** (`call_watch.py`): Teams = an app with "teams" in its name using the mic (registry
    CapabilityAccessManager, LastUsedTimeStop == 0). Red "On a call" status, music paused/resumed, no spoken
    alerts or pop-ups, red mute button next to the face -> `cmd teams_mute` -> Ctrl+Shift+M sent to the Teams
    window. Mute is a "try" (no mute state shown). Better later: Teams' third-party device API
    (ws://127.0.0.1:8124, needs enabling in Teams) gives real mute state and in-meeting status.
- Screens now: **Music ← Face → Weather → Time → Sketch → Claude** (icon tabs at the top).

Lessons from 3.9 (keep for future screens):
- Every screen that needs voice should have its own talk button (the user didn't know where to tap).
- Test pure-C++ drawing and logic on the PC first (`tools/pc-tests`), then let GitHub compile.

## Next steps (chosen by the user, in this order; plan each one with the user before coding)

Planned screen order when all exist: **Music ← Face → Weather → Time → Sketch** (dots in the top bar).

1. ~~**Time screen**~~ - done in 3.9 (see above).
2. ~~**Sketch pad**~~ - done in 4.0 (the AI-guess bonus was declined).
3. **Night clock**: at night Gorgi shows a big dim clock instead of just sleeping.
4. **"Hey Gorgi" wake word**: talk without tapping. Local on the PC (voice detection + Whisper checking
   for the name and sound-alikes), with an on/off setting.

## Ideas list (discussed, not chosen yet - offer them again when asked "what next?")

- **Petting reactions**: rub the face = happy, poke = grumpy.
- **PC status screen**: CPU, memory, temperature, network speed as live gauges (from the helper).
- **Photo frame**: photos from a PC folder when Gorgi is idle/asleep.
- **Games**: e.g. tic-tac-toe against Gorgi, with happy/sad faces when it wins/loses.
- **Next meeting countdown** (Outlook calendar, "Join" button) - suggested with 4.1, a good next step.
- **Gorgi dances** to the music beat; **focus music** (playlist starts with a focus session);
  **build/PR watcher** (GitHub CI results) - suggested with 4.1.
- **Pull-down panel** (swipe down): quiet mode, screen off, lock/sleep PC, big volume, status + "Update now".
- **Smooth sliding between screens**: tried twice (3.7 slide, 3.7.9x new driver) and dropped; see lessons.

Not wanted (don't suggest again): greetings, break reminders.

**Always feel free to suggest new ideas** when the user asks what to build next: give a short list with a
recommendation, then let the user pick, then plan it.
