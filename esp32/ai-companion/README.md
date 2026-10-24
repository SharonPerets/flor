# Gorgi – AI desk companion

Gorgi is an animated face for the **Waveshare ESP32-S3-Touch-LCD-7** (800×480).
It shows expressions, reacts to touch, and has a media bar to control music on your PC.

| Step | What it does | Status |
|---|---|---|
| 1 | Face, expressions, touch, media bar | ✅ done |
| 2 | Wi-Fi + Gorgi Helper on Windows: real play/pause/next/volume + song name | ✅ done |
| 3 | Voice: tap Gorgi, speak into the PC mic, Gorgi answers out loud (Ollama Cloud) | ✅ this version |
| 4 | Polish: personality, clock, more idle behaviour | later |

## Easiest: install a release

Download `gorgi-full-<version>.bin` from https://github.com/SharonPerets/flor/releases/latest, open
https://espressif.github.io/esptool-js/ in Chrome or Edge, click **Connect**, choose the board, add the
file at address **0x0**, and click **Program**. After that, Gorgi Helper installs new versions over Wi-Fi
(see `esp32/gorgi-helper/README.md`).

To publish a new version: change the version in `esp32/VERSION`, `GORGI_VERSION` in `src/pclink.h`
and `VERSION` in `gorgi-helper/gorgi_helper.py`, and push. GitHub builds everything and publishes it.

## Build it yourself (VS Code + PlatformIO)

1. Install **VS Code**: https://code.visualstudio.com
2. In VS Code open **Extensions** (Ctrl+Shift+X), search **PlatformIO IDE**, install it, and restart VS Code.
3. Download this project: on GitHub, switch to the branch
   `claude/esp32-touch-screen-software-emaips`, then **Code → Download ZIP**, and unzip it.
4. In VS Code: **File → Open Folder…** and pick the `esp32/ai-companion` folder.
   PlatformIO downloads the tools it needs the first time (a few minutes).
5. Connect the board with a USB-C cable to the port marked **UART**.
6. Click the **→ (Upload)** arrow in the blue bar at the bottom of VS Code.
7. When it says `SUCCESS`, the board restarts and Gorgi says hi.

**Upload fails / no port found?**
- Install the CH343 USB driver from Waveshare's wiki page for this board, then try again.
- Hold the **BOOT** button, press and release **RESET**, release **BOOT**, then upload again.
- Try another cable: some USB-C cables only charge.

The plug icon in the blue bar opens the **Serial Monitor**, where Gorgi prints
messages (frames per second, memory) that help if something goes wrong.


## Wi-Fi setup (once)

1. After flashing, Gorgi opens its own Wi-Fi network called **Gorgi-Setup**.
2. On your phone, join **Gorgi-Setup**. A setup page opens (if not, browse to http://192.168.4.1).
3. Tap **Configure WiFi**, pick your home Wi-Fi, enter the password, and **Save**.
4. Gorgi joins your Wi-Fi and says it's online.

To change networks later, **hold the clock (top left) for 5 seconds**: Gorgi forgets
the Wi-Fi and opens **Gorgi-Setup** again.

Then install **Gorgi Helper** on your PC: see `esp32/gorgi-helper/README.md`.

## What you can do

- **Tap the face**: talk to Gorgi through the PC mic (needs Gorgi Helper). Tap again to stop.
- **Hold the face for 1 second**: shows the next expression. Keep doing it to see all 12.
- **Media buttons and volume**: control music and volume on your PC (needs Gorgi Helper).
- **Change screens**: tap an icon in the top bar (music, face, time, sketch, Claude), or swipe sideways in the
  middle of the screen. Order: **Music ← Face → Time → Sketch → Claude**. Gorgi stays on the screen you leave it on
  (it switches to its face when you talk to it or when a timer or reminder goes off).
- **Music line**: the music bar is folded into a slim "♪ Song - Artist ⏯" line at the bottom. Tap it to open the
  full bar (buttons and volume) for 10 seconds.
- **Action Center**: pull down from the top bar. A full screen with the time and date, **Sleep**, a small weather
  chip (tap it for the forecast and to change the city) and **notifications**: an update that's ready (tap
  **Update**: the PC helper first, then Gorgi) and things nobody saw (an alert that wasn't tapped within 10 minutes,
  a Claude alert that came while Gorgi was busy). Clear one with its x. A small orange dot on the handle at the top
  means there are notifications. Push it back up to close it.
- **Time screen** (swipe left from the weather): timers, focus sessions and reminders.
  - Tap **1 / 5 / 10 min** to start a timer (up to 3 at once). Tap a ring to pause or resume it,
    tap its small **x** to cancel it.
  - Tap the **25 min** button to change the focus length (15, 25, 45, 60), then **Start focus**.
    Gorgi doesn't comment on new songs while you focus, and cheers when the session ends.
  - **Next up** lists the next reminders. Tap one to delete it. To add one, tap **Ask me** and say it
    ("remind me at 5 to call mom"; see the helper's README). Ask me works for timers too.
  - While something counts down, a small countdown shows next to the clock on every screen.
  - When something is due, Gorgi wakes up, shows it and bounces every 30 s. **Tap anywhere** to stop it.
    The PC says it out loud if Gorgi Helper is running.
  - Everything is saved on Gorgi (up to 20 reminders), so it survives restarts and works without the PC.
    Anything that went off while Gorgi was off is shown once when it starts again.
- **Sketch screen** (swipe left from the time screen): quick notes and drawings. **Write** or **Draw** at the top
  of the toolbar.
  - **Write**: write a word big in the box. With Gorgi Helper connected (4.5), the PC reads it (Windows'
    handwriting recognition) and it is typed on the line in a clean font; other guesses show as chips in the
    box (tap one to swap it in). Without the PC the handwriting lands at a third of the size. The teal writing
    spot moves along after a short pause (or at once when you write past the dashed line).
  - **Talk** (top right) on this screen = voice typing: say it, and it's typed at the writing spot (English).
    **space** / **new line** buttons; tap the page to move the writing spot. On the lower lines the box moves to
    the top. When the page is full, a new page starts. With the eraser on, rub the page itself.
  - **Draw**: free drawing on the whole paper.
  - Left toolbar: 4 colours, eraser, pen size (tap to cycle 3 sizes), undo (Write: the last word; Draw: the last
    stroke; up to 5), gallery, **Clear** (tap twice: "Sure?"; undo brings it back), **New page**.
  - No music bar on this screen (4.5): the page has 13 lines.
  - On the sketch screen, **swipe on the toolbar** to change screens (on the paper, a swipe draws).
  - Drawings save by themselves (after a 20-second pause, or when you leave). The **gallery** shows the
    last 10: tap one to open it, **New page** for a fresh one, the small **x** to delete one. When there
    are more than 10, the oldest is removed.
  - The first start after installing 4.0 takes a few seconds longer: Gorgi prepares its storage once.
- **Claude screen** (the spark icon): your Claude Code sessions on the PC (working, waiting for you,
  done) and your plan usage (5-hour and weekly, with reset times). Needs Gorgi Helper with
  **Settings → Connect Claude Code**. When a session needs you, or finishes after working a while,
  Gorgi shows it on any screen and the PC says it; tap to dismiss. An orange dot on the icon means
  a session is waiting for you.
- **On a call**: when Teams uses your microphone, the top bar shows **On a call** in red, music pauses
  (and resumes after), Gorgi stays quiet (no spoken alerts), and a red **Mute / unmute** button next to
  the face toggles your Teams mic (Teams' Ctrl+Shift+M shortcut).
- **The clock** comes from the internet (Israel time, summer/winter time automatic), or from the PC
  until the internet answers.
- **Leave it alone**: after 1 minute Gorgi gets sleepy, after 3 minutes it falls asleep.
  Tap to wake it up.

## Files

| File | What's inside |
|---|---|
| `src/main.cpp` | Behaviour: touch, demo conversation, sleeping |
| `src/face.cpp` | The face: expressions, blinking, looking around, talking mouth |
| `src/ui.cpp` | Top bar, subtitles, media bar |
| `src/music_screen.cpp` | The music screen: album art, song, progress |
| `src/weather.cpp` | The weather screen: forecast, weather pictures, city picker |
| `src/weather_net.cpp` | Downloads the forecast in the background |
| `src/time_screen.cpp` | The Time screen: countdown rings, buttons, the reminder list |
| `src/alarms.cpp` | Keeps timers, focus sessions and reminders (saved in flash) and says when one is due |
| `src/wallclock.cpp` | The time of day from the internet (Israel time zone) |
| `src/sketch.cpp` | The Sketch screen: toolbar, paper, gallery, packing drawings small |
| `src/sketch_store.cpp` | Saves and loads sketches in flash (LittleFS) |
| `src/tabs.cpp` | The screen icons in the top bar, the slim music line, the Teams mute button |
| `src/action_center.cpp` | The Action Center: notifications, weather, Sleep |
| `src/claude_screen.cpp` | The Claude screen: sessions and usage |
| `src/gfx.cpp` | Smooth-edged drawing (circles, rounded boxes, icons, text) |
| `src/display.cpp` | Draws in fast memory, strip by strip, then copies to the screen |
| `src/gfont_data.cpp` | Smooth text, made from the Nunito font by `tools/make_fonts.py` |
| `src/pclink.cpp` | Wi-Fi setup and the connection to Gorgi Helper |
| `src/board.cpp` | Screen, touch and backlight drivers for this board |
| `platformio.ini` | Board and library settings |

To change an expression, edit the `MOODS` table at the top of `src/face.cpp`.

## Credits

Text uses the Nunito font, (c) The Nunito Project Authors, SIL Open Font License 1.1
(`tools/Nunito-OFL.txt`).
