"""A small progress window, shown while updates download and install.

Runs as its own process (GorgiHelper.exe --progress "Title"), because Tkinter
must own the main thread of a process. The helper sends it lines on stdin:
    42 Downloading...      -> 42% and a message
    DONE Gorgi updated      -> show the message, close after a few seconds
    FAIL Something broke    -> show the message, wait for the user to close it
"""

import queue
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import ttk


def run_window(title):
    lines = queue.Queue()

    def read_stdin():
        for line in sys.stdin:
            lines.put(line.rstrip("\n"))
        lines.put(None)  # the helper went away

    threading.Thread(target=read_stdin, daemon=True).start()

    root = tk.Tk()
    root.title(title)
    root.resizable(False, False)
    root.attributes("-topmost", True)
    frame = ttk.Frame(root, padding=18)
    frame.grid()
    message = tk.StringVar(value="Starting...")
    ttk.Label(frame, textvariable=message, width=46).grid(row=0, column=0, sticky="w", pady=(0, 10))
    bar = ttk.Progressbar(frame, length=360, maximum=100)
    bar.grid(row=1, column=0)
    close_button = ttk.Button(frame, text="Close", command=root.destroy)
    finished = [False]

    def poll():
        try:
            while True:
                line = lines.get_nowait()
                if line is None:
                    if not finished[0]:  # the helper stopped without saying how it went
                        root.destroy()
                        return
                    continue
                head, _, text = line.partition(" ")
                if head == "DONE":
                    finished[0] = True
                    bar["value"] = 100
                    message.set(text)
                    root.after(4000, root.destroy)
                elif head == "FAIL":
                    finished[0] = True
                    message.set(text)
                    close_button.grid(row=2, column=0, sticky="e", pady=(12, 0))
                else:
                    try:
                        bar["value"] = max(0, min(100, int(head)))
                    except ValueError:
                        pass
                    if text:
                        message.set(text)
        except queue.Empty:
            pass
        except tk.TclError:  # window already closed
            return
        root.after(100, poll)

    root.after(100, poll)
    root.mainloop()


class ProgressWindow:
    """Used by the helper: opens the window process and sends it updates."""

    def __init__(self, title):
        if getattr(sys, "frozen", False):
            command = [sys.executable, "--progress", title]
        else:
            command = [sys.executable, __file__.replace("progress_window.py", "gorgi_helper.py"), "--progress", title]
        try:
            self.proc = subprocess.Popen(command, stdin=subprocess.PIPE, text=True, encoding="utf-8")
        except OSError:
            self.proc = None
        self.last = None

    def _send(self, line):
        if not self.proc or self.proc.poll() is not None:
            return
        try:
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()
        except (OSError, ValueError):
            pass

    def update(self, percent, text=""):
        line = f"{int(percent)} {text}"
        if line != self.last:  # don't flood it
            self.last = line
            self._send(line)

    def done(self, text):
        self._send(f"DONE {text}")
        self._close_input()

    def fail(self, text):
        self._send(f"FAIL {text}")
        self._close_input()

    def close(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()

    def _close_input(self):
        try:
            if self.proc:
                self.proc.stdin.close()
        except OSError:
            pass
