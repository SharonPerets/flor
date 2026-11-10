"""A small Settings window: AI keys (Groq, Gemini, Ollama), Gorgi's voice, Claude Code, YouTube Music."""

import tkinter as tk
from tkinter import messagebox, ttk

import claude_setup

VOICES = {
    "Ana - cute, young (US)": "en-US-AnaNeural",
    "Maisie - cute, young (UK)": "en-GB-MaisieNeural",
    "Jenny - friendly (US)": "en-US-JennyNeural",
    "Guy - friendly (US)": "en-US-GuyNeural",
}
KEYS = [  # (config key, label, where to get one)
    ("groq_api_key", "Groq key (Gorgi asks Groq first)", "Free at console.groq.com → API Keys"),
    ("gemini_api_key", "Gemini key (backup when Groq's limit runs out)", "Free at aistudio.google.com → Get API key"),
    ("ollama_api_key", "Ollama key (optional, asked last)", "ollama.com → Settings → Keys (needs paid usage now)"),
]


YT_STEPS = """Gorgi needs your YouTube Music sign-in to show your Liked songs and to like or dislike songs.
Copy it once from your browser (about 2 minutes):

1. In Chrome or Edge, open music.youtube.com (signed in, as usual).
2. Press F12. In the panel that opens, click the Network tab.
3. In its Filter box, type:  browse
4. On the YouTube Music page, click Library. A line called "browse" appears in the list.
5. Right-click that line → Copy → Copy request headers.
6. Paste it in the box below and press Connect. You can close the F12 panel after that.

This works like a password: it stays only on this PC (in Gorgi Helper's settings folder)
and is only sent to YouTube. If you sign out of YouTube Music, connect again."""


def connect_youtube_window(root, connect):
    """The "Connect YouTube Music" steps, a box to paste into, and a Connect button.
    connect(text) -> (ok, message). Returns True if it connected."""
    win = tk.Toplevel(root)
    win.title("Connect YouTube Music")
    win.resizable(False, False)
    win.attributes("-topmost", True)
    win.transient(root)
    frame = ttk.Frame(win, padding=16)
    frame.grid()
    ttk.Label(frame, text=YT_STEPS, justify="left").grid(row=0, column=0, columnspan=2, sticky="w")
    box = tk.Text(frame, width=70, height=8, wrap="none")
    box.grid(row=1, column=0, columnspan=2, sticky="we", pady=(10, 6))
    result = tk.StringVar(value="")
    ttk.Label(frame, textvariable=result, foreground="#444", wraplength=480, justify="left").grid(
        row=2, column=0, sticky="w")
    done = {"ok": False}

    def go():
        result.set("Checking with YouTube Music...")
        win.update()
        try:
            ok, message = connect(box.get("1.0", "end"))
        except Exception as e:  # noqa: BLE001
            ok, message = False, f"Couldn't connect: {e}"
        result.set(("✓ " if ok else "✗ ") + message)
        if ok:
            done["ok"] = True
            box.delete("1.0", "end")  # don't leave the sign-in on screen
            connect_button.configure(text="Close", command=win.destroy)

    connect_button = ttk.Button(frame, text="Connect", command=go)
    connect_button.grid(row=2, column=1, sticky="e", padx=(8, 0))
    box.focus_set()
    win.grab_set()
    root.wait_window(win)
    return done["ok"]


def show_window(config, save_config, check_keys, youtube=None):
    """Shows the Settings window and returns when it's closed.

    Runs in its own process (GorgiHelper.exe --settings): Tkinter must own the
    main thread of a process, or it can crash the app later.
    check_keys({config key: key}) -> [(service name, ok, short text)].
    youtube: (is_connected(), connect(text) -> (ok, message), disconnect()), or None.
    """
    root = tk.Tk()
    root.title("Gorgi Helper - Settings")
    root.resizable(False, False)
    root.attributes("-topmost", True)
    frame = ttk.Frame(root, padding=16)
    frame.grid()

    key_vars = {}
    row = 0
    for name, label, hint in KEYS:
        ttk.Label(frame, text=label).grid(row=row, column=0, columnspan=2, sticky="w")
        key_vars[name] = tk.StringVar(value=config.get(name, ""))
        ttk.Entry(frame, textvariable=key_vars[name], width=48, show="•").grid(
            row=row + 1, column=0, columnspan=2, sticky="we", pady=(2, 2))
        ttk.Label(frame, text=hint, foreground="#666").grid(row=row + 2, column=0, columnspan=2, sticky="w",
                                                            pady=(0, 8))
        row += 3

    test_var = tk.StringVar(value="")
    ttk.Label(frame, textvariable=test_var, foreground="#444", justify="left").grid(row=row, column=0, sticky="w",
                                                                                   pady=(0, 12))

    def test_keys():
        keys = {k: v.get().strip() for k, v in key_vars.items()}
        if not any(keys.values()):
            test_var.set("Paste a key first.")
            return
        test_var.set("Testing...")
        root.update()
        try:
            results = check_keys(keys)
        except Exception as e:  # noqa: BLE001
            test_var.set(f"Couldn't test: {e}")
            return
        test_var.set("\n".join(f"{'✓' if ok else '✗'} {name}: {text}" for name, ok, text in results))

    ttk.Button(frame, text="Test keys", command=test_keys).grid(row=row, column=1, padx=(8, 0), pady=(0, 12),
                                                                sticky="ne")
    row += 1

    ttk.Label(frame, text="Gorgi's voice").grid(row=row + 0, column=0, sticky="w")
    current_voice = next((k for k, v in VOICES.items() if v == config.get("voice")), list(VOICES)[0])
    voice_var = tk.StringVar(value=current_voice)
    ttk.Combobox(frame, textvariable=voice_var, values=list(VOICES), state="readonly", width=34).grid(
        row=row + 1, column=0, sticky="we", pady=(2, 14))

    # Claude Code: hooks so Gorgi shows your sessions and usage (changes take effect right away)
    ttk.Label(frame, text="Claude Code").grid(row=row + 2, column=0, sticky="w")
    claude_var = tk.StringVar()
    ttk.Label(frame, textvariable=claude_var, foreground="#666").grid(row=row + 3, column=0, sticky="w", pady=(2, 14))
    claude_button = ttk.Button(frame)
    claude_button.grid(row=row + 3, column=1, padx=(8, 0), pady=(2, 14))

    def refresh_claude():
        connected, ours, other = claude_setup.status()
        if connected:
            claude_var.set("Connected" + ("" if ours else " (usage needs Gorgi's status line)"))
            claude_button.configure(text="Disconnect", command=disconnect_claude)
        else:
            claude_var.set("Not connected: Gorgi can't see your sessions")
            claude_button.configure(text="Connect Claude Code", command=connect_claude)

    def connect_claude():
        replace = False
        if claude_setup.status()[2]:
            replace = messagebox.askyesno(
                "Gorgi Helper", "You already have your own Claude Code status line.\n\nReplace it with Gorgi's, "
                "so Gorgi can show your usage? (A backup of your settings is kept.)", parent=root)
        messagebox.showinfo("Gorgi Helper", claude_setup.connect(replace_status_line=replace), parent=root)
        refresh_claude()

    def disconnect_claude():
        messagebox.showinfo("Gorgi Helper", claude_setup.disconnect(), parent=root)
        refresh_claude()

    refresh_claude()

    # YouTube Music: your sign-in, for Liked songs and like / dislike (takes effect right away)
    row += 4
    if youtube is not None:
        is_connected, connect_yt, disconnect_yt = youtube
        ttk.Label(frame, text="YouTube Music").grid(row=row, column=0, sticky="w")
        yt_var = tk.StringVar()
        ttk.Label(frame, textvariable=yt_var, foreground="#666").grid(row=row + 1, column=0, sticky="w", pady=(2, 14))
        yt_button = ttk.Button(frame)
        yt_button.grid(row=row + 1, column=1, padx=(8, 0), pady=(2, 14))

        def refresh_yt():
            if is_connected():
                yt_var.set("Connected: Liked songs and thumbs work")
                yt_button.configure(text="Disconnect", command=lambda: (disconnect_yt(), refresh_yt()))
            else:
                yt_var.set("Not connected: Gorgi can search and play, but not like")
                yt_button.configure(text="Connect YouTube Music...",
                                    command=lambda: (connect_youtube_window(root, connect_yt), refresh_yt()))

        refresh_yt()
        row += 2
    row -= 4

    def save():
        for name, var in key_vars.items():
            config[name] = var.get().strip()
        config["voice"] = VOICES[voice_var.get()]
        save_config(config)
        root.destroy()

    buttons = ttk.Frame(frame)
    buttons.grid(row=row + 8, column=0, columnspan=2, sticky="e")
    ttk.Button(buttons, text="Cancel", command=root.destroy).grid(row=0, column=0, padx=(0, 8))
    ttk.Button(buttons, text="Save", command=save).grid(row=0, column=1)

    root.mainloop()
