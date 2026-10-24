# Gorgi Helper (Windows)

A small app that connects your PC to Gorgi over Wi-Fi:

- Gorgi's **play/pause, next, previous** buttons control whatever is playing (Spotify, YouTube in a browser, ...)
- Gorgi's **volume slider** controls the PC volume, and follows it when you change it on the PC
- Gorgi shows the **song title and artist** and the PC's **time**
- **Talk to Gorgi**: tap its face, speak into the PC microphone, and Gorgi answers out loud
- **Timers and reminders** by voice, and Gorgi's voice says them out loud when they're due

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

The helper checks GitHub once a day. When a new version is out, a message pops up near the clock and
the top of the Gorgi icon's menu (right-click) says **Update to …** (otherwise **Check for updates…**).

That opens the **Gorgi updates** window: both versions and one **Update all** button.
1. Gorgi Helper downloads and installs itself. The window closes for a few seconds while it
   reinstalls, and the new helper opens it again by itself.
2. The new firmware goes to Gorgi over Wi-Fi (Gorgi's screen goes dark while it installs).
3. "All done" only shows once Gorgi is back with the new version. If something fails, the window
   says what and offers **Try again**.

Tapping **Update** in Gorgi's Action Center (swipe down from the top bar) does the same, and opens
the same window on the PC.

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

## Timers and reminders

Ask Gorgi, for example:

- "Set a timer for 10 minutes for the pasta", "how long is left?", "cancel the pasta timer"
- "Start a focus session" (25 minutes) or "a 45 minute focus session"
- "Remind me in 20 minutes to take the laundry out", "remind me at 5 to call mom",
  "remind me tomorrow at 8:30 about the dentist"
- "Every day at 8 remind me to take my vitamins", "every Sunday and Tuesday at 7 pm remind me to take out the trash"
  ("weekdays" means Sunday to Thursday)
- "What reminders do I have?", "cancel the vitamins reminder"

They're kept on Gorgi itself, so they go off even when the PC is off. When one is due, Gorgi shows it,
and if the helper is running, a pop-up appears and Gorgi's voice says it on the PC (music pauses for a moment).

## To-do list

The list lives on Gorgi's To-do screen. On that screen, **Add by voice** writes down what you say as a task
("buy milk tomorrow" -> "Buy milk", due tomorrow; "at 5" adds a reminder). From the face you can ask Gorgi:

- "Add call the bank to my list", "put renew the passport on my list for Friday, it's important"
- "I finished the bank thing", "mark buy milk as done", "remove buy milk"
- "What's on my list today?"

(`todolist.py` turns the AI's `[todo: add ... | day]`, `[todo: done ...]`, `[todo: remove ...]` tags into messages
for Gorgi and finds tasks by their words. Test: `python test_todolist.py`.)
You can see, pause and delete them on Gorgi's **Time** screen too.

## Claude Code

Gorgi can show your Claude Code sessions (working / waiting for you / done) and your plan usage.
Open **Settings** and click **Connect Claude Code**. This adds small "hooks" and a status line to
Claude Code's settings (`%USERPROFILE%\.claude\settings.json`; your own settings are kept and a backup is
made). Restart open Claude Code sessions afterwards. **Disconnect** in the same place removes them.

- When a session needs your OK or answer, or finishes after working a while, Gorgi shows it and says it.
  "Finished" waits 15 s of quiet first (Claude often stops between steps and carries on); a stop that ends
  with a question counts as "needs you". Permission prompts, questions with buttons and plans to approve
  are "needs you" at once. No pop-ups or voice while Gorgi isn't connected.
- **Usage** (5-hour and weekly %, like Settings → Usage) is read for the Claude account signed in to Claude
  **on this PC** (not your browser). The helper reads the Claude desktop app's own sign-in (read-only; the app
  renews it itself), or Claude Code's saved sign-in if it's still fresh, and asks Anthropic every 5 minutes. It
  never renews a sign-in itself (that would sign Claude Code out). This isn't an official feature and may stop
  working one day; then Gorgi shows "Plan usage not available" (or the last numbers with their age).
  What happened last is in `claude-usage.json` (settings folder; it never contains the sign-in).
- In the terminal version of Claude Code, the status line also shows the model and usage (e.g. `Opus | 5h 62% | week 23%`).
- Ask Gorgi "how much Claude usage do I have left?" or "is Claude done?".

## Teams calls

When Teams uses your microphone, Gorgi shows **On a call**, pauses your music (and resumes it after
the call), and doesn't speak alerts or show pop-ups. The red button on Gorgi presses Teams' mute
shortcut (Ctrl+Shift+M) for you.

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
