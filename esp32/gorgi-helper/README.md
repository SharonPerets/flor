# Gorgi Helper (Windows)

A small app that connects your PC to Gorgi over Wi-Fi:

- Gorgi's **play/pause, next, previous** buttons control whatever is playing (Spotify, YouTube in a browser, ...)
- Gorgi's **volume slider** controls the PC volume, and follows it when you change it on the PC
- Gorgi shows the **song title and artist** and the PC's **time**

## Install

1. On GitHub open **Actions → Gorgi Helper (Windows) → latest run** and download **GorgiHelper** (a zip with `GorgiHelper.exe`).
2. Unzip it and put `GorgiHelper.exe` somewhere permanent, for example `Documents\Gorgi`.
3. Double-click it. Windows may say *"Windows protected your PC"* because the app isn't signed:
   click **More info → Run anyway**.
4. A small Gorgi face appears in the taskbar tray (click the **^** arrow near the clock if you don't see it).
   It turns teal when it's connected to Gorgi.
5. Right-click the icon → **Start with Windows** so it runs automatically.

Gorgi and the PC must be on the same Wi-Fi network.

## If it doesn't connect

- Right-click the tray icon to see the status.
- Allow it through the Windows firewall if Windows asks.
- Set Gorgi's address by hand: right-click → **Open settings folder**, edit `config.json`
  and put Gorgi's IP in `"gorgi_address"` (Gorgi's IP is shown in the router, or in the
  serial monitor). Then quit and restart the helper.
- The log file `gorgi-helper.log` is in the same folder.

## Run from source (optional)

```
pip install -r requirements.txt
python gorgi_helper.py --no-tray
```
