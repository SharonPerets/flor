"""A small Settings window: AI keys (Groq, Gemini, Ollama), Gorgi's voice, Claude Code."""

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


def show_window(config, save_config, check_keys):
    """Shows the Settings window and returns when it's closed.

    Runs in its own process (GorgiHelper.exe --settings): Tkinter must own the
    main thread of a process, or it can crash the app later.
    check_keys({config key: key}) -> [(service name, ok, short text)].
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

    def save():
        for name, var in key_vars.items():
            config[name] = var.get().strip()
        config["voice"] = VOICES[voice_var.get()]
        save_config(config)
        root.destroy()

    buttons = ttk.Frame(frame)
    buttons.grid(row=row + 4, column=0, columnspan=2, sticky="e")
    ttk.Button(buttons, text="Cancel", command=root.destroy).grid(row=0, column=0, padx=(0, 8))
    ttk.Button(buttons, text="Save", command=save).grid(row=0, column=1)

    root.mainloop()
