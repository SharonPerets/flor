"""The "Gorgi updates" window: one place to update Gorgi Helper and Gorgi.

Runs as its own process (GorgiHelper.exe --updates), because Tkinter must own
the main thread of a process. The helper and the window talk in lines:

    helper -> window (stdin):  one JSON state per line, e.g.
        {"rows": [{"name": "Gorgi Helper", "now": "4.3.2", "new": "4.4", "state": "busy"}, ...],
         "text": "Downloading...", "percent": 42, "button": "start"}
        row states: todo, busy, ok, fail, offline, same.  button: start, retry, close or "".
    window -> helper (stdout): "ready", then "start", "retry" or "close"

If the helper goes away (it's restarting to install itself), the window closes;
the new helper opens it again and carries on with Gorgi.
"""

import json
import queue
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import ttk

MARKS = {"todo": ("", "#555555"), "busy": ("Updating...", "#0b7285"), "ok": ("✔ Done", "#2b8a3e"),
         "fail": ("✖ Failed", "#c92a2a"), "offline": ("Not connected", "#868e96"),
         "same": ("✔ Up to date", "#2b8a3e")}
BUTTONS = {"start": "Update all", "retry": "Try again", "close": "Close"}


def run_window():
    lines = queue.Queue()
    out = sys.stdout

    def read_stdin():
        for line in sys.stdin:
            lines.put(line.rstrip("\n"))
        lines.put(None)  # the helper went away

    def tell(word):
        try:
            out.write(word + "\n")
            out.flush()
        except (OSError, ValueError, AttributeError):
            pass

    threading.Thread(target=read_stdin, daemon=True).start()

    root = tk.Tk()
    root.title("Gorgi updates")
    root.resizable(False, False)
    root.attributes("-topmost", True)
    root.after(1500, lambda: root.attributes("-topmost", False))  # in front once, not forever
    frame = ttk.Frame(root, padding=18)
    frame.grid()
    ttk.Label(frame, text="Gorgi updates", font=("Segoe UI", 13, "bold")).grid(
        row=0, column=0, columnspan=3, sticky="w", pady=(0, 10))
    row_widgets = []
    for i in range(2):
        name = ttk.Label(frame, text="", width=14)
        versions = ttk.Label(frame, text="", width=20)
        mark = tk.Label(frame, text="", width=14, anchor="w")
        name.grid(row=1 + i, column=0, sticky="w", pady=2)
        versions.grid(row=1 + i, column=1, sticky="w", pady=2)
        mark.grid(row=1 + i, column=2, sticky="w", pady=2)
        row_widgets.append((name, versions, mark))
    message = tk.StringVar(value="Checking for updates...")
    ttk.Label(frame, textvariable=message, width=52, wraplength=380).grid(
        row=3, column=0, columnspan=3, sticky="w", pady=(12, 8))
    bar = ttk.Progressbar(frame, length=380, maximum=100)
    bar.grid(row=4, column=0, columnspan=3)
    button_word = [""]

    def press():
        word = button_word[0]
        if word == "close":
            close()
        elif word:
            button.state(["disabled"])
            tell(word)

    button = ttk.Button(frame, text="", command=press)
    finished = [False]

    def close():
        tell("close")
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", close)
    tell("ready")  # lets the helper know the window works (its self-test checks this)

    def show(state):
        rows = state.get("rows") or []
        for i, (name, versions, mark) in enumerate(row_widgets):
            if i < len(rows):
                r = rows[i]
                name.config(text=r.get("name", ""))
                now, new = r.get("now") or "?", r.get("new") or ""
                versions.config(text=f"{now} → {new}" if new and new != now else now)
                label, colour = MARKS.get(r.get("state"), ("", "#555555"))
                mark.config(text=label, fg=colour)
            else:
                name.config(text="")
                versions.config(text="")
                mark.config(text="")
        message.set(state.get("text", ""))
        percent = state.get("percent")
        if isinstance(percent, (int, float)):
            bar.grid()
            bar.config(mode="determinate")
            bar.stop()
            bar["value"] = max(0, min(100, percent))
        elif percent == "wait":
            bar.grid()
            if str(bar.cget("mode")) != "indeterminate":
                bar.config(mode="indeterminate")
                bar.start(15)
        else:
            bar.stop()
            bar.grid_remove()
        word = state.get("button") or ""
        button_word[0] = word
        finished[0] = word == "close"
        if word:
            button.config(text=BUTTONS.get(word, word))
            button.state(["!disabled"])
            button.grid(row=5, column=0, columnspan=3, sticky="e", pady=(12, 0))
        else:
            button.grid_remove()
        if state.get("raise"):
            root.deiconify()
            root.lift()
            root.focus_force()

    def poll():
        try:
            while True:
                line = lines.get_nowait()
                if line is None:
                    root.destroy()  # the helper quit (or restarts to install itself)
                    return
                try:
                    state = json.loads(line)
                except ValueError:
                    continue
                if isinstance(state, dict):
                    show(state)
        except queue.Empty:
            pass
        except tk.TclError:  # window already closed
            return
        root.after(100, poll)

    root.after(100, poll)
    root.mainloop()


class UpdateWindow:
    """Used by the helper: opens the window process, sends it states, hears its buttons.
    on_word(word) is called from a background thread."""

    def __init__(self, on_word):
        if getattr(sys, "frozen", False):
            command = [sys.executable, "--updates"]
        else:
            command = [sys.executable, __file__.replace("update_window.py", "gorgi_helper.py"), "--updates"]
        try:
            self.proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         text=True, encoding="utf-8")
        except OSError:
            self.proc = None
            return
        self.last = None

        def listen():
            for line in self.proc.stdout:
                on_word(line.strip())
            on_word("gone")

        threading.Thread(target=listen, daemon=True).start()

    @property
    def open(self):
        return bool(self.proc and self.proc.poll() is None)

    def show(self, state):
        if not self.open:
            return
        line = json.dumps(state)
        if line == self.last and not state.get("raise"):
            return  # don't flood it
        self.last = line
        try:
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()
        except (OSError, ValueError):
            pass

    def close(self):
        if self.open:
            try:
                self.proc.stdin.close()  # the window closes itself
            except OSError:
                self.proc.terminate()
