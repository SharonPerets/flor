# Gorgi – AI desk companion

Gorgi is an animated face for the **Waveshare ESP32-S3-Touch-LCD-7** (800×480).
It shows expressions, reacts to touch, and has a media bar to control music on your PC.

| Step | What it does | Status |
|---|---|---|
| 1 | Face, expressions, touch, media bar (demo only) | ✅ this version |
| 2 | Gorgi Helper on Windows: real play/pause/next/volume + song name | next |
| 3 | Voice: tap Gorgi, speak into the PC mic, Gorgi answers out loud (Ollama Cloud) | later |
| 4 | Polish: personality, clock, more idle behaviour | later |

## Upload to the board (first time)

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

### Alternative: flash from the browser (no install)

Every push to GitHub builds the firmware automatically
(**Actions → Gorgi firmware → latest run → gorgi-firmware**).
Download it, open https://espressif.github.io/esptool-js/ in Chrome or Edge, click **Connect**,
choose the board, add `gorgi-full.bin` at address **0x0**, and click **Program**.

## What you can do in step 1

- **Tap the face**: Gorgi listens, thinks, and answers (a demo answer for now).
- **Hold the face for 1 second**: shows the next expression. Keep doing it to see all 12.
- **Media buttons and volume**: Gorgi reacts. They control the PC in step 2.
- **Leave it alone**: after 1 minute Gorgi gets sleepy, after 3 minutes it falls asleep.
  Tap to wake it up.

## Files

| File | What's inside |
|---|---|
| `src/main.cpp` | Behaviour: touch, demo conversation, sleeping |
| `src/face.cpp` | The face: expressions, blinking, looking around, talking mouth |
| `src/ui.cpp` | Top bar, subtitles, media bar |
| `src/board.cpp` | Screen, touch and backlight drivers for this board |
| `platformio.ini` | Board and library settings |

To change an expression, edit the `MOODS` table at the top of `src/face.cpp`.
