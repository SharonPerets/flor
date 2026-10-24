"""Claude plan usage (5-hour and weekly %), for the Claude app on this PC.

The Claude desktop app has no status line, so usage can't come from there.
Instead, every few minutes we ask Anthropic for the usage (the same numbers as
Settings -> Usage), with a sign-in that is already on this PC:

1. The Claude desktop app's own Claude Code sign-in. The app keeps it in its
   config.json, locked with Windows' per-user protection (DPAPI + AES-GCM, the
   same way Chrome-based apps store secrets), and renews it itself. We only
   read it, keep it in memory, and send it to Anthropic and nowhere else.
2. Claude Code's saved sign-in (~/.claude/.credentials.json), if it's still
   fresh. The desktop app doesn't keep this file up to date, so it's often
   expired.

We never renew a sign-in ourselves: sign-ins can be renewed only once, so that
would sign Claude Code out. If there's no fresh sign-in, or Anthropic says no,
there's simply no usage (the last numbers stay, with their age).

The usage address isn't officially documented (many tools use it), so it may
stop working one day. It is also strict about how often it's asked: we ask at
most every 5 minutes and wait longer when it says "too many requests".

What happened last is written to claude-usage.json in the helper's settings
folder (never the sign-in itself), to make problems easy to find.
"""

import asyncio
import base64
import json
import logging
import os
import sys
import time
from pathlib import Path

log = logging.getLogger("gorgi.usage")

USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
USER_AGENT = "claude-cli/2.1.300 (external, cli)"  # the address is stricter with other names
EVERY = 300             # seconds between checks
AFTER_401 = 300         # a refused sign-in: wait, so it doesn't turn into "too many requests"
MIN_BACK_OFF = 300      # "too many requests" without a usable Retry-After
MAX_BACK_OFF = 3600
STORE_PACKAGE = "Claude_pzs8sxrjxfjjc"  # the Microsoft Store version of the Claude app
CACHE_KEYS = ("oauth:tokenCacheV2", "oauth:tokenCache")  # newest first


def claude_dir():
    return Path(os.environ.get("CLAUDE_CONFIG_DIR") or (Path.home() / ".claude"))


def _ms(value):
    """expiresAt as seconds (it's milliseconds in these files)."""
    try:
        value = float(value)
    except (TypeError, ValueError):
        return 0.0
    return value / 1000 if value > 1e11 else value


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
    if _ms(oauth.get("expiresAt")) < time.time() + 60:
        return None, plan  # expired (or blanked): only Claude Code itself may renew it
    return (token if isinstance(token, str) and token.strip() else None), plan


# ---------------------------------------------------------------- the desktop app's sign-in


def desktop_config_paths():
    """Where the Claude desktop app may keep its config.json (normal install, then Store installs)."""
    paths = []
    appdata, local = os.environ.get("APPDATA"), os.environ.get("LOCALAPPDATA")
    if appdata:
        paths.append(Path(appdata) / "Claude" / "config.json")
    if local:
        packages = Path(local) / "Packages"
        try:
            found = sorted(p for p in packages.glob("Claude_*") if p.is_dir())
        except OSError:
            found = []
        found.sort(key=lambda p: p.name != STORE_PACKAGE)
        paths += [p / "LocalCache" / "Roaming" / "Claude" / "config.json" for p in found]
    return paths


def _dpapi_unprotect(data):
    """Windows' per-user decryption (CryptUnprotectData)."""
    import ctypes
    from ctypes import wintypes

    class Blob(ctypes.Structure):
        _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_char))]

    buf = ctypes.create_string_buffer(data, len(data))
    blob_in = Blob(len(data), ctypes.cast(buf, ctypes.POINTER(ctypes.c_char)))
    blob_out = Blob()
    if not ctypes.windll.crypt32.CryptUnprotectData(ctypes.byref(blob_in), None, None, None, None, 0,
                                                     ctypes.byref(blob_out)):
        raise OSError("Windows couldn't unlock the key")
    try:
        return ctypes.string_at(blob_out.pbData, blob_out.cbData)
    finally:
        ctypes.windll.kernel32.LocalFree(blob_out.pbData)


def _app_key(local_state_path, unprotect):
    data = json.loads(Path(local_state_path).read_text(encoding="utf-8"))
    wrapped = base64.b64decode(data["os_crypt"]["encrypted_key"])
    if not wrapped.startswith(b"DPAPI"):
        raise ValueError("unknown key format")
    return unprotect(wrapped[5:])


def _decrypt(value, key):
    """A "v10" value: 12-byte nonce, then the encrypted text with its 16-byte tag (AES-256-GCM)."""
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM

    raw = base64.b64decode(value)
    if not raw.startswith(b"v10") or len(raw) < 3 + 12 + 16:
        raise ValueError("unknown value format")
    return AESGCM(key).decrypt(raw[3:15], raw[15:], None).decode("utf-8")


def _scopes(entry_key):
    """'<install>:<user>:<base url>:user:inference user:profile ...' -> {"user:inference", "user:profile", ...}"""
    words = str(entry_key).split()
    if not words:
        return set()
    first = words[0].split(":")
    return {":".join(first[-2:])} | set(words[1:])


def pick_token(cache, now=None):
    """The best fresh token in a decrypted token cache (it needs the user:profile permission), or None."""
    now = now or time.time()
    best = None
    for key, entry in (cache.items() if isinstance(cache, dict) else []):
        if not isinstance(entry, dict):
            continue
        token = entry.get("token")
        if not isinstance(token, str) or not token.strip():
            continue
        scopes = _scopes(key)
        if "user:profile" not in scopes:
            continue  # can't read usage
        expires = _ms(entry.get("expiresAt"))
        if expires and expires < now + 60:
            continue  # expired: the app renews it next time it runs
        rank = ("user:inference" in scopes, expires or float("inf"))
        if best is None or rank > best[0]:
            best = (rank, token)
    return best[1] if best else None


def read_desktop_sign_in(paths=None, unprotect=None):
    """(token or None, what happened) from the Claude desktop app's config."""
    if paths is None and sys.platform != "win32":
        return None, "not Windows"
    unprotect = unprotect or _dpapi_unprotect
    notes = []
    for path in (desktop_config_paths() if paths is None else paths):
        try:
            config = json.loads(Path(path).read_text(encoding="utf-8"))
        except FileNotFoundError:
            continue
        except (OSError, ValueError) as e:
            notes.append(f"{path}: can't read ({e})")
            continue
        values = [(k, config.get(k)) for k in CACHE_KEYS if isinstance(config.get(k), str) and config.get(k)]
        if not values:
            notes.append(f"{path}: no sign-in saved")
            continue
        try:
            key = _app_key(Path(path).with_name("Local State"), unprotect)
        except Exception as e:
            notes.append(f"{path}: can't unlock ({type(e).__name__}: {e})")
            continue
        for name, value in values:
            try:
                token = pick_token(json.loads(_decrypt(value, key)))
            except Exception as e:
                notes.append(f"{path} {name}: can't read ({type(e).__name__})")
                continue
            if token:
                return token, f"desktop app ({name})"
            notes.append(f"{path} {name}: no fresh sign-in with usage permission")
    return None, "; ".join(notes) or "Claude desktop app not found"


# ---------------------------------------------------------------- the loop


def _percents(data):
    """(five, five_reset, week, week_reset) from Anthropic's answer, in 0..100."""
    five, week = data.get("five_hour"), data.get("seven_day")
    p5 = five.get("utilization") if isinstance(five, dict) else None
    p7 = week.get("utilization") if isinstance(week, dict) else None
    nums = [p for p in (p5, p7) if isinstance(p, (int, float))]
    if not nums:
        return None
    # some answers give 0..1 instead of 0..100
    if all(p <= 1.0 for p in nums) and any(p != int(p) for p in nums):
        p5, p7 = (p * 100 if isinstance(p, (int, float)) else p for p in (p5, p7))
    return (p5 if isinstance(p5, (int, float)) else None, five.get("resets_at") if isinstance(five, dict) else None,
            p7 if isinstance(p7, (int, float)) else None, week.get("resets_at") if isinstance(week, dict) else None)


def _retry_after(value):
    try:
        seconds = float(value)
    except (TypeError, ValueError):
        seconds = 0
    return min(MAX_BACK_OFF, max(MIN_BACK_OFF, seconds))


class UsageFetcher:
    def __init__(self, watch, debug_file=None):
        self.watch = watch
        self.debug_file = debug_file
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
        """(status, reply as a dict, Retry-After header)"""
        r = await client.get(USAGE_URL, timeout=15, headers={
            "Authorization": f"Bearer {token}", "anthropic-beta": "oauth-2025-04-20",
            "Content-Type": "application/json", "User-Agent": USER_AGENT})
        data = r.json() if r.headers.get("content-type", "").startswith("application/json") else {}
        return r.status_code, data, r.headers.get("retry-after")

    async def check_once(self, client):
        if time.time() < self.wait_until:
            return
        token, source = read_desktop_sign_in()
        file_token, plan = read_sign_in()
        if not token and file_token:
            token, source = file_token, "Claude Code sign-in file"
        if not token:
            self._note(result="no fresh sign-in found, so no usage", desktop_app=source, plan=plan)
            return
        try:
            status, data, retry = await self.ask_anthropic(client, token)
        except Exception as e:
            status, data, retry = 0, {}, None
            self._note(source=source, result=f"couldn't ask Anthropic: {e}", plan=plan)
            self.wait_until = time.time() + 60
            return
        data = data if isinstance(data, dict) else {}
        got = _percents(data) if status == 200 else None
        self._note(source=source, status=status, plan=plan,
                   reply={k: v for k, v in data.items() if k in ("five_hour", "seven_day", "error")})
        if got:
            p5, r5, p7, r7 = got
            self.watch.set_percent(p5, r5, p7, r7, source, plan=plan)
            return
        log.info("usage from Anthropic didn't work (%s, %s)", status, source)
        if status == 429:
            self.wait_until = time.time() + _retry_after(retry)
        elif status in (401, 403):
            self.wait_until = time.time() + AFTER_401
        elif status == 0 or status >= 500:
            self.wait_until = time.time() + MIN_BACK_OFF

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
