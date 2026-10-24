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
- **When the user confirms a release works** ("ok", "works", "looks good"), always, without asking: mark it
  confirmed in this file (Where things stand + Handover), merge the branch to main (PR via `gh api`, merge), check
  that the release job on main only skipped, and confirm that everything is pushed. The user starts new sessions
  often, so this file must always say where things stand.
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
- **The helper must be able to quit**: every task in the main `gather` must end when `stop_event` is set. In 4.1-4.3
  `claude_watch.serve()` ran forever, so after starting its installer the old helper stayed alive ("Downloading..."
  in the tray), and the new one quit at once ("already running"). Fixed in 4.3.1: serve stops with the event,
  `request_stop()` force-exits after 8 s, `main()` ends with `os._exit(0)`, and the installer's `PrepareToInstall`
  runs `taskkill /IM GorgiHelper.exe /F` first (that also rescues updates started by the old, buggy helper).

## Where things stand (4.6.2 confirmed and merged; 4.7 Hebrew on the screen released on branch claude/project-thread-vbs4n9, waiting for the user's test)

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
- **4.2 - pull-down panel** (replaced by the Action Center in 4.3; `panel.cpp` now only draws the top handle): swipe down from the top bar (drag > 50 px down from y < 44), close by
  pushing up or tapping outside; drawn over the page (page drawing paused, page marked dirty on close).
  - **Sleep**: backlight off, phase Asleep; any touch wakes (consumed). Timer/reminder/Claude alerts turn the screen
    on and it goes back to sleep after the alert is dismissed (`sleepAfterAlert`).
  - **Update**: helper sends `update_info` (available, version, busy); panel open -> `cmd check_updates` (helper
    checks GitHub at most every 10 min); tap -> `cmd update` -> helper `update_all()`: helper first (sets
    `pending_gorgi_update` in config.json, installer restarts it), the new helper's `offer_updates()` then sends
    the firmware to Gorgi by itself. Orange dot on the top handle when an update is waiting.
  - Status line: version, Wi-Fi signal (RSSI), PC name.
  - The pull-down panel from the ideas list is partly done; still open: quiet mode, lock/sleep PC, big volume.
- **4.2.1 - usage fix**: the user runs Claude Code in the **Claude desktop app** (Code tab), which has **no status
  line**, so usage never arrived (hooks work fine there). Now `usage_fetch.py` reads the PC app's saved sign-in
  (`~/.claude/.credentials.json`, `claudeAiOauth.accessToken`, read-only, never refreshed) and asks
  `https://api.anthropic.com/api/oauth/usage` (unofficial, `anthropic-beta: oauth-2025-04-20`) every 3 min
  (`five_hour`/`seven_day` `utilization` + `resets_at`). Fallback: tokens from `~/.claude/projects/*/*.jsonl`
  (input+output+cache creation, last 5 h / 7 days) shown as amounts. Plan name ("Max") next to "Usage" so the
  user can see which account it is: only the PC app's account, never the browser one (user asked for that).
  Last result in `claude-usage.json` (helper settings folder, no token) for debugging. The user has a Max plan.
- **4.3 - top bar vs Action Center** (user's notes from a video):
  - Top bar = icons only (clock, countdown chip, tabs ♪ ☺ ⏱ ✎ ✳ with highlight + teal underline, talk pill, a thin
    line under it). Handle dot = there are notifications. `CLOCK_AREA` now stops at x 248 (it used to wipe the
    first tab icon when the countdown appeared - fixed).
  - **Action Center** (`action_center.cpp`) replaces the 4.2 panel: a full 800x480 screen (ui::update is paused
    while open, `ui::redrawAll()` on close): big time + date, small weather chip (tap -> forecast with Back and the
    city picker; the user rarely uses weather, so it's hidden until tapped), Sleep, full-width notifications.
    Notifications (main.cpp `notices[]`): UPDATE (from `update_info`, button "Update"), MISSED (an alert nobody
    tapped in 10 min), CLAUDE (a Claude alert while Gorgi was busy). x clears one.
  - **No Weather tab** any more (fetching continues: the chip, the AI).
  - **Music bar folded** into a slim line (`tabs::drawMiniPlayer`, y 424); tap -> full bar for 10 s (`openMusicBar`).
    Face moved down a little (FACE_Y 56, subtitle 330..388, bar 392..476).
  - Time screen redraws only the changed part (rings / buttons / list): a full redraw flickered (start/stop focus).
- **4.3.2 - Claude alert fixes** (helper only; confirmed: "finished a task and did not notify - looks good", i.e. no
  alerts between steps):
  - "Finished" fired after every step: Claude Code sends `Stop` at the end of every reply. Now a Stop waits
    `STOP_QUIET` 15 s (`ClaudeWatch.tick()`, called each second by `serve()`); any tool use / prompt cancels it.
    If the last message (`last_assistant_message`, else the transcript tail) ends with "?" in its last 3 lines
    -> "needs you" ("Asked you something") instead of "finished".
  - More "needs you": new hooks `PermissionRequest` and `PreToolUse` (matcher `AskUserQuestion|ExitPlanMode`).
    `claude_setup.upgrade()` adds missing hooks at helper start for users connected by an older helper.
    "Needs you" alerts only when a session starts waiting (no repeats for the same wait).
  - No pop-up / spoken alerts at all while Gorgi isn't connected (`announce` checks `self.ws`; user asked
    the same for timers/reminders).
- **4.3.3 - one update window** (helper only; confirmed "updates looks good"; user chose "A": no auto-open, pop-up + one tray click):
  - `update_window.py` (`--updates` process, replaces `progress_window.py`): JSON state lines on stdin
    (rows helper/Gorgi now -> new + state, text, percent or "wait", button start/retry/close), words on stdout
    ("ready", "start", "retry", "close"). The Windows self-test checks the "ready" round trip.
  - Tray: one item "Update to X..." / "Check for updates..." -> `Helper.open_updates()`. Gorgi's Update button ->
    `update_all()` -> `open_updates(start=True)`.
  - `run_updates()`: helper step (download, `config["update_flow"] = {"version"}`, installer, quit) -> the new
    helper's `update_loop()` sees `update_flow` (or the old `pending_gorgi_update`) -> `continue_after_restart()`
    reopens the window -> `_update_gorgi_step()` (wait for Gorgi 60 s, send, then wait up to 180 s for `hello`
    with the new version) -> "All done" + Close. Failures -> "Try again". Tests: `test_updates.py`.
- **4.3.4 - plan % from the desktop app's sign-in** (confirmed: "We have % !!!"; the full one-window update ran fine). Research report (deep research, 2026-10-24)
  found: the desktop app's Code tab uses its own token and never refreshes `~/.claude/.credentials.json` (stale);
  renewing a token ourselves (platform.claude.com/v1/oauth/token) rotates the refresh token and SIGNS CLAUDE CODE
  OUT (CodexBar, Claude-Usage-Tracker shipped that bug) - never do it. `usage_fetch.py` now (like
  CodeZeno/Claude-Code-Usage-Monitor): `%APPDATA%\Claude\config.json` (or Store: `%LOCALAPPDATA%\Packages\
  Claude_pzs8sxrjxfjjc\LocalCache\Roaming\Claude\`) key `oauth:tokenCacheV2` (then `oauth:tokenCache`) = base64
  "v10"+nonce12+ciphertext+tag16, AES-GCM key = `Local State` os_crypt.encrypted_key (base64, "DPAPI" prefix,
  CryptUnprotectData). Decrypted JSON: "<install>:<user>:<base url>:<scopes>" -> {token, expiresAt ms}; pick a fresh
  entry with `user:profile`. Then a fresh .credentials.json. No fresh token -> no request. Every 5 min, 429 ->
  Retry-After (5 min..1 h), 401/403 -> 5 min. Utilization 0..1 or 0..100 both accepted.
  **User doesn't want token counts at all** ("just the %, if we fail, drop it"): LogCounter / set_tokens removed;
  Gorgi shows "Plan usage not available right now" when there's no %. If it fails, ask for `claude-usage.json`.
- **4.4 - notes on the Sketch screen** (user: finger notes came out huge; plan doc
  https://claude.ai/artifact/PSdMeAMBBejgJGZKjvRspS, mock-up approved). `sketch::Writer` (sketch.cpp): **Write** mode =
  big writing box (148 px, at the bottom; moves to the top for lines 5-9), ink lands at 1/3 size on a lined page
  (10 lines, 30 px) at a writing spot that moves on after a 0.9 s pause or at once past the dashed slide line;
  space / new line buttons; tap the page = move the spot; eraser rubs the page directly; full page -> new page;
  after loading, `findCursor()` continues after the last writing. Box ink lives in a separate PSRAM picture
  (`skBox`). **Draw** mode = the old free drawing. Both use `sketch::Smoother` (calmed points + curves).
  Undo: a ring of 6 copies taken when a stroke / word ENDS (not at finger down); Write undo = last word.
  Toolbar: Write / Draw, 4 colours, eraser, size, undo, gallery, Clear (twice: "Sure?"), New page. Mode in NVS
  "sketch"/"mode". The idle "Tap me to talk" pill is gone everywhere (user: it looked like a button but wasn't);
  the Sketch screen has a real teal **Talk** button (`ui::setTalkButton`, Target::Talk -> face + listen).
  Status pill still shows Listening / Thinking / On a call.
- **4.5 - typed notes** (user tried 4.4 with a photo; asked for no music bar, a bigger page, handwriting -> real text,
  voice typing). `ui::setBottomHidden` on Sketch: paper 680x424 (13 lines, box 170 px, flips to the top from line 7);
  old 320-row files still load (`OLD_PAPER_H`), thumbs 160x100. Typed mode (helper connected): the Writer records the
  word's points (`sketch::Word`, box px, y from the guide line), main sends `{"type":"ink","id","strokes":[[x,y..]]}`
  (`pclink::sendText`), helper `ink_reader.py` = Windows `InkRecognizerContainer` (pywinrt
  `winrt-Windows.UI.Input.Inking`; the CI self-test reads a sample "hello" -> ['hello','hallo',...]) -> `ink_text`
  {id, guesses}. Main keeps an op queue (Word / Text / Space / NewLine / Place) so order holds; no answer in 2.5 s
  -> handwriting placed small. Text in the new `gfont::NOTE` (Nunito 21 px, 600). Other guesses = chips in the box
  (tap = undo + type it). Talk on Sketch = `dictate` -> helper `Conversation.dictate` (Whisper only) ->
  `dictate_state` (listening / thinking / idle + problem) and `dictation` {text}. English only (fonts are ASCII).
  The user tried 4.5: "the writing does not work at all, voice talk works great" -> **Sketch was dropped** (4.6).
- **4.6 - To-do screen** (replaces Sketch; plan doc https://claude.ai/artifact/XSw28AN7VC7sDiAYGkqLdL, user picked
  option B, mock-ups in /mnt/project-files/todo/). `todos.cpp` keeps up to 40 open + 20 done tasks in NVS ("todo"/"list",
  one blob; day numbers = days since 1970 local; flags star/done/someday; remindAt + alarmId = a normal Time-screen
  reminder via `alarms::remindAt` + new `alarms::lastAddedId()`). `todo_screen.cpp`: Today / Upcoming / Done tabs,
  5 rows of 58 px (drag to scroll), circle = done (Undo toast 5 s), star, tap text = row buttons (Star / Today /
  Tomorrow / Delete; Done tab: Not done / Delete), "Add by voice" + "Type" (QWERTY keyboard, 123 layer, When chips
  Today / Tomorrow / Next week (coming Sunday) / Someday, star, "Say it instead"). No music bar (`setBottomHidden`).
  Messages: Gorgi -> `todo_list` (on change/connect), `dictate` {mode todo|text}; helper -> `dictation`
  {text, due "YYYY-MM-DD", someday, star, at, mode}, `todo_add` / `todo_done` {id, done} / `todo_remove` {id}.
  Helper `todolist.py`: `[todo: add x | tomorrow 17:00]`, `[todo: done x]`, `[todo: undone x]`, `[todo: remove x]`,
  fuzzy find by words (asks "Which one?" when unclear), `parse_spoken` ("buy milk tomorrow at 5" -> Buy milk, 17:00),
  list in the AI context. Old sketches (LittleFS /sketch, NVS "sketch") are deleted on first start (user OK'd).
  `ink_reader.py` and the Inking package removed. Tests: `tools/pc-tests` (todo_preview), `test_todolist.py`.
- **4.6.1 - face fix**: user: "Works good" (4.6), but after adding from the face it stayed on "I'm listening...".
  Cause (since 4.1!): `pclink.cpp` sent "voice"/"heard"/"say"/"level" to `onOther` (checked first), so the face
  never got the conversation and waited for the 60 s safety timeout. Now the voice branch comes before onOther.
  Confirmed ("ok good").
- **4.6.2 - reply text with the voice** (user: listening / thinking / reply text showed before the voice, "looks
  buggy"; picked option B "words appear as spoken"). Helper `Speaker.say(on_start=...)` makes the Edge voice first
  (`boundary="WordBoundary"`, edge-tts 7), then `Conversation._say` sends `say` {text, mood, words: [[ms, words shown]]}
  right before playback (`voice.word_timings`, guesses from word lengths without boundaries). Firmware:
  `ui::setSubtitleReveal(n)` draws only the first n words of the (fully wrapped) subtitle, two lines at a time, turning
  to the next two lines for long replies (no more "..." cut); main `updateSayWords` each frame. Rejected option C
  (hide "You: ..." / "Thinking...") for now. Confirmed ("looks great!").
- **Hebrew plan** (user, 2026-10-24: "Hebrew across the board"; plan doc https://claude.ai/artifact/252ezF3tuXChEh2ictoAsT,
  user picked all recommendations): a Hebrew / English **switch** (Settings + Action Center), English buttons with Hebrew
  content (no mirrored screens), Edge voice **he-IL-HilaNeural**, Hebrew keyboard later. Steps: 4.7 screen shows Hebrew,
  4.8 hears/speaks Hebrew (ivrit.ai `whisper-large-v3-turbo-ct2` via faster-whisper, language "he", ~1.6 GB download on
  first Hebrew use; Hebrew rule in the personality, tags stay English with Hebrew content; to-do find must handle Hebrew
  words + prefixes ה ו ב ל מ ש; Hebrew day words in `parse_spoken`; Hebrew alerts; a "Hebrew model test" in Settings to
  pick the Ollama model - gpt-oss:20b is weak in Hebrew, candidates gpt-oss:120b, gemma4, deepseek-v4.1-flash), 4.9 Hebrew keyboard.
- **4.7 - Hebrew on the screen**: Hebrew letters (alef..tav) from **Fredoka** (OFL, `tools/Fredoka-OFL.txt`) in SUBTITLE,
  TITLE, SMALL, HEADLINE, NOTE (`src/gfont_hebrew.cpp` from `tools/make_fonts.py --hebrew Fredoka.ttf`, weight = Nunito's - 200;
  `gfont::extraFor(font)`). `bidi.cpp` (pure C++): UTF-8 `next`, `dropLast` / `safeCut` / `trimPartial` (never half a letter),
  `isRtl` (first letter Hebrew), `visual()` = a small Unicode bidi for one line (numbers like 17:00 kept, brackets mirrored).
  `Canvas::text` reorders Hebrew lines itself; `Canvas::textIn(x, w)` puts RTL lines at the right edge (music screen + bar,
  to-dos, reminders, notifications, Claude names); subtitles reveal from the right. `ui::cleanText` (was `toAscii`) keeps
  Hebrew, drops vowel marks/emoji. To-dos: TEXT_MAX 112 bytes, NVS "todo"/"list" now packed "TOD2" records (4.6 "TOD1" converted
  on start). Alarm labels (32 B) / reminder texts (64 B) keep UTF-8 (sizes unchanged). PC tests: `bidi_test`, `todo_hebrew.ppm`,
  Time `hebrew.ppm`.
- Screens now: **Music ← Face → Time → To-do → Claude** (icon tabs at the top).

## Handover for the next session (2026-10-24)

- **4.3.1 is confirmed** by the user (with everything in 4.2.1 and 4.3) and merged to main.
- **4.3.2** (Claude alerts) and **4.3.3** (one update window) are confirmed and merged to main. If "finished"
  ever comes too often again, ask for `gorgi-helper.log` (lines "claude hook: ..." show the order of events).
- **4.3.4 is confirmed** (plan % works) and merged to main.
- **4.6 + 4.6.1 (To-do screen, face follows the conversation) are confirmed** ("ok good") and merged to main
  (with 4.4/4.5).
- **4.6.2 is confirmed** ("looks great!", reply text word by word with the voice) and merged to main.
  If voice tasks come out wrong, ask for `gorgi-helper.log` (lines "dictation:" / "new task:" / "to-do ...").
- **4.7 (Hebrew on the screen) released** from branch `claude/project-thread-vbs4n9`, not merged: waiting for the user's test
  (a Hebrew song on the music screen). On "works": mark confirmed, merge, then build 4.8 (Hebrew hearing/speaking, see plan above).

Lessons from 3.9 (keep for future screens):
- Every screen that needs voice should have its own talk button (the user didn't know where to tap).
- Test pure-C++ drawing and logic on the PC first (`tools/pc-tests`), then let GitHub compile.

## Next steps (chosen by the user, in this order; plan each one with the user before coding)

Screens now: **Music ← Face → Time → To-do → Claude** (icon tabs; weather lives in the Action Center).

1. ~~**Time screen**~~ - done in 3.9 (see above).
2. ~~**Sketch pad**~~ - done in 4.0, replaced by the **To-do screen** in 4.6 (finger writing didn't work out).
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
- **More for the Action Center**: quiet mode, lock/sleep PC, big volume; more notification kinds.
- **Smooth sliding between screens**: tried twice (3.7 slide, 3.7.9x new driver) and dropped; see lessons.

Not wanted (don't suggest again): greetings, break reminders.

**Always feel free to suggest new ideas** when the user asks what to build next: give a short list with a
recommendation, then let the user pick, then plan it.
