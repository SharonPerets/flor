"""A small Settings window: Ollama key, AI model, language, Gorgi's voices, Claude Code."""

import asyncio
import os
import threading
import tkinter as tk
from tkinter import messagebox, ttk

import claude_setup

VOICES = {
    "Ana - cute, young (US)": "en-US-AnaNeural",
    "Maisie - cute, young (UK)": "en-GB-MaisieNeural",
    "Jenny - friendly (US)": "en-US-JennyNeural",
    "Guy - friendly (US)": "en-US-GuyNeural",
}
VOICES_HE = {
    "Hila - warm (female)": "he-IL-HilaNeural",
    "Avri - calm (male)": "he-IL-AvriNeural",
}
LANGUAGES = {"English": "en", "עברית (Hebrew)": "he"}
HEARING = {
    "Groq - fast, free (needs a key)": "groq",
    "OpenAI - best Hebrew, paid (needs a key)": "openai",
    "On this PC - slow without NVIDIA": "local",
}
HEARING_HINTS = {
    "groq": "Groq key: console.groq.com → API Keys (free account)",
    "openai": "OpenAI key: platform.openai.com → API keys (billing needed)",
    "local": "No key needed. Downloads 1.6 GB the first time.",
}
SUGGESTED_MODELS = ["gpt-oss:20b", "gpt-oss:120b"]


def show_window(config, save_config, list_models, report_dir=None):
    """Shows the Settings window and returns when it's closed.

    Runs in its own process (GorgiHelper.exe --settings): Tkinter must own the
    main thread of a process, or it can crash the app later.
    list_models(key) -> list of model names (may raise).
    """
    root = tk.Tk()
    root.title("Gorgi Helper - Settings")
    root.resizable(False, False)
    root.attributes("-topmost", True)
    frame = ttk.Frame(root, padding=16)
    frame.grid()

    ttk.Label(frame, text="Ollama API key").grid(row=0, column=0, sticky="w")
    key_var = tk.StringVar(value=config.get("ollama_api_key", ""))
    key_entry = ttk.Entry(frame, textvariable=key_var, width=48, show="•")
    key_entry.grid(row=1, column=0, columnspan=2, sticky="we", pady=(2, 2))
    ttk.Label(frame, text="Get one at ollama.com → Settings → Keys", foreground="#666").grid(
        row=2, column=0, columnspan=2, sticky="w", pady=(0, 10))

    ttk.Label(frame, text="AI model").grid(row=3, column=0, sticky="w")
    model_var = tk.StringVar(value=config.get("model") or SUGGESTED_MODELS[0])
    model_box = ttk.Combobox(frame, textvariable=model_var, values=SUGGESTED_MODELS, width=34)
    model_box.grid(row=4, column=0, sticky="we", pady=(2, 10))

    def load_models():
        try:
            names = asyncio.run(list_models(key_var.get().strip()))
        except Exception as e:  # noqa: BLE001
            messagebox.showerror("Gorgi Helper", f"Couldn't load models. Is the key right?\n\n{e}", parent=root)
            return
        if names:
            model_box["values"] = names
            messagebox.showinfo("Gorgi Helper", f"Key works! Found {len(names)} models.\nPick one from the list.",
                                parent=root)

    ttk.Button(frame, text="Test key / load models", command=load_models).grid(row=4, column=1, padx=(8, 0),
                                                                              pady=(2, 10))

    ttk.Label(frame, text="Gorgi's voice").grid(row=5, column=0, sticky="w")
    current_voice = next((k for k, v in VOICES.items() if v == config.get("voice")), list(VOICES)[0])
    voice_var = tk.StringVar(value=current_voice)
    ttk.Combobox(frame, textvariable=voice_var, values=list(VOICES), state="readonly", width=34).grid(
        row=6, column=0, sticky="we", pady=(2, 10))

    # Language: what Gorgi hears and speaks (the switch in Gorgi's Action Center does the same)
    ttk.Label(frame, text="Language").grid(row=10, column=0, sticky="w")
    current_lang = next((k for k, v in LANGUAGES.items() if v == config.get("language")), "English")
    lang_var = tk.StringVar(value=current_lang)
    ttk.Combobox(frame, textvariable=lang_var, values=list(LANGUAGES), state="readonly", width=34).grid(
        row=11, column=0, sticky="we", pady=(2, 10))

    ttk.Label(frame, text="Gorgi's Hebrew voice").grid(row=12, column=0, sticky="w")
    current_he = next((k for k, v in VOICES_HE.items() if v == config.get("voice_he")), list(VOICES_HE)[0])
    voice_he_var = tk.StringVar(value=current_he)
    ttk.Combobox(frame, textvariable=voice_he_var, values=list(VOICES_HE), state="readonly", width=34).grid(
        row=13, column=0, sticky="we", pady=(2, 10))

    # Hebrew hearing: the big Hebrew model is slow on most PCs, so a cloud service can do it (needs a key)
    ttk.Label(frame, text="Hebrew hearing").grid(row=14, column=0, sticky="w")
    current_hearing = next((k for k, v in HEARING.items() if v == (config.get("hearing_he") or "groq")),
                           list(HEARING)[0])
    hearing_var = tk.StringVar(value=current_hearing)
    hearing_box = ttk.Combobox(frame, textvariable=hearing_var, values=list(HEARING), state="readonly", width=34)
    hearing_box.grid(row=15, column=0, sticky="we", pady=(2, 2))
    hearing_key_var = tk.StringVar()
    hearing_key = ttk.Entry(frame, textvariable=hearing_key_var, width=48, show="•")
    hearing_key.grid(row=16, column=0, columnspan=2, sticky="we", pady=(2, 2))
    hearing_hint = tk.StringVar()
    ttk.Label(frame, textvariable=hearing_hint, foreground="#666").grid(row=17, column=0, columnspan=2,
                                                                       sticky="w", pady=(0, 10))
    keys = {"groq": config.get("groq_api_key", ""), "openai": config.get("openai_api_key", "")}
    shown = [None]

    def hearing_changed(_event=None):
        if shown[0] in keys:
            keys[shown[0]] = hearing_key_var.get().strip()
        name = HEARING[hearing_var.get()]
        shown[0] = name
        if name in keys:
            hearing_key.state(["!disabled"])
            hearing_key_var.set(keys[name])
            hearing_hint.set(HEARING_HINTS[name])
        else:
            hearing_key_var.set("")
            hearing_key.state(["disabled"])
            hearing_hint.set(HEARING_HINTS[name])

    hearing_box.bind("<<ComboboxSelected>>", hearing_changed)
    hearing_changed()

    # Hebrew test: asks a few AI models the same Hebrew requests, so you can pick the best one
    test_var = tk.StringVar(value="Which AI model speaks the best Hebrew?")
    ttk.Label(frame, textvariable=test_var, foreground="#666", wraplength=300).grid(
        row=21, column=0, sticky="w", pady=(2, 14))
    test_button = ttk.Button(frame, text="Test Hebrew answers")
    test_button.grid(row=21, column=1, padx=(8, 0), pady=(2, 14))
    best_model = []

    def run_test():
        import model_test

        key = key_var.get().strip()
        test_config = {**config, "ollama_api_key": key}
        try:
            available = asyncio.run(list_models(key))
        except Exception as e:  # noqa: BLE001
            root.after(0, lambda: test_done(None, f"Couldn't load models. Is the key right? ({e})"))
            return
        models = model_test.pick_models(available, model_var.get().strip())
        try:
            report, summary = asyncio.run(model_test.run(
                test_config, models, lambda text: root.after(0, lambda: test_var.set("Testing " + text))))
        except Exception as e:  # noqa: BLE001
            root.after(0, lambda: test_done(None, f"The test failed: {e}"))
            return
        path = None
        if report_dir is not None:
            path = os.path.join(str(report_dir), "hebrew-test.txt")
            with open(path, "w", encoding="utf-8") as f:
                f.write(report)
        best_model[:] = [summary.split(" ")[1]] if summary.startswith("Best: ") else []
        root.after(0, lambda: test_done(path, summary))

    def test_done(path, summary):
        test_button.state(["!disabled"])
        test_var.set(summary)
        if path:
            try:
                os.startfile(path)  # Windows: opens the report in Notepad
            except Exception:  # noqa: BLE001
                pass
        if best_model and best_model[0] != model_var.get().strip():
            if messagebox.askyesno("Gorgi Helper", f"{summary}\n\nUse {best_model[0]} from now on?", parent=root):
                model_var.set(best_model[0])

    def start_test():
        if not key_var.get().strip():
            messagebox.showerror("Gorgi Helper", "Enter the Ollama key first.", parent=root)
            return
        test_button.state(["disabled"])
        test_var.set("Testing... (a few minutes)")
        threading.Thread(target=run_test, daemon=True).start()

    test_button.configure(command=start_test)

    # Claude Code: hooks so Gorgi shows your sessions and usage (changes take effect right away)
    ttk.Label(frame, text="Claude Code").grid(row=22, column=0, sticky="w")
    claude_var = tk.StringVar()
    ttk.Label(frame, textvariable=claude_var, foreground="#666").grid(row=23, column=0, sticky="w", pady=(2, 14))
    claude_button = ttk.Button(frame)
    claude_button.grid(row=23, column=1, padx=(8, 0), pady=(2, 14))

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
        config["ollama_api_key"] = key_var.get().strip()
        config["model"] = model_var.get().strip() or SUGGESTED_MODELS[0]
        config["voice"] = VOICES[voice_var.get()]
        config["voice_he"] = VOICES_HE[voice_he_var.get()]
        config["language"] = LANGUAGES[lang_var.get()]
        hearing_changed()  # keeps the key typed for the service shown now
        config["hearing_he"] = HEARING[hearing_var.get()]
        config["groq_api_key"] = keys["groq"]
        config["openai_api_key"] = keys["openai"]
        save_config(config)
        root.destroy()

    buttons = ttk.Frame(frame)
    buttons.grid(row=30, column=0, columnspan=2, sticky="e")
    ttk.Button(buttons, text="Cancel", command=root.destroy).grid(row=0, column=0, padx=(0, 8))
    ttk.Button(buttons, text="Save", command=save).grid(row=0, column=1)

    key_entry.focus_set()
    root.mainloop()
