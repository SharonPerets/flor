"""Keeps track of Claude Code on this PC, for Gorgi's Claude screen.

Claude Code sends its events here through the hooks that claude_setup.py adds
(a tiny web server on 127.0.0.1:47811, reachable only from this PC):

    POST /claude/hook     a hook event (session started, prompt, tool used, needs you, finished...)
    POST /claude/status   Claude Code's status line data, including 5-hour / weekly usage;
                          the answer is shown as Claude Code's status line

Each session is "working", "waiting" (for your answer or OK) or "done".
Gorgi gets an alert when a session needs you, or finishes after working a while.

Claude Code says "Stop" at the end of every reply, not only when the whole task
is done (it also stops to report or to ask something, and may carry on by itself
when a background job ends). So a "finished" alert waits STOP_QUIET seconds:
if Claude works again in that time, there's no alert. If its last message ends
with a question, the alert is "needs you" instead.
"""

import asyncio
import datetime as dt
import json
import logging
import time
from pathlib import PurePath

log = logging.getLogger("gorgi.claude")

PORT = 47811
MAX_SESSIONS = 5
DONE_ALERT_AFTER = 20      # seconds of work before "finished" is worth an alert
FORGET_AFTER = 3 * 3600    # done / idle sessions disappear after this
STOP_QUIET = 15            # seconds of quiet after "Stop" before it counts as finished
ASK_TOOLS = {"AskUserQuestion": "Has a question for you", "ExitPlanMode": "Plan ready for your OK"}


def _project(cwd):
    if not cwd:
        return "Claude"
    name = PurePath(str(cwd).replace("\\", "/")).name
    return name or str(cwd)


def _epoch(value):
    """resets_at as seconds since 1970 (it may be a number or an ISO date)."""
    if value is None:
        return 0
    if isinstance(value, (int, float)):
        return int(value / 1000) if value > 1e12 else int(value)
    try:
        return int(dt.datetime.fromisoformat(str(value).replace("Z", "+00:00")).timestamp())
    except ValueError:
        return 0


def _ends_with_question(text):
    """Does Claude's message end by asking something? (one of its last 3 lines ends with "?")"""
    lines = [ln.strip().rstrip("*_`'\")” ").strip() for ln in str(text or "").splitlines()]
    lines = [ln for ln in lines if ln]
    return any(ln.endswith("?") for ln in lines[-3:])


def _last_text(transcript_path, limit=256 * 1024):
    """The text of Claude's last message in a session log (a .jsonl file), or ""."""
    try:
        with open(transcript_path, "rb") as f:
            f.seek(0, 2)
            size = f.tell()
            f.seek(max(0, size - limit))
            tail = f.read().decode("utf-8", "replace")
    except (OSError, TypeError, ValueError):
        return ""
    for line in reversed(tail.splitlines()):
        try:
            entry = json.loads(line)
        except ValueError:
            continue
        if not isinstance(entry, dict):
            continue
        if entry.get("type") == "user" and not entry.get("isMeta"):
            content = (entry.get("message") or {}).get("content")
            if isinstance(content, str) or (isinstance(content, list) and any(
                    isinstance(c, dict) and c.get("type") == "text" for c in content)):
                return ""  # the user spoke after Claude's last text
        if entry.get("type") != "assistant":
            continue
        content = (entry.get("message") or {}).get("content")
        if isinstance(content, str) and content.strip():
            return content
        if isinstance(content, list):
            texts = [c.get("text", "") for c in content if isinstance(c, dict) and c.get("type") == "text"]
            if any(t.strip() for t in texts):
                return "\n".join(texts)
    return ""


def _percent(window):
    if not isinstance(window, dict):
        return None
    for key in ("used_percentage", "utilization", "used_percent", "percent"):
        v = window.get(key)
        if isinstance(v, (int, float)):
            return float(v)
    return None


class ClaudeWatch:
    def __init__(self, on_change=None, on_alert=None, clock=time.time):
        self.on_change = on_change or (lambda: None)        # the list or usage changed
        self.on_alert = on_alert or (lambda kind, name, text: None)
        self.clock = clock
        self.sessions = {}   # id -> {"name", "state", "text", "since", "work_start", "seen"}
        self.usage = None    # {"five", "five_reset", "week", "week_reset", "at"}
        self.model = ""
        self.hooked = False  # we've heard from Claude Code
        self.log_dirs = set()  # folders with Claude Code's session logs

    # ------------------------------------------------------------ events

    def _session(self, data):
        sid = str(data.get("session_id") or "default")
        s = self.sessions.get(sid)
        created = s is None
        if created:
            s = {"name": _project(data.get("cwd")), "state": "idle", "text": "", "since": self.clock(),
                 "work_start": None, "seen": self.clock(), "stop": None}
            self.sessions[sid] = s
        s["seen"] = self.clock()
        if data.get("cwd"):
            s["name"] = _project(data.get("cwd"))
        return s, created

    def _set(self, s, state, text=""):
        if s["state"] == state and s["text"] == text:
            return False
        s["state"], s["text"], s["since"] = state, text, self.clock()
        return True

    def _need(self, s, text):
        """The session needs you. Alerts only when it starts waiting (not again for the same wait)."""
        was = s["state"]
        changed = self._set(s, "waiting", text)
        if changed and was != "waiting":
            self.on_alert("waiting", s["name"], f"Claude needs you in {s['name']}")
        return changed

    def tick(self):
        """Called every second or two: turns a quiet "Stop" into "finished" or "needs you"."""
        now = self.clock()
        changed = False
        for s in list(self.sessions.values()):
            stop = s.get("stop")
            if not stop or now - stop["at"] < STOP_QUIET:
                continue
            s["stop"] = None
            worked = stop["at"] - s["work_start"] if s["work_start"] else 0
            s["work_start"] = None
            last = stop["last"] if isinstance(stop["last"], str) else _last_text(stop["transcript"])
            if s["state"] != "done":
                continue
            if _ends_with_question(last):
                changed = self._need(s, "Asked you something") or changed
            elif worked >= DONE_ALERT_AFTER:
                self.on_alert("done", s["name"], f"Claude finished in {s['name']}")
        if changed:
            self.on_change()

    def hook(self, data):
        """A hook event from Claude Code."""
        if not isinstance(data, dict):
            return
        self.hooked = True
        if data.get("transcript_path"):  # where this Claude keeps its logs (for counting tokens)
            self.log_dirs.add(str(PurePath(str(data["transcript_path"])).parent.parent))
        event = data.get("hook_event_name") or ""
        changed = False
        if event == "SessionEnd":
            changed = self.sessions.pop(str(data.get("session_id") or "default"), None) is not None
        else:
            s, created = self._session(data)
            if event == "SessionStart":
                changed = created
            elif event == "UserPromptSubmit":
                s["stop"] = None
                s["work_start"] = self.clock()
                changed = self._set(s, "working")
            elif event == "PreToolUse" and data.get("tool_name") in ASK_TOOLS:
                s["stop"] = None
                changed = self._need(s, ASK_TOOLS[data["tool_name"]])
            elif event == "PermissionRequest":
                s["stop"] = None
                changed = self._need(s, "Needs your OK")
            elif event in ("PostToolUse", "PreToolUse"):
                s["stop"] = None  # still working: that "Stop" wasn't the end
                if s["state"] != "working":
                    if s["work_start"] is None:
                        s["work_start"] = self.clock()
                    changed = self._set(s, "working")
            elif event == "Notification":
                message = str(data.get("message") or "")
                low = message.lower()
                if "permission" in low:
                    text = "Needs your OK"
                elif "waiting for your input" in low:
                    text = "Waiting for your answer"
                else:
                    text = message[:40] or "Needs you"
                # the "still waiting" reminder after a finished answer: it's already done
                if not (s["state"] == "done" and "waiting for your input" in low):
                    s["stop"] = None
                    changed = self._need(s, text)
            elif event == "Stop":
                changed = self._set(s, "done")
                # decided later (tick), once Claude has been quiet for a while
                s["stop"] = {"at": self.clock(), "transcript": data.get("transcript_path"),
                             "last": data.get("last_assistant_message")}
        log.info("claude hook: %s (%s)", event, data.get("cwd") or "")
        if changed:
            self.on_change()

    def status(self, data):
        """Status line data from Claude Code. Returns the text to show as its status line."""
        if not isinstance(data, dict):
            return ""
        self.hooked = True
        model = data.get("model")
        if isinstance(model, dict):
            self.model = str(model.get("display_name") or model.get("id") or "")
        limits = data.get("rate_limits")
        if isinstance(limits, dict):
            five, week = limits.get("five_hour"), limits.get("seven_day")
            p5, p7 = _percent(five), _percent(week)
            if p5 is not None or p7 is not None:
                self.set_percent(p5, (five or {}).get("resets_at"), p7, (week or {}).get("resets_at"), "status line")
        return self.status_line()

    # ------------------------------------------------------------ usage (from the status line, Anthropic, or logs)

    def set_percent(self, p5, reset5, p7, reset7, source, plan=None):
        """Plan usage in percent (the best kind)."""
        old = self.usage or {}
        self.usage = {
            "mode": "percent",
            "five": round(p5 if p5 is not None else old.get("five", 0), 1),
            "five_reset": _epoch(reset5),
            "week": round(p7 if p7 is not None else old.get("week", 0), 1),
            "week_reset": _epoch(reset7),
            "at": int(self.clock()),
            "plan": plan or old.get("plan", ""),
            "source": source,
        }
        if (old.get("mode") != "percent" or round(old.get("five", -1)) != round(self.usage["five"])
                or round(old.get("week", -1)) != round(self.usage["week"]) or self.clock() - old.get("at", 0) > 300
                or old.get("plan") != self.usage["plan"]):
            self.on_change()

    def set_tokens(self, five_tokens, week_tokens, plan=None):
        """Only token amounts from the logs (used when percentages can't be had)."""
        old = self.usage or {}
        if old.get("mode") == "percent" and self.clock() - old.get("at", 0) < 15 * 60:
            return  # recent percentages are better
        self.usage = {"mode": "tokens", "five_tokens": int(five_tokens), "week_tokens": int(week_tokens),
                      "at": int(self.clock()), "plan": plan or old.get("plan", ""), "source": "logs"}
        if (old.get("mode") != "tokens" or old.get("five_tokens") != self.usage["five_tokens"]
                or old.get("week_tokens") != self.usage["week_tokens"]):
            self.on_change()

    def status_line(self):
        parts = [self.model] if self.model else []
        if self.usage and self.usage.get("mode") == "percent":
            parts.append(f"5h {self.usage['five']:.0f}%")
            parts.append(f"week {self.usage['week']:.0f}%")
        return " | ".join(parts) or "Gorgi"

    # ------------------------------------------------------------ for Gorgi and the AI

    def _visible(self):
        now = self.clock()
        for sid in [k for k, s in self.sessions.items()
                    if s["state"] in ("done", "idle") and now - s["since"] > FORGET_AFTER]:
            del self.sessions[sid]
        order = {"waiting": 0, "working": 1, "done": 2, "idle": 3}
        return sorted(self.sessions.values(), key=lambda s: (order.get(s["state"], 4), -s["since"]))[:MAX_SESSIONS]

    def message(self, hooked=None):
        """The "claude" message for Gorgi."""
        msg = {"type": "claude", "hooked": bool(self.hooked if hooked is None else hooked),
               "sessions": [{"name": s["name"][:27], "state": s["state"], "text": s["text"][:47],
                             "since": int(s["since"])} for s in self._visible()]}
        if self.usage:
            msg["usage"] = {k: v for k, v in self.usage.items() if k != "source"}
        return msg

    def summary(self):
        """A few lines for the AI."""
        lines = []
        sessions = self._visible()
        if sessions:
            parts = []
            for s in sessions:
                mins = int((self.clock() - s["since"]) // 60)
                what = {"working": f"working ({mins} min)", "waiting": f"waiting for you ({s['text']})",
                        "done": f"done {mins} min ago"}.get(s["state"], "idle")
                parts.append(f"{s['name']}: {what}")
            lines.append("Claude Code sessions on the PC: " + "; ".join(parts) + ".")
        u = self.usage
        if u and u.get("mode") == "percent":
            lines.append(f"Claude plan usage: {u['five']:.0f}% of the 5-hour limit, {u['week']:.0f}% of the weekly "
                         "limit.")
        elif u:
            lines.append(f"Claude Code used about {u['five_tokens']:,} tokens in the last 5 hours and "
                         f"{u['week_tokens']:,} in the last 7 days (plan percentages aren't available).")
        return "\n".join(lines)

    # ------------------------------------------------------------ the little web server

    async def serve(self, stop_event=None):
        """Listens until stop_event is set (it must stop, or the helper can't quit to update itself)."""
        try:
            server = await asyncio.start_server(self._client, "127.0.0.1", PORT)
        except OSError as e:
            log.warning("can't listen for Claude Code on port %d: %s", PORT, e)
            return
        log.info("listening for Claude Code on 127.0.0.1:%d", PORT)
        stop_event = stop_event or asyncio.Event()
        async with server:
            while not stop_event.is_set():
                try:
                    await asyncio.wait_for(stop_event.wait(), 1)
                except asyncio.TimeoutError:
                    pass
                try:
                    self.tick()
                except Exception as e:
                    log.warning("claude tick failed: %s", e)
        log.info("stopped listening for Claude Code")

    async def _client(self, reader, writer):
        try:
            head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 5)
            lines = head.decode("latin-1").split("\r\n")
            method, path = (lines[0].split(" ") + ["", ""])[:2]
            headers = {k.strip().lower(): v.strip() for k, _, v in (h.partition(":") for h in lines[1:] if h)}
            if headers.get("expect", "").lower() == "100-continue":
                writer.write(b"HTTP/1.1 100 Continue\r\n\r\n")
            length = min(int(headers.get("content-length") or 0), 1_000_000)
            body = await asyncio.wait_for(reader.readexactly(length), 5) if length else b""
            reply = self.handle(method, path, body)
            data = reply.encode("utf-8")
            writer.write(b"HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
                         + f"Content-Length: {len(data)}\r\nConnection: close\r\n\r\n".encode() + data)
            await writer.drain()
        except Exception as e:  # a broken request: just drop it
            log.debug("claude request failed: %s", e)
        finally:
            writer.close()

    def handle(self, method, path, body):
        if method != "POST":
            return "Gorgi Helper"
        try:
            data = json.loads(body.decode("utf-8") or "{}")
        except ValueError:
            data = {}
        if path.startswith("/claude/hook"):
            self.hook(data)
            return ""
        if path.startswith("/claude/status"):
            return self.status(data)
        return ""
