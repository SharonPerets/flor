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
- **Swipe sideways** in the middle of the screen to change screens. Order: **Music ← Face →
  Weather → Time**. After 45 seconds untouched, Gorgi goes back to its face.
- **Music screen** (swipe right): album art, the song, and how far into it you are.
- **Weather screen** (swipe left): now and the next 7 days. Tap the city name to pick another
  city in Israel (Nahariya is the default). The forecast comes from
  [Open-Meteo](https://open-meteo.com) and updates every 30 minutes.
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
