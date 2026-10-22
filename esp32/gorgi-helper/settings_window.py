"""A small Settings window: Ollama key, AI model, Gorgi's voice."""

import asyncio
import tkinter as tk
from tkinter import messagebox, ttk

VOICES = {
    "Ana - cute, young (US)": "en-US-AnaNeural",
    "Maisie - cute, young (UK)": "en-GB-MaisieNeural",
    "Jenny - friendly (US)": "en-US-JennyNeural",
    "Guy - friendly (US)": "en-US-GuyNeural",
}
SUGGESTED_MODELS = ["gpt-oss:20b", "gpt-oss:120b"]


def show_window(config, save_config, list_models):
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
        row=6, column=0, sticky="we", pady=(2, 14))

    def save():
        config["ollama_api_key"] = key_var.get().strip()
        config["model"] = model_var.get().strip() or SUGGESTED_MODELS[0]
        config["voice"] = VOICES[voice_var.get()]
        save_config(config)
        root.destroy()

    buttons = ttk.Frame(frame)
    buttons.grid(row=7, column=0, columnspan=2, sticky="e")
    ttk.Button(buttons, text="Cancel", command=root.destroy).grid(row=0, column=0, padx=(0, 8))
    ttk.Button(buttons, text="Save", command=save).grid(row=0, column=1)

    key_entry.focus_set()
    root.mainloop()
