"""Claude plan usage (5-hour and weekly %), for the Claude app on this PC.

The Claude desktop app has no status line, so usage can't come from there.
Instead, every few minutes:

1. Read the sign-in that Claude Code saved on this PC (~/.claude/.credentials.json,
   the PC app's account only - never the browser) and ask Anthropic for the usage,
   the same numbers as Settings -> Usage. This address isn't officially documented
   (tools like ccusage use it), so it may stop working one day. The sign-in is only
   read: it's sent to Anthropic and nowhere else, and never changed or renewed here.
2. If that doesn't work: add up the tokens in Claude Code's own logs on this PC
   (amounts, not percentages: the plan limits aren't published).

What happened last is written to claude-usage.json in the helper's settings folder
(without the sign-in), to make problems easy to find.
"""

import asyncio
import datetime as dt
import json
import logging
import os
import time
from pathlib import Path

log = logging.getLogger("gorgi.usage")

USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
EVERY = 180          # seconds between checks
BACK_OFF = 15 * 60   # after "too many requests" or a refused sign-in


def claude_dir():
    return Path(os.environ.get("CLAUDE_CONFIG_DIR") or (Path.home() / ".claude"))


def read_sign_in(path=None):
    """(access token, plan name) from Claude Code's saved sign-in, or (None, plan/None)."""
    path = Path(path) if path else claude_dir() / ".credentials.json"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None, None
    oauth = data.get("claudeAiOauth") if isinstance(data, dict) else None
    if not isinstance(oauth, dict):
        return None, None
    plan = str(oauth.get("subscriptionType") or "").strip().title() or None
    token = oauth.get("accessToken")
    expires = oauth.get("expiresAt") or 0
    if expires and expires / (1000 if expires > 1e12 else 1) < time.time() + 60:
        return None, plan  # expired: Claude renews it next time it's used
    return (token if isinstance(token, str) and token else None), plan


# ---------------------------------------------------------------- tokens from the logs


class LogCounter:
    """Adds up tokens in Claude Code's session logs (projects/*/*.jsonl)."""

    def __init__(self):
        self._files = {}  # path -> (size, mtime, [(time, tokens, id)])

    def _read(self, path):
        entries = []
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                for line in f:
                    if '"usage"' not in line:
                        continue
                    try:
                        obj = json.loads(line)
                    except ValueError:
                        continue
                    msg = obj.get("message") if isinstance(obj, dict) else None
                    usage = msg.get("usage") if isinstance(msg, dict) else None
                    if not isinstance(usage, dict):
                        continue
                    try:
                        when = dt.datetime.fromisoformat(str(obj.get("timestamp")).replace("Z", "+00:00")).timestamp()
                    except ValueError:
                        continue
                    tokens = sum(int(usage.get(k) or 0) for k in
                                 ("input_tokens", "output_tokens", "cache_creation_input_tokens"))
                    entries.append((when, tokens, msg.get("id") or obj.get("requestId") or obj.get("uuid")))
        except OSError:
            pass
        return entries

    def count(self, roots, now=None):
        """(tokens in the last 5 hours, in the last 7 days)"""
        now = now or time.time()
        week_ago = now - 7 * 86400
        seen = {}
        for root in roots:
            for path in Path(root).glob("*/*.jsonl"):
                try:
                    st = path.stat()
                except OSError:
                    continue
                if st.st_mtime < week_ago:
                    continue
                cached = self._files.get(path)
                if not cached or cached[0] != st.st_size or cached[1] != st.st_mtime:
                    cached = (st.st_size, st.st_mtime, self._read(path))
                    self._files[path] = cached
                for when, tokens, mid in cached[2]:
                    if when >= week_ago:
                        seen[mid or (path, when)] = (when, tokens)  # the same answer can be logged twice
        five = sum(t for w, t in seen.values() if w >= now - 5 * 3600)
        week = sum(t for w, t in seen.values())
        return five, week


# ---------------------------------------------------------------- the loop


class UsageFetcher:
    def __init__(self, watch, debug_file=None):
        self.watch = watch
        self.debug_file = debug_file
        self.counter = LogCounter()
        self.wait_until = 0.0

    def _note(self, **info):
        if not self.debug_file:
            return
        try:
            Path(self.debug_file).write_text(json.dumps({"time": time.strftime("%Y-%m-%d %H:%M:%S"), **info},
                                                        indent=2, default=str), encoding="utf-8")
        except OSError:
            pass

    async def ask_anthropic(self, client, token):
        """The usage, or None. Returns the reply as a dict."""
        r = await client.get(USAGE_URL, timeout=15, headers={
            "Authorization": f"Bearer {token}", "anthropic-beta": "oauth-2025-04-20",
            "Content-Type": "application/json", "User-Agent": "claude-code/2.1 (GorgiHelper)"})
        return r.status_code, (r.json() if r.headers.get("content-type", "").startswith("application/json") else {})

    async def check_once(self, client):
        token, plan = read_sign_in()
        if token and time.time() >= self.wait_until:
            try:
                status, data = await self.ask_anthropic(client, token)
            except Exception as e:
                status, data = 0, {"error": str(e)}
            five, week = (data.get("five_hour"), data.get("seven_day")) if isinstance(data, dict) else (None, None)
            p5 = (five or {}).get("utilization") if isinstance(five, dict) else None
            p7 = (week or {}).get("utilization") if isinstance(week, dict) else None
            self._note(source="anthropic", status=status, plan=plan,
                       reply={k: v for k, v in (data or {}).items() if k != "error"} if isinstance(data, dict) else {},
                       error=(data or {}).get("error") if isinstance(data, dict) else None)
            if status == 200 and (isinstance(p5, (int, float)) or isinstance(p7, (int, float))):
                self.watch.set_percent(p5, five.get("resets_at") if isinstance(five, dict) else None,
                                       p7, week.get("resets_at") if isinstance(week, dict) else None,
                                       "anthropic", plan=plan)
                return
            log.info("usage from Anthropic didn't work (%s), counting tokens instead", status)
            if status in (401, 403, 429) or status >= 500:
                self.wait_until = time.time() + BACK_OFF
        elif not token:
            self._note(source="logs", reason="no saved Claude sign-in found (or it has expired)", plan=plan)
        roots = {str(claude_dir() / "projects")} | set(self.watch.log_dirs)
        five_tokens, week_tokens = await asyncio.to_thread(self.counter.count, roots)
        if five_tokens or week_tokens or self.watch.usage is None:
            self.watch.set_tokens(five_tokens, week_tokens, plan=plan)

    async def run(self, stop_event):
        import httpx

        async with httpx.AsyncClient() as client:
            while not stop_event.is_set():
                try:
                    await self.check_once(client)
                except Exception as e:
                    log.warning("usage check failed: %s", e)
                try:
                    await asyncio.wait_for(stop_event.wait(), EVERY)
                except asyncio.TimeoutError:
                    pass
