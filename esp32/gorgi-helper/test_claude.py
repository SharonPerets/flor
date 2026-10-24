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
data = json.loads((d / "new" / "settings.json").read_text())
assert data["hooks"]["PreToolUse"][0]["matcher"] == "AskUserQuestion|ExitPlanMode"
assert data["hooks"]["PermissionRequest"][0]["matcher"] == "*"
# an older helper's connection (no PreToolUse / PermissionRequest yet) is upgraded quietly
old_hook = {"hooks": [{"type": "command", "command": cs.HOOK_COMMAND, "timeout": 5}]}
older = {"hooks": {e: [dict(old_hook, matcher="*") if e == "PostToolUse" else old_hook]
                   for e in ["SessionStart", "UserPromptSubmit", "PostToolUse", "Notification", "Stop", "SessionEnd"]},
         "statusLine": {"type": "command", "command": "my-status.sh"}}
(d / "old.json").write_text(json.dumps(older))
assert cs.status(d / "old.json")[0] is False
assert cs.upgrade(d / "old.json") is True and cs.status(d / "old.json") == (True, False, True)
assert cs.upgrade(d / "old.json") is False  # nothing more to add
assert json.loads((d / "old.json").read_text())["statusLine"]["command"] == "my-status.sh"
(d / "none.json").write_text(json.dumps({"model": "opus"}))
assert cs.upgrade(d / "none.json") is False and json.loads((d / "none.json").read_text()) == {"model": "opus"}

# ---- sessions
t = [1000.0]
alerts, changes = [], []
w = ClaudeWatch(on_change=lambda: changes.append(1), on_alert=lambda k, n, x: alerts.append((k, n, x)), clock=lambda: t[0])
ev = lambda name, **kw: w.hook({"hook_event_name": name, "session_id": "s1", "cwd": "C:\\Users\\sharon\\code\\flor", **kw})
ev("SessionStart"); ev("UserPromptSubmit"); t[0] += 5; ev("PostToolUse")
assert w.message()["sessions"][0] == {"name": "flor", "state": "working", "text": "", "since": 1000}
ev("Notification", message="Claude needs your permission to use Bash")
assert alerts[-1] == ("waiting", "flor", "Claude needs you in flor") and w.message()["sessions"][0]["text"] == "Needs your OK"
n = len(alerts); ev("PermissionRequest", tool_name="Bash")
assert len(alerts) == n  # the same wait: no second alert
t[0] += 3; ev("PostToolUse"); t[0] += 30; ev("Stop")
n = len(alerts); w.tick()
assert len(alerts) == n and w.message()["sessions"][0]["state"] == "done"  # not yet: Claude may carry on
t[0] += 5; ev("PostToolUse"); t[0] += 20; w.tick()
assert len(alerts) == n  # it carried on: that Stop was only a step
t[0] += 10; ev("Stop", last_assistant_message="All done, the tests pass."); t[0] += 16; w.tick()
assert alerts[-1] == ("done", "flor", "Claude finished in flor") and len(alerts) == n + 1
w.tick(); assert len(alerts) == n + 1  # only once
n = len(alerts); ev("Notification", message="Claude is waiting for your input")
assert len(alerts) == n and w.message()["sessions"][0]["state"] == "done"  # no second alert
ev("UserPromptSubmit"); t[0] += 2; ev("Stop"); t[0] += 16; w.tick()
assert len(alerts) == n  # a quick answer: no alert
# a stop with a question (even a quick one) = needs you; read from the session log
log_file = d / "session.jsonl"
log_file.write_text("\n".join(json.dumps(x) for x in [
    {"type": "user", "message": {"role": "user", "content": "fix it"}},
    {"type": "assistant", "message": {"content": [{"type": "text", "text": "Here's my plan:\n- step one"}]}},
    {"type": "assistant", "message": {"content": [{"type": "tool_use", "name": "Read"}]}},
    {"type": "user", "message": {"content": [{"type": "tool_result", "content": "..."}]}},
    {"type": "assistant", "message": {"content": [{"type": "text", "text": "Plan:\n1. fix\n\n**Should I go ahead?**\n\nThe log helps."}]}},
]) + "\n")
ev("UserPromptSubmit"); t[0] += 2; ev("Stop", transcript_path=str(log_file)); t[0] += 16; w.tick()
assert alerts[-1] == ("waiting", "flor", "Claude needs you in flor") and len(alerts) == n + 1
assert w.message()["sessions"][0]["text"] == "Asked you something"
# asking with buttons / a plan to approve: needs you at once
n = len(alerts); ev("UserPromptSubmit"); ev("PreToolUse", tool_name="AskUserQuestion")
assert len(alerts) == n + 1 and w.message()["sessions"][0]["text"] == "Has a question for you"
ev("PostToolUse", tool_name="AskUserQuestion"); assert w.message()["sessions"][0]["state"] == "working"
ev("PreToolUse", tool_name="ExitPlanMode"); assert len(alerts) == n + 2
ev("PreToolUse", tool_name="Bash"); assert w.message()["sessions"][0]["state"] == "working"
from claude_watch import _ends_with_question, _last_text
assert _ends_with_question("Done.\nWant me to merge it?)") and not _ends_with_question("Fixed the ? bug.\nAll good.")
assert _last_text(d / "missing.jsonl") == ""
t[0] += 1; w.hook({"hook_event_name": "UserPromptSubmit", "session_id": "s2", "cwd": "/home/x/api-server"})
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
# ---- usage: sign-ins (desktop app, Claude Code file), Anthropic's answer, no token counting
import base64 as _b64
import time as _t
import usage_fetch as uf
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
cred = d / ".credentials.json"
cred.write_text(json.dumps({"claudeAiOauth": {"accessToken": "tok", "expiresAt": int((_t.time() + 3600) * 1000),
                                              "subscriptionType": "max"}}))
assert uf.read_sign_in(cred) == ("tok", "Max")
cred.write_text(json.dumps({"claudeAiOauth": {"accessToken": "tok", "expiresAt": int((_t.time() - 10) * 1000),
                                              "subscriptionType": "pro"}}))
assert uf.read_sign_in(cred) == (None, "Pro")
cred.write_text(json.dumps({"claudeAiOauth": {"accessToken": "", "expiresAt": 0, "subscriptionType": "max"}}))
assert uf.read_sign_in(cred) == (None, "Max")  # blanked by a failed renewal
assert uf.read_sign_in(d / "missing.json") == (None, None)

# the desktop app's token cache, locked like the app does it (a fake Windows lock: none)
app = d / "Claude"
app.mkdir()
key = AESGCM.generate_key(bit_length=256)
(app / "Local State").write_text(json.dumps({"os_crypt": {"encrypted_key": _b64.b64encode(b"DPAPI" + key).decode()}}))
def lock(obj):
    nonce = b"0123456789ab"
    return _b64.b64encode(b"v10" + nonce + AESGCM(key).encrypt(nonce, json.dumps(obj).encode(), None)).decode()
soon, later = int((_t.time() + 3600) * 1000), int((_t.time() + 7200) * 1000)
base = "inst:user:https://api.anthropic.com:"
cache = {base + "user:inference": {"token": "infer-only", "expiresAt": later},
         base + "user:inference user:profile user:sessions:claude_code": {"token": "good", "expiresAt": soon},
         base + "user:profile user:inference": {"token": "old", "expiresAt": int((_t.time() - 50) * 1000)},
         base + "user:profile": {"token": "  ", "expiresAt": later}}
(app / "config.json").write_text(json.dumps({"locale": "en", "oauth:tokenCacheV2": lock(cache)}))
same = lambda b: b
assert uf.read_desktop_sign_in([app / "config.json"], same) == ("good", "desktop app (oauth:tokenCacheV2)")
(app / "config.json").write_text(json.dumps({"oauth:tokenCacheV2": "djEwYnJva2Vu", "oauth:tokenCache": lock(cache)}))
assert uf.read_desktop_sign_in([app / "config.json"], same) == ("good", "desktop app (oauth:tokenCache)")
(app / "config.json").write_text(json.dumps({"oauth:tokenCacheV2": lock({base + "user:inference": {"token": "x"}})}))
token, why = uf.read_desktop_sign_in([app / "config.json"], same)
assert token is None and "no fresh sign-in" in why, why
assert uf.read_desktop_sign_in([d / "nope" / "config.json"], same) == (None, "Claude desktop app not found")
def broken_lock(_b):
    raise OSError("Windows couldn't unlock the key")
(app / "config.json").write_text(json.dumps({"oauth:tokenCacheV2": lock(cache)}))
assert "can't unlock" in uf.read_desktop_sign_in([app / "config.json"], broken_lock)[1]
assert uf._percents({"five_hour": {"utilization": 0.41}, "seven_day": {"utilization": 0.125}})[0] == 41.0
assert uf._percents({"five_hour": {"utilization": 41}, "seven_day": {"utilization": 0}})[0] == 41
assert uf._retry_after("0") == 300 and uf._retry_after("99999") == 3600 and uf._retry_after(None) == 300

class FakeWatch(ClaudeWatch):
    pass
uf.claude_dir = lambda: d
real_desktop = uf.read_desktop_sign_in
uf.read_desktop_sign_in = lambda: real_desktop([app / "config.json"], same)
cred.write_text(json.dumps({"claudeAiOauth": {"accessToken": "file-tok", "expiresAt": soon, "subscriptionType": "max"}}))
asked = []
async def ok(client, token):
    asked.append(token)
    return 200, {"five_hour": {"utilization": 41.0, "resets_at": "2026-10-24T03:00:00+00:00"},
                 "seven_day": {"utilization": 12.5, "resets_at": "2026-10-29T09:00:00+00:00"}}, None
fw = FakeWatch()
f = uf.UsageFetcher(fw, d / "usage-debug.json")
f.ask_anthropic = ok
asyncio.run(f.check_once(None))
u = fw.message()["usage"]
assert asked == ["good"]  # the desktop app's sign-in comes first
assert u["mode"] == "percent" and u["five"] == 41.0 and u["plan"] == "Max" and u["week_reset"] > 0, u
debug = (d / "usage-debug.json").read_text()
assert "good" not in debug and "file-tok" not in debug and "desktop app" in debug
# no desktop app: the fresh file sign-in is used
uf.read_desktop_sign_in = lambda: (None, "Claude desktop app not found")
f2 = uf.UsageFetcher(FakeWatch()); f2.ask_anthropic = ok
asyncio.run(f2.check_once(None)); assert asked[-1] == "file-tok"
# nothing fresh: Anthropic isn't asked at all, and there's no usage (no token counting any more)
cred.write_text(json.dumps({"claudeAiOauth": {"accessToken": "file-tok", "expiresAt": 1, "subscriptionType": "max"}}))
fw3 = FakeWatch(); f3 = uf.UsageFetcher(fw3, d / "usage-debug.json"); f3.ask_anthropic = ok
n = len(asked); asyncio.run(f3.check_once(None))
assert len(asked) == n and "usage" not in fw3.message() and "no fresh sign-in" in (d / "usage-debug.json").read_text()
# refused / too many requests: waits before asking again, keeps the last numbers
cred.write_text(json.dumps({"claudeAiOauth": {"accessToken": "file-tok", "expiresAt": soon, "subscriptionType": "max"}}))
async def busy(client, token):
    asked.append(token)
    return 429, {"error": {"type": "rate_limit_error"}}, "0"
f.ask_anthropic = busy
f.wait_until = 0
asyncio.run(f.check_once(None)); n = len(asked)
assert f.wait_until > _t.time() + 290 and fw.message()["usage"]["five"] == 41.0
asyncio.run(f.check_once(None)); assert len(asked) == n  # still waiting
async def refused(client, token):
    return 401, {"error": {"type": "authentication_error"}}, None
f.ask_anthropic = refused; f.wait_until = 0
asyncio.run(f.check_once(None)); assert f.wait_until > _t.time() + 290
uf.read_desktop_sign_in = real_desktop
print(fw.summary())
print("ALL OK")
