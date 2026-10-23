"""Connects Claude Code to Gorgi Helper (Settings -> "Connect Claude Code").

Adds small "hooks" to Claude Code's own settings file (~/.claude/settings.json).
Claude Code runs them when a session starts, gets a prompt, uses a tool, needs
you, or finishes; each one just sends the event to Gorgi Helper on this PC:

    curl.exe -s -m 2 --data-binary @- http://127.0.0.1:47811/claude/hook

and a status line command that sends Claude Code's status (with the 5-hour and
weekly usage) the same way; the helper's answer is shown as the status line.

Your own settings and hooks are kept; a backup of the file is made first.
"""

import json
import shutil
import time
from pathlib import Path

PORT = 47811
MARK = f"127.0.0.1:{PORT}/claude"
HOOK_COMMAND = f"curl.exe -s -m 2 --data-binary @- http://{MARK}/hook"
STATUS_COMMAND = f"curl.exe -s -m 2 --data-binary @- http://{MARK}/status"
EVENTS = ["SessionStart", "UserPromptSubmit", "PostToolUse", "Notification", "Stop", "SessionEnd"]
WITH_MATCHER = {"PostToolUse"}  # tool events need a matcher ("*" = every tool)


def settings_path():
    return Path.home() / ".claude" / "settings.json"


def _load(path):
    """The settings as a dict ({} if there's no file). Raises ValueError if it's broken."""
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8").strip()
    if not text:
        return {}
    data = json.loads(text)
    if not isinstance(data, dict):
        raise ValueError("settings.json is not a JSON object")
    return data


def _ours(command):
    return isinstance(command, str) and MARK in command


def _has_our_hook(entries):
    for entry in entries or []:
        for hook in (entry or {}).get("hooks", []) if isinstance(entry, dict) else []:
            if isinstance(hook, dict) and _ours(hook.get("command")):
                return True
    return False


def status(path=None):
    """(hooks connected, status line is ours, somebody else's status line)"""
    path = path or settings_path()
    try:
        data = _load(path)
    except Exception:
        return False, False, False
    hooks = data.get("hooks") if isinstance(data.get("hooks"), dict) else {}
    connected = all(_has_our_hook(hooks.get(e)) for e in EVENTS)
    line = data.get("statusLine") if isinstance(data.get("statusLine"), dict) else None
    ours = bool(line and _ours(line.get("command")))
    other = bool(line and not ours)
    return connected, ours, other


def _write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        backup = path.with_name(f"settings.json.gorgi-backup-{time.strftime('%Y%m%d-%H%M%S')}")
        shutil.copy2(path, backup)
    tmp = path.with_suffix(".json.gorgi-tmp")
    tmp.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    tmp.replace(path)


def connect(path=None, replace_status_line=False):
    """Adds Gorgi's hooks (and status line). Returns a message for the user."""
    path = path or settings_path()
    try:
        data = _load(path)
    except Exception as e:
        return f"Your Claude Code settings file couldn't be read, so I didn't change it.\n\n{path}\n{e}"
    hooks = data.setdefault("hooks", {})
    if not isinstance(hooks, dict):
        return "Your Claude Code settings have an unusual \"hooks\" section, so I didn't change them."
    for event in EVENTS:
        entries = hooks.setdefault(event, [])
        if not isinstance(entries, list):
            return f"Your Claude Code settings have an unusual \"{event}\" hook, so I didn't change them."
        if _has_our_hook(entries):
            continue
        entry = {"hooks": [{"type": "command", "command": HOOK_COMMAND, "timeout": 5}]}
        if event in WITH_MATCHER:
            entry = {"matcher": "*", **entry}
        entries.append(entry)

    line = data.get("statusLine")
    other = isinstance(line, dict) and not _ours(line.get("command"))
    usage = True
    if not line or replace_status_line or not other:
        data["statusLine"] = {"type": "command", "command": STATUS_COMMAND, "padding": 0}
    else:
        usage = False
    _write(path, data)
    msg = "Claude Code is connected! New Claude Code sessions will show up on Gorgi."
    if not usage:
        msg += ("\n\nYou already have your own status line, so I kept it. Gorgi can't show your usage "
                "without its status line.")
    return msg + "\n\nRestart any Claude Code sessions that are already open."


def disconnect(path=None):
    """Removes Gorgi's hooks and status line. Returns a message for the user."""
    path = path or settings_path()
    try:
        data = _load(path)
    except Exception as e:
        return f"Your Claude Code settings file couldn't be read.\n\n{e}"
    hooks = data.get("hooks")
    if isinstance(hooks, dict):
        for event in list(hooks):
            entries = hooks[event]
            if not isinstance(entries, list):
                continue
            kept = []
            for entry in entries:
                if isinstance(entry, dict) and isinstance(entry.get("hooks"), list):
                    entry = dict(entry, hooks=[h for h in entry["hooks"]
                                               if not (isinstance(h, dict) and _ours(h.get("command")))])
                    if not entry["hooks"]:
                        continue
                kept.append(entry)
            if kept:
                hooks[event] = kept
            else:
                del hooks[event]
        if not hooks:
            del data["hooks"]
    line = data.get("statusLine")
    if isinstance(line, dict) and _ours(line.get("command")):
        del data["statusLine"]
    _write(path, data)
    return "Claude Code is disconnected from Gorgi."
