# Gorgi Helper (Windows)

A small app that connects your PC to Gorgi over Wi-Fi:

- Gorgi's **play/pause, next, previous** buttons control whatever is playing (Spotify, YouTube in a browser, ...)
- Gorgi's **volume slider** controls the PC volume, and follows it when you change it on the PC
- Gorgi shows the **song title and artist** and the PC's **time**
- **Talk to Gorgi**: tap its face, speak into the PC microphone, and Gorgi answers out loud

## Install

1. Open https://github.com/SharonPerets/flor/releases/latest and download `GorgiHelper-Setup-<version>.exe`.
2. Run it. Windows may say *"Windows protected your PC"* because the app isn't signed:
   click **More info → Run anyway**. It installs for your user only (no admin password).
3. A small Gorgi face appears in the taskbar tray (click the **^** arrow near the clock if you don't see it).
   It turns teal when it's connected to Gorgi.
4. The **Settings** window opens the first time: paste your **Ollama API key**
   (ollama.com → Settings → Keys), click **Test key / load models**, pick a model, and **Save**.
   You can reopen Settings any time by double-clicking the tray icon.

Gorgi and the PC must be on the same Wi-Fi network.

## Updates

The helper checks GitHub once a day. When a new version is out, a message pops up near the clock.
Right-click the Gorgi icon and choose:

- **Update Gorgi Helper to …** — downloads the new installer (with a progress window), updates itself
  quietly and restarts.
- **Update Gorgi to …** — sends the new firmware to Gorgi over Wi-Fi, with a progress window on the PC.
  Gorgi's screen goes dark while it installs, then it restarts.

**Check for updates** in the same menu checks right away.

Gorgi only accepts updates from the helper it paired with (the first one that connected to it).
To pair it with a different PC, hold Gorgi's clock for 5 seconds: it forgets its Wi-Fi and the
paired helper, then set it up again.

## Talking to Gorgi

- **Tap Gorgi's face** and speak. Your music pauses while you talk.
- Gorgi answers out loud through the PC speakers, with a matching face.
- After answering, Gorgi listens about 5 more seconds for a follow-up. Stay quiet to end.
- **Tap the face again** to stop at any time.
- Music commands work too: "next song", "pause the music", "volume 30", "turn it up".
- Ask about the weather: "will it rain tomorrow?". Gorgi uses the forecast from its weather screen
  (for the city picked there).

If Gorgi says it didn't hear anything: Windows **Settings → Privacy & security → Microphone**,
turn on **Let desktop apps access your microphone**, and check the right microphone is the
default one (**Settings → System → Sound → Input**).

Gorgi and the PC must be on the same Wi-Fi network.

## Memory

Gorgi remembers things you tell it, like your name, the people and pets in your life, what you
like, or plans you mention. They're kept on your PC in `memory.json` (tray menu → **Memories...**
opens it). Say "Gorgi, forget that" or "forget everything" to remove them.

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

## Settings file (`config.json`)

| Setting | What it does |
|---|---|
| `ollama_api_key` | Your Ollama Cloud key |
| `model` | AI model, e.g. `gpt-oss:20b` |
| `ollama_url` | Empty for Ollama Cloud, or `http://localhost:11434` for Ollama on your PC |
| `voice` | Gorgi's voice, e.g. `en-US-AnaNeural` |
| `whisper_model` | `base.en` (fast) or `small.en` (understands better, slower) |
| `gorgi_address` | Gorgi's IP, only if it isn't found automatically |
