"""Gorgi Helper - connects your Windows PC to Gorgi.

Finds Gorgi on your Wi-Fi, sends it the song that's playing, the volume and
the time, and carries out Gorgi's media buttons (play/pause, next, previous,
volume). When you tap Gorgi's face it listens to the PC microphone, asks the AI
and speaks the answer. Lives as a small icon in the taskbar tray.

Run:  python gorgi_helper.py            (normal, with tray icon)
      python gorgi_helper.py --no-tray  (console only, for testing)
      python gorgi_helper.py --address 192.168.1.50   (skip auto-discovery)
"""

import argparse
import asyncio
import json
import logging
import os
import platform
import secrets
import socket
import sys
import threading
import time
from logging.handlers import RotatingFileHandler
from pathlib import Path

# Whisper and other libraries each bring their own copy of Intel's OpenMP;
# without this Windows aborts the app when the second copy loads.
os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")

import httpx  # noqa: E402
import websockets  # noqa: E402

import album_art  # noqa: E402
import lang  # noqa: E402
import call_watch  # noqa: E402
import claude_setup  # noqa: E402
from claude_watch import ClaudeWatch  # noqa: E402
from usage_fetch import UsageFetcher  # noqa: E402
from timekeeper import TimeKeeper  # noqa: E402
from todolist import TodoKeeper  # noqa: E402
import updater  # noqa: E402
import weather  # noqa: E402

APP_NAME = "Gorgi Helper"
VERSION = "4.8.4"  # must match esp32/VERSION
PORT = 81
SERVICE_TYPE = "_gorgi._tcp.local."
SERVICE_NAME = "gorgi._gorgi._tcp.local."
POLL_SECONDS = 1.0
IS_WINDOWS = sys.platform == "win32"

log = logging.getLogger("gorgi")


# ---------------------------------------------------------------- settings


def data_dir():
    base = os.environ.get("APPDATA") if IS_WINDOWS else os.path.expanduser("~/.config")
    path = Path(base or ".") / "GorgiHelper"
    path.mkdir(parents=True, exist_ok=True)
    return path


DEFAULT_CONFIG = {
    "gorgi_address": "",          # leave empty to find Gorgi automatically
    "ollama_api_key": "",
    "ollama_url": "",             # empty = Ollama Cloud; or e.g. http://localhost:11434
    "model": "gpt-oss:20b",
    "voice": "en-US-AnaNeural",
    "whisper_model": "base.en",   # "small.en" understands better but is slower
    "language": "en",             # "en" or "he": the language you spoke last (Gorgi answers in it)
    "voice_he": "he-IL-HilaNeural",
    "hearing": "groq",            # cloud hearing, Hebrew + English: "groq" or "openai" (needs its key below)
    "groq_api_key": "",
    "openai_api_key": "",
}


def hebrew_alert(text):
    """Gorgi's alert sentences (made in English on Gorgi) said in Hebrew."""
    import re

    m = re.match(r"^Ding! Your (.+?)(?: timer)? is done!$", text)
    if m:
        label = m.group(1)
        n = re.fullmatch(r"(\d+) (second|minute|hour)", label)
        if n:
            unit = {"second": "שניות", "minute": "דקות", "hour": "שעות"}[n.group(2)]
            if n.group(1) == "1":
                unit = {"second": "שנייה", "minute": "דקה", "hour": "שעה"}[n.group(2)]
            return f"דינג! הטיימר של {n.group(1)} {unit} נגמר!"
        return f"דינג! הטיימר של {label} נגמר!"
    m = re.match(r"^Hey! Reminder: (.+)$", text)
    if m:
        return f"היי! תזכורת: {m.group(1)}"
    if text.startswith(("Focus session done", "Done! ", "Focus time is over")):
        n = re.search(r"\b(\d+)\b", text)
        return f"סשן הפוקוס נגמר! זה ה-{n.group(1)} היום, כל הכבוד!" if n else "סשן הפוקוס נגמר! עבודה מעולה!"
    m = re.match(r"^While I was off, I missed: (.+)$", text)
    if m:
        missed = m.group(1).replace("a focus session", "סשן פוקוס")
        missed = re.sub(r" and (\d+) more", r" ועוד \1", missed)
        return f"בזמן שהייתי כבויה פספסתי: {missed}"
    m = re.match(r"^Claude needs you in (.+)$", text)
    if m:
        return f"Claude צריך אותך ב-{m.group(1)}"
    m = re.match(r"^Claude finished in (.+)$", text)
    if m:
        return f"Claude סיים ב-{m.group(1)}"
    return text


def load_config():
    path = data_dir() / "config.json"
    config = dict(DEFAULT_CONFIG)
    try:
        config.update(json.loads(path.read_text(encoding="utf-8")))
    except FileNotFoundError:
        save_config(config)
    except Exception as e:
        log.warning("could not read %s: %s", path, e)
    return config


def save_config(config):
    path = data_dir() / "config.json"
    path.write_text(json.dumps(config, indent=2), encoding="utf-8")


def setup_logging(console):
    handlers = [RotatingFileHandler(data_dir() / "gorgi-helper.log", maxBytes=500_000, backupCount=1, encoding="utf-8")]
    if console:
        handlers.append(logging.StreamHandler())
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s", handlers=handlers)


# ---------------------------------------------------------------- media backend


class FakeMedia:
    """Stand-in used on non-Windows systems, for development and tests."""

    def __init__(self):
        self.info = {"title": "Test Song", "artist": "Test Artist", "album": "Test Album", "playing": True,
                     "pos": 10.0, "dur": 200.0}
        self.art = None  # picture file bytes for get_art
        self.volume = 40
        self.commands = []

    async def get_info(self):
        return dict(self.info)

    async def get_art(self):
        return self.art

    async def command(self, action):
        self.commands.append(action)
        if action == "playpause":
            self.info["playing"] = not self.info["playing"]
        elif action == "next":
            self.info["title"] = "Next Song"

    async def set_playing(self, play):
        self.commands.append("play" if play else "pause")
        self.info["playing"] = play

    def get_volume(self):
        return self.volume

    def set_volume(self, value):
        self.commands.append(f"volume {value}")
        self.volume = value


def make_media():
    if IS_WINDOWS:
        from media_windows import WindowsMedia

        return WindowsMedia()
    return FakeMedia()


# ---------------------------------------------------------------- finding Gorgi


async def find_gorgi(config_address):
    """Returns the IP address of Gorgi, or None."""
    if config_address:
        return config_address

    # 1) Ask the network for the _gorgi._tcp service
    try:
        from zeroconf import IPVersion
        from zeroconf.asyncio import AsyncZeroconf

        aiozc = AsyncZeroconf(ip_version=IPVersion.V4Only)
        try:
            info = await aiozc.async_get_service_info(SERVICE_TYPE, SERVICE_NAME, timeout=3000)
            if info:
                addresses = info.parsed_addresses(IPVersion.V4Only)
                if addresses:
                    return addresses[0]
        finally:
            await aiozc.async_close()
    except Exception as e:
        log.debug("zeroconf lookup failed: %s", e)

    # 2) Windows 10+ can resolve .local names itself
    try:
        loop = asyncio.get_running_loop()
        infos = await asyncio.wait_for(loop.getaddrinfo("gorgi.local", PORT, family=socket.AF_INET), 4)
        if infos:
            return infos[0][4][0]
    except Exception as e:
        log.debug("gorgi.local lookup failed: %s", e)
    return None


# ---------------------------------------------------------------- the link


class _NoWindow:
    """Stand-in updates window (tests, or when windows can't be shown)."""

    open = False

    def show(self, state):
        pass

    def close(self):
        pass


def make_update_window(on_word):
    if not IS_WINDOWS:
        return _NoWindow()
    from update_window import UpdateWindow

    return UpdateWindow(on_word)


class Helper:
    def __init__(self, media, config, on_status=None, make_conversation=None, timekeeper=None, speak=None,
                 todos=None):
        self.media = media
        self.config = config
        self.timekeeper = timekeeper or TimeKeeper()  # Gorgi's timers and reminders, for the AI
        self.todos = todos or TodoKeeper()  # Gorgi's to-do list, for the AI
        self.speak = speak  # async fn(text): says something on the PC speakers (None = no voice)
        self.ears = None    # voice.Ears (English + Hebrew Whisper), when voice is available
        self._announcing = asyncio.Lock()
        # Claude Code sessions and usage (hooks -> claude_watch), and Teams calls
        self.claude = ClaudeWatch(on_change=self._claude_changed, on_alert=self._claude_alert)
        self.on_call = False
        self._paused_for_call = False
        self.on_status = on_status or (lambda text, connected: None)
        # make_conversation(send) -> Conversation, or None when voice isn't available
        self.make_conversation = make_conversation
        self.ws = None
        self.conversation_task = None
        self.stop_event = asyncio.Event()
        self.refresh_event = asyncio.Event()
        self.ignore_volume_until = 0.0
        self.status = ("Starting...", False)
        # updates
        self.on_notify = lambda title, message: None  # pop-up message near the clock
        self.on_changed = lambda: None                # menu needs refreshing
        self.gorgi_address = None
        self.gorgi_version = None
        self.gorgi_paired = False
        self.latest = None        # newest release on GitHub
        self.updating = False
        self._offered = set()     # versions we already told the user about
        self.make_update_window = make_update_window
        self.update_window = None
        self.marks = {}           # updates window: "helper"/"gorgi" -> todo, busy, ok, fail, offline
        self._view = {}

    def set_status(self, text, connected):
        if (text, connected) != self.status:
            self.status = (text, connected)
            log.info("status: %s", text)
            self.on_status(text, connected)

    async def run(self):
        backoff = 2
        while not self.stop_event.is_set():
            self.set_status("Looking for Gorgi...", False)
            address = await find_gorgi(self.config.get("gorgi_address", ""))
            if not address:
                await self._sleep(backoff)
                backoff = min(backoff * 2, 15)
                continue
            uri = f"ws://{address}:{PORT}/"
            try:
                async with websockets.connect(uri, open_timeout=5, ping_interval=15, ping_timeout=10) as ws:
                    backoff = 2
                    self.gorgi_address = address
                    self.set_status(f"Connected to Gorgi ({address})", True)
                    await self._session(ws)
            except (OSError, asyncio.TimeoutError, websockets.exceptions.WebSocketException) as e:
                log.info("connection to %s ended: %s", address, e)
            if not self.stop_event.is_set():
                self.set_status("Gorgi disconnected, retrying...", False)
                await self._sleep(backoff)
                backoff = min(backoff * 2, 15)

    # ------------------------------------------------------------ updates
    #
    # One "Gorgi updates" window does it all: tray "Update to ..." (or Gorgi's Update
    # button) opens it, "Update all" updates the helper first (it restarts; the new
    # helper reopens the window and carries on), then Gorgi, and it only says "All done"
    # once Gorgi is back with the new version.

    @property
    def helper_update_available(self):
        return bool(self.latest and self.latest.helper_url and getattr(sys, "frozen", False)
                    and updater.is_newer(self.latest.version, VERSION))

    @property
    def gorgi_update_available(self):
        return bool(self.latest and self.latest.firmware_url and self.ws is not None and self.gorgi_version
                    and updater.is_newer(self.latest.version, self.gorgi_version))

    async def update_loop(self):
        self._loop = asyncio.get_running_loop()
        flow = self.config.pop("update_flow", None)
        if self.config.pop("pending_gorgi_update", False) and not flow:
            flow = {"version": VERSION}  # an older helper was updated from Gorgi's panel
        if flow:
            save_config(self.config)
            await self.continue_after_restart(flow)
        else:
            await self._sleep(20)  # let everything start first
            await self.check_updates()
        while not self.stop_event.is_set():
            await self._sleep(24 * 3600)
            await self.check_updates()

    async def check_updates(self, notify=True):
        """Asks GitHub for the newest release. Returns False if that didn't work."""
        try:
            async with httpx.AsyncClient() as client:
                self.latest = await updater.fetch_latest(client)
            log.info("newest release: %s", self.latest.version if self.latest else "none yet")
        except Exception as e:
            log.warning("update check failed: %s", e)
            return False
        if notify:
            self.offer_updates()
        else:
            self.on_changed()
            self._send_soon(self.update_info())
        self._push()
        return True

    def _send_soon(self, msg):
        try:
            asyncio.get_running_loop().create_task(self.send(msg))
        except RuntimeError:  # not in the event loop (tray thread)
            pass

    def update_info(self):
        """For Gorgi's Action Center: is there something to update?"""
        return {"type": "update_info", "available": self.helper_update_available or self.gorgi_update_available,
                "version": self.latest.version if self.latest else "", "busy": self.updating}

    async def update_all(self):
        """Gorgi's "Update" button: the same as "Update all" in the window on the PC."""
        self.open_updates(start=True)

    async def check_updates_soon(self):
        """Gorgi's Action Center was opened: check again, at most every 10 minutes."""
        if time.monotonic() - getattr(self, "_last_check", -1e9) > 600:
            self._last_check = time.monotonic()
            await self.check_updates()
        else:
            await self.send(self.update_info())

    def offer_updates(self):
        """Tells the user about a new version (once per version)."""
        self.on_changed()
        self._send_soon(self.update_info())
        if not self.latest:
            return
        v = self.latest.version
        parts = []
        if self.helper_update_available:
            parts.append("Gorgi Helper")
        if self.gorgi_update_available:
            parts.append("Gorgi")
        if parts and v not in self._offered:
            self._offered.add(v)
            self.on_notify(f"Update {v} is ready",
                           f"New version for {' and '.join(parts)}. Right-click the Gorgi icon and choose "
                           f"\"Update to {v}\".")

    # ---- the window

    def open_updates(self, start=False):
        """Opens the updates window (or brings it to the front), checks GitHub, and with
        start=True goes straight on to updating. Call it in the event loop."""
        self._loop = asyncio.get_running_loop()
        if not (self.update_window and self.update_window.open):
            self.update_window = self.make_update_window(self._window_word)
            if not self.updating:
                self.marks = {}
                self._view = {"text": "Checking for updates...", "percent": "wait", "button": ""}
        self._push(raise_=True)
        if start and self.updating:
            return
        if not self.updating and not getattr(self, "_checking", False):
            self._loop.create_task(self._window_check(start))

    def _window_word(self, word):
        """A button in the window (called from a background thread)."""
        loop = getattr(self, "_loop", None)
        if loop:
            loop.call_soon_threadsafe(self._on_word, word)

    def _on_word(self, word):
        log.info("updates window: %s", word)
        if word in ("start", "retry"):
            asyncio.get_running_loop().create_task(self.run_updates())
        # "close": the window went away; an update already running carries on

    async def _window_check(self, start):
        self._checking = True
        try:
            self._show("Checking for updates...", "wait")
            if not await self.check_updates(notify=False):
                self._show("Couldn't check for updates. Is the internet working?", button="retry")
                return
        finally:
            self._checking = False
        if self.helper_update_available or self.gorgi_update_available:
            if start:
                await self.run_updates()
            else:
                self._show(f"Version {self.latest.version} is ready. Gorgi Helper updates first, then Gorgi.",
                           button="start")
        elif self.ws is None:
            self._show("Gorgi Helper is up to date. Gorgi isn't connected, so I can't check it right now.",
                       button="close")
        else:
            self._show("Everything is up to date.", button="close")

    def _rows(self):
        new = self.latest.version if self.latest else ""
        marks = getattr(self, "marks", {})
        helper_new = new if self.helper_update_available or marks.get("helper") in ("busy", "fail") else ""
        helper = {"name": "Gorgi Helper", "now": VERSION, "new": helper_new,
                  "state": marks.get("helper") or ("todo" if helper_new else "same")}
        gorgi_new = new if (self.gorgi_update_available or marks.get("gorgi") in ("busy", "fail")) else ""
        if marks.get("gorgi"):
            state = marks["gorgi"]
        elif self.ws is None or not self.gorgi_version:
            state = "offline"
        else:
            state = "todo" if gorgi_new else "same"
        gorgi = {"name": "Gorgi", "now": self.gorgi_version or "?", "new": gorgi_new, "state": state}
        return [helper, gorgi]

    def _show(self, text, percent=None, button=""):
        if isinstance(percent, float):
            percent = int(percent)  # whole percents: fewer messages to the window
        self._view = {"text": text, "percent": percent, "button": button}
        self._push()

    def _push(self, raise_=False):
        window = getattr(self, "update_window", None)
        if window and window.open:
            state = dict(getattr(self, "_view", {}), rows=self._rows())
            if raise_:
                state["raise"] = True
            window.show(state)

    # ---- the steps

    async def run_updates(self):
        """"Update all": the helper first (it restarts and carries on), then Gorgi."""
        if self.updating:
            return
        if not self.latest:  # the check didn't work: check again first
            await self._window_check(True)
            return
        self.marks = {}
        if self.helper_update_available:
            await self._update_helper_step()
        else:
            await self._update_gorgi_step()

    async def _update_helper_step(self):
        self.updating = True
        v = self.latest.version
        self.marks["helper"] = "busy"
        self.set_status(f"Downloading Gorgi Helper {v}...", self.ws is not None)
        await self.send({"type": "update_status", "text": "Updating the PC helper..."})
        try:
            async with httpx.AsyncClient() as client:
                data = await updater.download(client, self.latest.helper_url, lambda f: self._show(
                    f"Downloading Gorgi Helper {v}... {f * 100:.0f}%", f * 100))
            path = updater.save_installer(data, v)
            self.config["update_flow"] = {"version": v}  # the new helper carries on with Gorgi
            save_config(self.config)
            self._show("Installing Gorgi Helper. This window closes for a few seconds and comes back by itself.",
                       "wait")
            log.info("starting installer %s", path)
            await asyncio.sleep(2.5)
            updater.run_installer(path)
        except Exception as e:
            self.updating = False
            self.config.pop("update_flow", None)
            save_config(self.config)
            log.warning("helper update failed: %s", e)
            self.marks["helper"] = "fail"
            self._show(f"Couldn't update Gorgi Helper: {e}", button="retry")
            await self.send(self.update_info())
            return
        self.request_stop()  # quit, so the installer can replace this app

    async def continue_after_restart(self, flow):
        """The helper was just updated by "Update all": reopen the window and do Gorgi."""
        v = str(flow.get("version") or "")
        self.marks = {"helper": "ok" if v == VERSION else "fail"}
        self._view = {"text": "Gorgi Helper is updated. Looking for Gorgi...", "percent": "wait", "button": ""}
        self.open_window_only()
        if v != VERSION:
            self._show(f"Gorgi Helper didn't update (it's still {VERSION}).", button="retry")
            return
        for _ in range(3):
            if await self.check_updates(notify=False) and self.latest:
                break
            await self._sleep(5)
        if not self.latest:
            self._show("Gorgi Helper is updated, but I couldn't check Gorgi's update. Is the internet working?",
                       button="retry")
            return
        await self._update_gorgi_step()

    def open_window_only(self):
        self._loop = asyncio.get_running_loop()
        if not (self.update_window and self.update_window.open):
            self.update_window = self.make_update_window(self._window_word)
        self._push(raise_=True)

    async def _wait_for(self, check, seconds):
        end = time.monotonic() + seconds
        while not check():
            if time.monotonic() > end or self.stop_event.is_set():
                return False
            await self._sleep(1)
        return True

    async def _update_gorgi_step(self):
        self._show("Looking for Gorgi...", "wait")
        if not await self._wait_for(lambda: self.ws is not None and self.gorgi_version, 60):
            self.marks["gorgi"] = "offline"
            self._show("Gorgi isn't connected. Turn it on (same Wi-Fi as this PC) and press Try again.",
                       button="retry")
            return
        self.marks.pop("gorgi", None)
        if not self.gorgi_update_available:
            self._all_done()
            return
        if not self.gorgi_paired:
            self.marks["gorgi"] = "fail"
            self._show("Gorgi is paired with another helper. Hold Gorgi's clock for 5 seconds to reset it "
                       "(this also forgets its Wi-Fi), then press Try again.", button="retry")
            return
        self.updating = True
        v = self.latest.version
        self.marks["gorgi"] = "busy"
        self.set_status(f"Updating Gorgi to {v}...", True)
        await self.send({"type": "update_status", "text": "Sending the update to Gorgi..."})
        sending = "Sending to Gorgi (its screen turns off for a moment)..."
        try:
            async with httpx.AsyncClient() as client:
                data = await updater.download(client, self.latest.firmware_url, lambda f: self._show(
                    "Downloading Gorgi's update...", f * 30))
                log.info("sending firmware %s (%d bytes) to Gorgi", v, len(data))
                self._show(sending, 30)
                ok, message = await updater.send_firmware(
                    client, self.gorgi_address, self.config.get("helper_key", ""), data,
                    lambda f: self._show(sending, 30 + f * 68))
        except Exception as e:
            ok, message = False, f"download failed ({e})"
        log.info("Gorgi update: %s", message)
        if ok:
            self._show(f"Gorgi is restarting with version {v}...", "wait")
            ok = await self._wait_for(lambda: self.ws is not None and self.gorgi_version == v, 180)
            message = "it didn't come back with the new version in 3 minutes. Is it on?"
        self.updating = False
        self.on_changed()
        if ok:
            self.marks["gorgi"] = "ok"
            self._all_done()
        else:
            self.marks["gorgi"] = "fail"
            self._show(f"Gorgi's update didn't work: {message}", button="retry")
            await self.send(self.update_info())

    def _all_done(self):
        gorgi = f" and Gorgi {self.gorgi_version}" if self.gorgi_version else ""
        self._show(f"All done! Gorgi Helper {VERSION}{gorgi} are up to date.", button="close")
        self.set_status(f"Connected to Gorgi ({self.gorgi_address})", True)
        self._send_soon(self.update_info())

    def request_stop(self):
        """Quit (from the event loop). If something still holds the app open after a few
        seconds, it's ended anyway: a helper that won't quit blocks its own update."""
        self.stop_event.set()

        def force_quit():
            log.warning("didn't quit in time: forcing it")
            logging.shutdown()
            os._exit(0)

        timer = threading.Timer(8.0, force_quit)
        timer.daemon = True
        timer.start()

    async def _sleep(self, seconds):
        try:
            await asyncio.wait_for(self.stop_event.wait(), seconds)
        except asyncio.TimeoutError:
            pass

    async def send(self, msg):
        ws = self.ws
        if ws is None:
            return
        try:
            await ws.send(json.dumps(msg))
        except websockets.exceptions.WebSocketException:
            pass

    def stop_conversation(self):
        if self.conversation_task and not self.conversation_task.done():
            self.conversation_task.cancel()

    def start_conversation(self, dictate=False, mode="todo"):
        if self.conversation_task and not self.conversation_task.done():
            return
        if self.make_conversation is None:
            msg = ({"type": "dictate_state", "state": "idle", "problem": "Voice isn't installed on the PC"} if dictate
                   else {"type": "say", "mood": "sad", "text": "My voice parts aren't installed on the PC."})
            asyncio.get_running_loop().create_task(self.send(msg))
            return
        conversation = self.make_conversation(self.send)
        conversation.dictate = dictate
        conversation.dictate_mode = mode
        self.conversation_task = asyncio.get_running_loop().create_task(self._run_conversation(conversation))

    async def _run_conversation(self, conversation):
        try:
            await conversation.run()
        except asyncio.CancelledError:
            log.info("conversation cancelled")
        except Exception:
            log.exception("conversation failed")
            if not conversation.dictate:  # (voice typing already said it's done)
                await self.send({"type": "voice", "state": "idle"})

    async def _session(self, ws):
        self.ws = ws
        await ws.send(json.dumps({"type": "hello", "name": platform.node() or "PC",
                                  "key": self.config.get("helper_key", "")}))
        await ws.send(json.dumps(self.claude_message()))
        await ws.send(json.dumps({"type": "language", "value": lang.code()}))
        if self.on_call:
            await ws.send(json.dumps({"type": "call", "on": True}))
        sender = asyncio.create_task(self._send_loop(ws))
        stopper = asyncio.create_task(self.stop_event.wait())
        receiver = asyncio.create_task(self._receive_loop(ws))
        try:
            done, _ = await asyncio.wait({sender, receiver, stopper}, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                if task is not stopper and task.exception():
                    raise task.exception()
        finally:
            self.ws = None
            self.gorgi_version = None
            self.timekeeper.forget()
            self.todos.forget()
            self.on_changed()
            self._push()
            self.stop_conversation()
            for task in (sender, receiver, stopper):
                task.cancel()
            await ws.close()

    async def _receive_loop(self, ws):
        async for raw in ws:
            try:
                msg = json.loads(raw)
            except ValueError:
                continue
            if msg.get("type") == "hello":
                self.gorgi_version = msg.get("version")
                self.gorgi_paired = bool(msg.get("paired"))
                log.info("Gorgi firmware version: %s (%s)", self.gorgi_version,
                         "paired" if self.gorgi_paired else "paired with another helper")
                self.offer_updates()
                self._push()
                continue
            if msg.get("type") == "weather":
                weather.update(msg)
                log.info("weather for %s from Gorgi", msg.get("city"))
                continue
            if msg.get("type") == "time_list":
                self.timekeeper.update(msg)
                continue
            if msg.get("type") == "todo_list":
                self.todos.update(msg)
                continue
            if msg.get("type") == "due":
                log.info("due on Gorgi (%s): %s", msg.get("kind"), msg.get("text"))
                asyncio.get_running_loop().create_task(self.announce(str(msg.get("text") or "")))
                continue
            if msg.get("type") == "talk":
                log.info("Gorgi was tapped: starting a conversation")
                self.start_conversation()
                continue
            if msg.get("type") == "dictate":
                mode = "text" if msg.get("mode") == "text" else "todo"
                log.info("voice typing on the To-do screen (%s)", mode)
                self.start_conversation(dictate=True, mode=mode)
                continue
            if msg.get("type") == "cancel":
                log.info("Gorgi was tapped again: stopping the conversation")
                self.stop_conversation()
                continue
            if msg.get("type") != "cmd":
                continue
            action = msg.get("action")
            log.info("command from Gorgi: %s %s", action, msg.get("value", ""))
            if action == "teams_mute":
                asyncio.get_running_loop().create_task(asyncio.to_thread(call_watch.teams_mute))
                continue
            if action == "update":
                asyncio.get_running_loop().create_task(self.update_all())
                continue
            if action == "check_updates":
                asyncio.get_running_loop().create_task(self.check_updates_soon())
                continue
            if action == "language":  # the switch in Gorgi's Action Center: 0 English, 1 Hebrew
                asyncio.get_running_loop().create_task(self.set_language("he" if msg.get("value") == 1 else "en"))
                continue
            if action in ("playpause", "next", "prev"):
                await self.media.command(action)
            elif action == "volume" and isinstance(msg.get("value"), (int, float)):
                self.media.set_volume(msg["value"])
                self.ignore_volume_until = time.monotonic() + 0.5
            # Report the new state soon, once the media app has reacted
            asyncio.get_running_loop().call_later(0.4, self.refresh_event.set)

    # ------------------------------------------------------------ Claude Code and calls

    def claude_message(self):
        return self.claude.message(hooked=self.claude.hooked or claude_setup.status()[0])

    def _claude_changed(self):
        asyncio.get_running_loop().create_task(self.send(self.claude_message()))

    def _claude_alert(self, kind, name, text):
        log.info("claude alert (%s): %s", kind, text)
        loop = asyncio.get_running_loop()
        loop.create_task(self.send({"type": "claude_alert", "kind": kind, "name": name, "text": text}))
        loop.create_task(self.announce(text))

    async def call_loop(self):
        """Every 2 s: is Teams using the microphone?"""
        if not call_watch.IS_WINDOWS:
            return
        while not self.stop_event.is_set():
            try:
                on = call_watch.in_call(await asyncio.to_thread(call_watch.apps_using_mic))
                if on != self.on_call:
                    await self._set_call(on)
            except Exception as e:
                log.warning("call check failed: %s", e)
            await self._sleep(2)

    async def _set_call(self, on):
        self.on_call = on
        log.info("Teams call %s", "started" if on else "ended")
        await self.send({"type": "call", "on": on})
        try:
            if on:
                info = await self.media.get_info()
                if info.get("playing"):
                    await self.media.set_playing(False)
                    self._paused_for_call = True
            elif self._paused_for_call:
                self._paused_for_call = False
                await self.media.set_playing(True)
        except Exception as e:
            log.warning("music during call: %s", e)

    # ------------------------------------------------------------ language

    async def set_language(self, code):
        """Gorgi's old EN/עב switch (firmware before 4.8.4): just use that language until you speak."""
        if code in lang.LANGUAGES and code != lang.code():
            lang.set_spoken(code)

    async def language_changed(self):
        """You spoke the other language: remember it and tell Gorgi (its screen texts follow it)."""
        save_config(self.config)
        log.info("language: %s", lang.code())
        await self.send({"type": "language", "value": lang.code()})

    async def _tell(self, mood, text):
        """Gorgi says something by itself: on its screen and out loud (unless busy talking or on a call)."""
        if self.ws is None:
            return
        if self.conversation_task and not self.conversation_task.done():
            return
        await self.send({"type": "say", "text": text, "mood": mood})
        if self.speak is None or self.on_call:
            return
        async with self._announcing:
            try:
                await self.speak(text)
            except Exception as e:
                log.warning("couldn't say it out loud: %s", e)

    async def announce(self, text):
        """Something on Gorgi is due: a pop-up, and Gorgi's voice says it."""
        if not text:
            return
        if self.ws is None:  # Gorgi isn't connected: no pop-up or voice either
            log.info("Gorgi not connected, not saying: %s", text)
            return
        if self.on_call:  # quiet during calls: Gorgi's screen shows it
            log.info("on a call, not saying: %s", text)
            return
        if lang.hebrew():
            text = hebrew_alert(text)
        self.on_notify("Gorgi", text)
        if self.speak is None:
            return
        # Not in the middle of a conversation: wait for it to end (a minute at most)
        for _ in range(120):
            if not (self.conversation_task and not self.conversation_task.done()):
                break
            await asyncio.sleep(0.5)
        async with self._announcing:
            paused = False
            try:
                info = await self.media.get_info()
                if info.get("playing"):
                    await self.media.set_playing(False)
                    paused = True
                    await asyncio.sleep(0.3)
                await self.speak(text)
            except Exception as e:
                log.warning("couldn't say it out loud: %s", e)
            finally:
                if paused:
                    await asyncio.sleep(0.4)
                    await self.media.set_playing(True)

    async def _send_art(self, ws, track):
        """Sends the song's picture to Gorgi (once per picture). Returns its id, or 0."""
        try:
            raw = await self.media.get_art()
            if not raw:
                return 0
            if raw == self._art_raw:
                return self._art_id
            pixels = await asyncio.to_thread(album_art.to_rgb565, raw)
        except Exception as e:
            log.info("album art skipped: %s", e)
            return 0
        self._art_raw = raw
        self._art_id += 1
        for piece in album_art.pieces(self._art_id, pixels):
            await ws.send(piece)
        log.info("album art sent for %s", track[0])
        return self._art_id

    async def _send_loop(self, ws):
        last_media = None
        last_clock = None
        last_full_send = 0.0
        last_track = None
        art_id = 0
        art_recheck_at = None  # apps sometimes update the picture a moment after the song
        sent_pos = None  # (position, when) last sent, to notice jumps
        self._art_raw = None
        self._art_id = getattr(self, "_art_id", 0)
        while True:
            info = await self.media.get_info()
            volume = self.media.get_volume()
            now = time.monotonic()

            track = (info["title"], info["artist"], info.get("album", ""))
            if track != last_track:
                last_track = track
                art_id = await self._send_art(ws, track) if info["title"] else 0
                art_recheck_at = now + 3
            elif art_recheck_at and now >= art_recheck_at:
                art_recheck_at = None
                if info["title"]:
                    art_id = await self._send_art(ws, track) or art_id

            media = {
                "type": "media",
                "title": info["title"],
                "artist": info["artist"],
                "album": info.get("album", ""),
                "playing": info["playing"],
                "volume": 50 if volume is None else volume,
                "art": art_id,
            }
            pos, dur = info.get("pos"), info.get("dur")
            # The position moves all the time; Gorgi counts along by itself. It's
            # only sent again when it jumps (you skipped ahead) or with other news.
            jumped = False
            if pos is not None and dur:
                if sent_pos is not None:
                    expected = sent_pos[0] + ((now - sent_pos[1]) if info["playing"] else 0)
                    jumped = abs(pos - expected) > 2.5
                else:
                    jumped = True
            # Resend every 30 s even if nothing changed, in case Gorgi restarted
            full = now - last_full_send > 30
            if full or jumped or (media != last_media and now >= self.ignore_volume_until):
                message = dict(media)
                if pos is not None and dur:
                    message["pos"] = round(pos, 1)
                    message["dur"] = round(dur, 1)
                    sent_pos = (pos, now)
                else:
                    sent_pos = None
                await ws.send(json.dumps(message))
                last_media = media
            if full:
                last_full_send = now

            clock = time.strftime("%H:%M")
            if full or clock != last_clock:
                await ws.send(json.dumps({"type": "clock", "text": clock, "epoch": int(time.time())}))
                last_clock = clock

            self.refresh_event.clear()
            try:
                await asyncio.wait_for(self.refresh_event.wait(), POLL_SECONDS)
            except asyncio.TimeoutError:
                pass


# ---------------------------------------------------------------- Windows bits


def already_running():
    """True if another Gorgi Helper is running (Windows only)."""
    if not IS_WINDOWS:
        return False
    import ctypes

    ctypes.windll.kernel32.CreateMutexW(None, False, "GorgiHelperSingleInstance")
    return ctypes.windll.kernel32.GetLastError() == 183  # ERROR_ALREADY_EXISTS


RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"


def autostart_enabled():
    import winreg

    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            winreg.QueryValueEx(key, APP_NAME)
            return True
    except OSError:
        return False


def set_autostart(enabled):
    import winreg

    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY, 0, winreg.KEY_SET_VALUE) as key:
        if enabled:
            if getattr(sys, "frozen", False):
                command = f'"{sys.executable}"'
            else:
                command = f'"{sys.executable}" "{os.path.abspath(__file__)}"'
            winreg.SetValueEx(key, APP_NAME, 0, winreg.REG_SZ, command)
        else:
            try:
                winreg.DeleteValue(key, APP_NAME)
            except OSError:
                pass


def start_tray(helper, loop):
    import pystray

    from icon import make_image

    images = {True: make_image(64, True), False: make_image(64, False)}

    def quit_app(icon, _item):
        icon.stop()
        loop.call_soon_threadsafe(helper.request_stop)

    def toggle_autostart(_icon, _item):
        set_autostart(not autostart_enabled())

    def open_folder(_icon, _item):
        os.startfile(data_dir())

    def open_settings_item(_icon, _item):
        show_settings(helper.config)

    def open_memories(_icon, _item):
        path = data_dir() / "memory.json"
        if not path.exists():
            path.write_text('{"facts": []}', encoding="utf-8")
        os.startfile(path)  # opens in Notepad (or whatever opens .json files)

    menu = pystray.Menu(
        pystray.MenuItem(lambda _item: helper.status[0], None, enabled=False),
        pystray.MenuItem(f"Version {VERSION}", None, enabled=False),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem(lambda _item: f"Update to {helper.latest.version}..."
                         if helper.helper_update_available or helper.gorgi_update_available else "Check for updates...",
                         lambda _icon, _item: loop.call_soon_threadsafe(helper.open_updates)),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Settings...", open_settings_item, default=True),
        pystray.MenuItem("Memories...", open_memories),
        pystray.MenuItem("Start with Windows", toggle_autostart, checked=lambda _item: autostart_enabled()),
        pystray.MenuItem("Open settings folder", open_folder),
        pystray.MenuItem("Quit", quit_app),
    )
    icon = pystray.Icon("GorgiHelper", images[False], APP_NAME, menu)

    def on_status(text, connected):
        icon.icon = images[connected]
        icon.title = f"{APP_NAME} - {text}"
        icon.update_menu()

    helper.on_status = lambda text, connected: on_status(text, connected)

    def notify(title, message):
        try:
            icon.notify(message, title)
        except Exception as e:  # not every Windows setup allows it
            log.info("notification failed: %s", e)

    helper.on_notify = notify
    helper.on_changed = icon.update_menu
    icon.run_detached()
    return icon


_settings_process = None




def show_settings(config):
    """Opens the Settings window as a separate process, then reloads the settings."""
    import subprocess

    global _settings_process
    if _settings_process is not None and _settings_process.poll() is None:
        return  # already open
    if getattr(sys, "frozen", False):
        command = [sys.executable, "--settings"]
    else:
        command = [sys.executable, os.path.abspath(__file__), "--settings"]
    _settings_process = subprocess.Popen(command)

    def wait_and_reload(process):
        process.wait()
        spoken = config.get("language")  # follows your voice; the Settings window doesn't change it
        config.update(load_config())  # the brain and voice read this same dict
        config["language"] = spoken
        log.info("settings reloaded (model: %s, voice: %s, hearing: %s, key %s)", config.get("model"),
                 config.get("voice"), config.get("hearing"), "set" if config.get(f"{config.get('hearing')}_api_key") else "missing")

    threading.Thread(target=wait_and_reload, args=(_settings_process,), daemon=True).start()


def run_settings_window():
    """The --settings process: show the window in this process's main thread."""
    from brain import Brain
    from settings_window import show_window

    config = load_config()

    async def list_models(key):
        return await Brain({**config, "ollama_api_key": key}).list_models()

    show_window(config, save_config, list_models, report_dir=data_dir())


# ---------------------------------------------------------------- voice


def make_voice(config):
    """Builds the conversation parts. Returns (conversation factory, speak), or (None, None)
    if they can't load."""
    try:
        from brain import Brain
        from conversation import Conversation
        from voice import CloudTranscriber, Ears, Recorder, Speaker, Transcriber, forget_hebrew_model
    except Exception as e:
        log.error("voice features unavailable: %s", e)
        return None, None

    from memory import Memory

    brain = Brain(config, Memory(data_dir() / "memory.json"))  # one brain, so Gorgi remembers between taps
    english = Transcriber(config.get("whisper_model") or "base.en", data_dir() / "models")
    transcriber = Ears(english, CloudTranscriber(config))  # the cloud (Hebrew + English) with a key, else English here
    forget_hebrew_model(data_dir() / "models")  # 4.8-4.8.3 downloaded a 1.6 GB Hebrew model: not used any more
    # Download/load Whisper in the background so the first chat is quick
    threading.Thread(target=transcriber.load, daemon=True).start()

    def speaker():
        voice = config.get("voice") or "en-US-AnaNeural"  # read each time: Settings may change it
        return Speaker(voice, pitch="+5Hz", voice_he=config.get("voice_he") or "he-IL-HilaNeural")

    def factory(send, media, timekeeper, extra_context=None, todos=None):
        return Conversation(send, media, brain, Recorder(), transcriber, speaker(), timekeeper=timekeeper,
                            extra_context=extra_context, todos=todos)

    async def speak(text):
        await speaker().say(text)

    factory.ears = transcriber
    return factory, speak


# ---------------------------------------------------------------- self test (used by the build)


class _Report(list):
    """Lines of the self-test report, saved after every step (in case of a crash)."""

    def __init__(self, path):
        super().__init__()
        self.path = Path(path)

    def append(self, line):
        super().append(line)
        self.path.write_text("\n".join(self), encoding="utf-8")


def self_test(out_path):
    lines = _Report(out_path)
    lines.append("starting")
    import faulthandler

    crash_log = open(Path(out_path).with_suffix(".crash.txt"), "w")
    faulthandler.enable(crash_log)  # records where a hard crash happened
    try:
        import numpy  # first, so a problem here shows its real error

        lines.append(f"numpy: {numpy.__version__}")
        media = make_media()
        lines.append(f"media backend: {type(media).__name__}")
        info = asyncio.run(media.get_info())
        lines.append(f"media info: {info}")
        lines.append(f"volume: {media.get_volume()}")
        from icon import make_image

        make_image(64)
        lines.append("icon: ok")
        import pystray  # noqa: F401
        import zeroconf  # noqa: F401
        import brain  # noqa: F401
        import memory  # noqa: F401
        import update_window  # noqa: F401
        import conversation  # noqa: F401
        import timekeeper  # noqa: F401
        import todolist  # noqa: F401
        import claude_watch  # noqa: F401
        import claude_setup as cs
        import call_watch as cw

        lines.append(f"mic in use by: {cw.apps_using_mic()}")
        lines.append(f"claude code connected: {cs.status()}")
        import usage_fetch

        token, plan = usage_fetch.read_sign_in()
        lines.append(f"claude sign-in found: {bool(token)}, plan: {plan}")
        lines.append(f"claude desktop sign-in: {usage_fetch.read_desktop_sign_in()[1]}")
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM

        assert AESGCM(b"k" * 32).decrypt(b"n" * 12, AESGCM(b"k" * 32).encrypt(b"n" * 12, b"hi", None), None) == b"hi"
        if IS_WINDOWS:  # Windows' per-user lock, both ways (the desktop app's key is locked like this)
            import ctypes
            from ctypes import wintypes

            class Blob(ctypes.Structure):
                _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_char))]

            buf = ctypes.create_string_buffer(b"gorgi", 5)
            out = Blob()
            assert ctypes.windll.crypt32.CryptProtectData(
                ctypes.byref(Blob(5, ctypes.cast(buf, ctypes.POINTER(ctypes.c_char)))), None, None, None, None, 0,
                ctypes.byref(out))
            locked = ctypes.string_at(out.pbData, out.cbData)
            assert usage_fetch._dpapi_unprotect(locked) == b"gorgi"
        lines.append("usage decryption: ok")
        import settings_window  # noqa: F401
        import model_test  # noqa: F401

        import updater as upd

        assert upd.is_newer("3.10", "3.9") and not upd.is_newer("3.4", "3.4")
        lines.append("imports: ok")
        task = todolist.parse_spoken("buy milk tomorrow")
        assert task["text"] == "Buy milk" and task["due"], task
        lines.append(f"to-do from speech: {task}")
        from voice import word_timings

        assert word_timings("Hi there", [(0.1, "Hi"), (0.4, "there")], 1.0) == [[100, 1], [400, 2]]
        if IS_WINDOWS:  # the updates window starts and can talk back (its buttons use that)
            import queue as _queue

            from update_window import UpdateWindow

            words = _queue.Queue()
            w = UpdateWindow(words.put)
            w.show({"rows": [{"name": "Gorgi Helper", "now": VERSION, "state": "same"}], "text": "Self-test",
                    "button": "close"})
            word = words.get(timeout=30)
            w.close()
            lines.append(f"updates window: {word}")
            assert word == "ready", word

        # Gorgi speaks a sentence, then Whisper listens to it: checks both ends
        import numpy as np

        from voice import Speaker, Transcriber, bundled_model_dir

        lines.append(f"bundled whisper model: {bundled_model_dir('base.en')}")

        speech = asyncio.run(Speaker("en-US-AnaNeural").synthesize("Hello Gorgi, please play the next song."))
        lines.append(f"voice: {len(speech)} samples")
        audio16k = np.interp(np.arange(0, len(speech), 1.5), np.arange(len(speech)), speech).astype(np.float32)
        t = Transcriber("base.en", Path(out_path).resolve().parent / "models")
        lines.append("loading whisper...")
        t.load()
        if t.error:
            raise RuntimeError(f"whisper failed to load: {t.error}")
        lines.append("whisper loaded, transcribing...")
        heard = t.transcribe(audio16k)
        lines.append(f"whisper heard: {heard!r}")
        if "next song" not in heard.lower():
            raise RuntimeError("whisper did not understand the test sentence")
        # Hebrew: Hila says a sentence (the Hebrew voice works)
        he_speech = asyncio.run(Speaker("en-US-AnaNeural").synthesize("גורגי, תפעיל טיימר של עשר דקות לפסטה."))
        lines.append(f"hebrew voice: {len(he_speech)} samples")
        he16k = np.interp(np.arange(0, len(he_speech), 1.5), np.arange(len(he_speech)), he_speech).astype(np.float32)
        # Cloud hearing (Hebrew + English) with a fake key: Groq must answer "bad key"
        from voice import CloudTranscriber, wav_bytes

        lines.append(f"hebrew wav: {len(wav_bytes(he16k))} bytes")
        try:
            CloudTranscriber({"hearing": "groq", "groq_api_key": "selftest-fake-key"}).transcribe(he16k)
            lines.append("hebrew cloud: unexpected success")
        except Exception as e:
            lines.append(f"hebrew cloud: groq reached, answered as expected ({str(e)[:60]})")
        # Talk to Ollama Cloud with a fake key: must come back as "key doesn't work"
        from brain import Brain, BrainError

        try:
            asyncio.run(Brain({"ollama_api_key": "selftest-fake-key"}).ask("hello", "selftest"))
            lines.append("ollama: unexpected success")
        except BrainError as e:
            lines.append(f"ollama: reached, answered as expected ({e.spoken})")

        import sounddevice

        try:
            lines.append(f"sound devices: {len(sounddevice.query_devices())}")
            asyncio.run(Speaker("en-US-AnaNeural").play(speech[:2400]))
            lines.append("playback: ok")
        except Exception as e:  # build machines have no speakers
            lines.append(f"playback: no audio device here ({e!r})")
        lines.append("SELFTEST OK")
    except Exception as e:
        import traceback

        lines.append(f"SELFTEST FAILED: {e!r}")
        lines.append(traceback.format_exc())
    Path(out_path).write_text("\n".join(lines), encoding="utf-8")


# ---------------------------------------------------------------- main


def main():
    parser = argparse.ArgumentParser(description=APP_NAME)
    parser.add_argument("--no-tray", action="store_true", help="run without the tray icon")
    parser.add_argument("--address", help="Gorgi's IP address (skips auto-discovery)")
    parser.add_argument("--selftest", metavar="FILE", help=argparse.SUPPRESS)
    parser.add_argument("--settings", action="store_true", help="only show the Settings window")
    parser.add_argument("--updates", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.selftest:
        self_test(args.selftest)
        return
    if args.settings:
        run_settings_window()
        return
    if args.updates:
        from update_window import run_window

        run_window()
        return

    setup_logging(console=args.no_tray or not getattr(sys, "frozen", False))
    import faulthandler

    # If the app ever crashes hard, crash.log in the settings folder says where
    crash_file = open(data_dir() / "crash.log", "a", encoding="utf-8")
    crash_file.write(f"\n--- started {time.strftime('%Y-%m-%d %H:%M:%S')} ---\n")
    crash_file.flush()
    faulthandler.enable(crash_file)
    if already_running():
        log.info("another Gorgi Helper is already running")
        return

    config = load_config()
    lang.bind(config)  # English or Hebrew, read from this dict whenever it's needed
    if not config.get("helper_key"):
        config["helper_key"] = secrets.token_hex(16)  # Gorgi pairs with the first helper it meets
        save_config(config)
    if args.address:
        config["gorgi_address"] = args.address

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    media = make_media()
    voice_factory, speak = make_voice(config)
    timekeeper = TimeKeeper()
    todos = TodoKeeper()
    helper = None

    def extra_context():
        lines = [helper.claude.summary()] if helper else []
        if helper and helper.on_call:
            lines.append("Your friend is on a Teams call right now.")
        return "\n".join(x for x in lines if x)

    make_conversation = ((lambda send: voice_factory(send, media, timekeeper, extra_context, todos))
                         if voice_factory else None)
    helper = Helper(media, config, make_conversation=make_conversation, timekeeper=timekeeper, speak=speak,
                    todos=todos)
    helper.ears = getattr(voice_factory, "ears", None)
    # You spoke the other language (heard in a worker thread): tell Gorgi from the main loop
    lang.on_change(lambda code: loop.call_soon_threadsafe(lambda: loop.create_task(helper.language_changed())))
    icon = None
    if not args.no_tray:
        icon = start_tray(helper, loop)
        if not config.get("ollama_api_key") and not config.get("ollama_url"):
            show_settings(config)  # first run: ask for the Ollama key
    log.info("%s started", APP_NAME)
    try:
        if claude_setup.upgrade():  # connected by an older helper: add the new hooks
            log.info("added Gorgi's new hooks to Claude Code's settings")
    except Exception as e:
        log.warning("couldn't update Claude Code's hooks: %s", e)
    try:
        usage = UsageFetcher(helper.claude, data_dir() / "claude-usage.json")
        loop.run_until_complete(asyncio.gather(helper.run(), helper.update_loop(),
                                               helper.claude.serve(helper.stop_event), helper.call_loop(),
                                               usage.run(helper.stop_event)))
    except KeyboardInterrupt:
        pass
    finally:
        if icon:
            icon.stop()
        log.info("%s stopped", APP_NAME)
        logging.shutdown()
        os._exit(0)  # don't let a leftover thread keep the app (and its files) alive


if __name__ == "__main__":
    main()
