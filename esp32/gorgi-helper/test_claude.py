"""Tests for the Claude Code parts (run: python test_claude.py)."""
import asyncio
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import call_watch
import claude_setup as cs
from claude_watch import ClaudeWatch

# ---- settings: keeps the user's own things, adds ours once, removes cleanly
d = Path(tempfile.mkdtemp())
p = d / "settings.json"
mine = {"model": "opus", "hooks": {"Stop": [{"hooks": [{"type": "command", "command": "say done"}]}]},
        "statusLine": {"type": "command", "command": "my-status.sh"}}
p.write_text(json.dumps(mine))
assert cs.status(p) == (False, False, True)
print(cs.connect(p))
data = json.loads(p.read_text())
assert data["model"] == "opus" and data["statusLine"]["command"] == "my-status.sh"
assert data["hooks"]["Stop"][0]["hooks"][0]["command"] == "say done" and len(data["hooks"]["Stop"]) == 2
assert data["hooks"]["PostToolUse"][0]["matcher"] == "*"
cs.connect(p)  # twice: no duplicates
assert len(json.loads(p.read_text())["hooks"]["Stop"]) == 2
assert cs.status(p) == (True, False, True)
cs.connect(p, replace_status_line=True)
assert cs.status(p) == (True, True, False)
print(cs.disconnect(p))
data = json.loads(p.read_text())
assert data["hooks"] == {"Stop": [{"hooks": [{"type": "command", "command": "say done"}]}]} and "statusLine" not in data
assert any(f.name.startswith("settings.json.gorgi-backup") for f in d.iterdir())
(d / "broken.json").write_text("{not json")
assert "couldn't be read" in cs.connect(d / "broken.json")
assert cs.connect(d / "new" / "settings.json") and cs.status(d / "new" / "settings.json") == (True, True, False)

# ---- sessions
t = [1000.0]
alerts, changes = [], []
w = ClaudeWatch(on_change=lambda: changes.append(1), on_alert=lambda k, n, x: alerts.append((k, n, x)), clock=lambda: t[0])
ev = lambda name, **kw: w.hook({"hook_event_name": name, "session_id": "s1", "cwd": "C:\\Users\\sharon\\code\\flor", **kw})
ev("SessionStart"); ev("UserPromptSubmit"); t[0] += 5; ev("PostToolUse")
assert w.message()["sessions"][0] == {"name": "flor", "state": "working", "text": "", "since": 1000}
ev("Notification", message="Claude needs your permission to use Bash")
assert alerts[-1] == ("waiting", "flor", "Claude needs you in flor") and w.message()["sessions"][0]["text"] == "Needs your OK"
t[0] += 3; ev("PostToolUse"); t[0] += 30; ev("Stop")
assert alerts[-1] == ("done", "flor", "Claude finished in flor")
n = len(alerts); ev("Notification", message="Claude is waiting for your input")
assert len(alerts) == n and w.message()["sessions"][0]["state"] == "done"  # no second alert
ev("UserPromptSubmit"); t[0] += 2; ev("Stop")
assert len(alerts) == n  # a quick answer: no alert
w.hook({"hook_event_name": "UserPromptSubmit", "session_id": "s2", "cwd": "/home/x/api-server"})
assert [s["name"] for s in w.message()["sessions"]] == ["api-server", "flor"]
ev("SessionEnd")
assert [s["name"] for s in w.message()["sessions"]] == ["api-server"]
line = w.status({"model": {"display_name": "Opus"}, "rate_limits": {"five_hour": {"used_percentage": 62.4, "resets_at": 1761250000},
                                                                   "seven_day": {"used_percentage": 23, "resets_at": "2026-10-30T09:00:00Z"}}})
assert line == "Opus | 5h 62% | week 23%", line
u = w.message()["usage"]
assert u["five"] == 62.4 and u["week_reset"] > 1700000000
print(w.summary())

# ---- the web server, with curl like Claude Code does
async def server_test():
    w2 = ClaudeWatch()
    task = asyncio.create_task(w2.serve())
    await asyncio.sleep(0.3)
    body = json.dumps({"hook_event_name": "UserPromptSubmit", "session_id": "x", "cwd": "/a/proj"})
    cmd = cs.HOOK_COMMAND.replace("curl.exe", "curl")
    r = await asyncio.to_thread(subprocess.run, cmd, shell=True, input=body.encode(), capture_output=True)
    assert r.returncode == 0, r
    assert w2.message()["sessions"][0]["state"] == "working"
    big = json.dumps({"model": {"display_name": "Sonnet"}, "pad": "x" * 200000,
                      "rate_limits": {"five_hour": {"used_percentage": 10}, "seven_day": {"used_percentage": 5}}})
    r = await asyncio.to_thread(subprocess.run, cs.STATUS_COMMAND.replace("curl.exe", "curl"), shell=True,
                                input=big.encode(), capture_output=True)
    assert r.stdout.decode() == "Sonnet | 5h 10% | week 5%", r
    task.cancel()
asyncio.run(server_test())

assert call_watch.in_call(["MSTeams_8wekyb3d8bbwe"]) and call_watch.in_call(["C:#Users#a#AppData#Local#Microsoft#Teams#current#Teams.exe"])
assert not call_watch.in_call(["Microsoft.WindowsSoundRecorder_8wekyb3d8bbwe", "C:#Program Files#GorgiHelper#GorgiHelper.exe"])
print("ALL OK")
