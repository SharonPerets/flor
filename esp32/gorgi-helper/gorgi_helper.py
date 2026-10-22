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
import socket
import sys
import threading
import time
from logging.handlers import RotatingFileHandler
from pathlib import Path

import websockets

APP_NAME = "Gorgi Helper"
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
}


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
        self.info = {"title": "Test Song", "artist": "Test Artist", "playing": True}
        self.volume = 40
        self.commands = []

    async def get_info(self):
        return dict(self.info)

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


class Helper:
    def __init__(self, media, config, on_status=None, make_conversation=None):
        self.media = media
        self.config = config
        self.on_status = on_status or (lambda text, connected: None)
        # make_conversation(send) -> Conversation, or None when voice isn't available
        self.make_conversation = make_conversation
        self.ws = None
        self.conversation_task = None
        self.stop_event = asyncio.Event()
        self.refresh_event = asyncio.Event()
        self.ignore_volume_until = 0.0
        self.status = ("Starting...", False)

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
                    self.set_status(f"Connected to Gorgi ({address})", True)
                    await self._session(ws)
            except (OSError, asyncio.TimeoutError, websockets.exceptions.WebSocketException) as e:
                log.info("connection to %s ended: %s", address, e)
            if not self.stop_event.is_set():
                self.set_status("Gorgi disconnected, retrying...", False)
                await self._sleep(backoff)
                backoff = min(backoff * 2, 15)

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

    def start_conversation(self):
        if self.conversation_task and not self.conversation_task.done():
            return
        if self.make_conversation is None:
            asyncio.get_running_loop().create_task(
                self.send({"type": "say", "mood": "sad", "text": "My voice parts aren't installed on the PC."}))
            return
        conversation = self.make_conversation(self.send)
        self.conversation_task = asyncio.get_running_loop().create_task(self._run_conversation(conversation))

    async def _run_conversation(self, conversation):
        try:
            await conversation.run()
        except asyncio.CancelledError:
            log.info("conversation cancelled")
        except Exception:
            log.exception("conversation failed")
            await self.send({"type": "voice", "state": "idle"})

    async def _session(self, ws):
        self.ws = ws
        await ws.send(json.dumps({"type": "hello", "name": platform.node() or "PC"}))
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
            if msg.get("type") == "talk":
                log.info("Gorgi was tapped: starting a conversation")
                self.start_conversation()
                continue
            if msg.get("type") == "cancel":
                log.info("Gorgi was tapped again: stopping the conversation")
                self.stop_conversation()
                continue
            if msg.get("type") != "cmd":
                continue
            action = msg.get("action")
            log.info("command from Gorgi: %s %s", action, msg.get("value", ""))
            if action in ("playpause", "next", "prev"):
                await self.media.command(action)
            elif action == "volume" and isinstance(msg.get("value"), (int, float)):
                self.media.set_volume(msg["value"])
                self.ignore_volume_until = time.monotonic() + 0.5
            # Report the new state soon, once the media app has reacted
            asyncio.get_running_loop().call_later(0.4, self.refresh_event.set)

    async def _send_loop(self, ws):
        last_media = None
        last_clock = None
        last_full_send = 0.0
        while True:
            info = await self.media.get_info()
            volume = self.media.get_volume()
            media = {
                "type": "media",
                "title": info["title"],
                "artist": info["artist"],
                "playing": info["playing"],
                "volume": 50 if volume is None else volume,
            }
            now = time.monotonic()
            # Resend every 30 s even if nothing changed, in case Gorgi restarted
            full = now - last_full_send > 30
            if full or (media != last_media and now >= self.ignore_volume_until):
                await ws.send(json.dumps(media))
                last_media = media
            if full:
                last_full_send = now

            clock = time.strftime("%H:%M")
            if full or clock != last_clock:
                await ws.send(json.dumps({"type": "clock", "text": clock}))
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
        loop.call_soon_threadsafe(helper.stop_event.set)

    def toggle_autostart(_icon, _item):
        set_autostart(not autostart_enabled())

    def open_folder(_icon, _item):
        os.startfile(data_dir())

    def open_settings_item(_icon, _item):
        show_settings(helper.config)

    menu = pystray.Menu(
        pystray.MenuItem(lambda _item: helper.status[0], None, enabled=False),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Settings...", open_settings_item, default=True),
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
    icon.run_detached()
    return icon


def show_settings(config):
    from brain import Brain
    from settings_window import open_settings

    async def list_models(key):
        return await Brain({**config, "ollama_api_key": key}).list_models()

    open_settings(config, save_config, list_models)


# ---------------------------------------------------------------- voice


def make_voice(config):
    """Builds the conversation parts. Returns a factory, or None if they can't load."""
    try:
        from brain import Brain
        from conversation import Conversation
        from voice import Recorder, Speaker, Transcriber
    except Exception as e:
        log.error("voice features unavailable: %s", e)
        return None

    brain = Brain(config)  # one brain, so Gorgi remembers between taps
    transcriber = Transcriber(config.get("whisper_model") or "base.en", data_dir() / "models")
    # Download/load Whisper in the background so the first chat is quick
    threading.Thread(target=transcriber.load, daemon=True).start()

    def factory(send, media):
        voice = config.get("voice") or "en-US-AnaNeural"  # read each time: Settings may change it
        return Conversation(send, media, brain, Recorder(), transcriber, Speaker(voice, pitch="+5Hz"))

    return factory


# ---------------------------------------------------------------- self test (used by the build)


def self_test(out_path):
    lines = []
    try:
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
        import conversation  # noqa: F401
        import settings_window  # noqa: F401

        lines.append("imports: ok")

        # Gorgi speaks a sentence, then Whisper listens to it: checks both ends
        import numpy as np

        from voice import Speaker, Transcriber

        speech = asyncio.run(Speaker("en-US-AnaNeural").synthesize("Hello Gorgi, please play the next song."))
        lines.append(f"voice: {len(speech)} samples")
        audio16k = np.interp(np.arange(0, len(speech), 1.5), np.arange(len(speech)), speech).astype(np.float32)
        t = Transcriber("base.en", Path(out_path).resolve().parent / "models")
        heard = t.transcribe(audio16k)
        lines.append(f"whisper heard: {heard!r}")
        if "next song" not in heard.lower():
            raise RuntimeError("whisper did not understand the test sentence")
        import sounddevice  # noqa: F401

        lines.append("sound: ok")
        lines.append("SELFTEST OK")
    except Exception as e:
        lines.append(f"SELFTEST FAILED: {e!r}")
    Path(out_path).write_text("\n".join(lines), encoding="utf-8")


# ---------------------------------------------------------------- main


def main():
    parser = argparse.ArgumentParser(description=APP_NAME)
    parser.add_argument("--no-tray", action="store_true", help="run without the tray icon")
    parser.add_argument("--address", help="Gorgi's IP address (skips auto-discovery)")
    parser.add_argument("--selftest", metavar="FILE", help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.selftest:
        self_test(args.selftest)
        return

    setup_logging(console=args.no_tray or not getattr(sys, "frozen", False))
    if already_running():
        log.info("another Gorgi Helper is already running")
        return

    config = load_config()
    if args.address:
        config["gorgi_address"] = args.address

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    media = make_media()
    voice_factory = make_voice(config)
    make_conversation = (lambda send: voice_factory(send, media)) if voice_factory else None
    helper = Helper(media, config, make_conversation=make_conversation)
    icon = None
    if not args.no_tray:
        icon = start_tray(helper, loop)
        if not config.get("ollama_api_key") and not config.get("ollama_url"):
            show_settings(config)  # first run: ask for the Ollama key
    log.info("%s started", APP_NAME)
    try:
        loop.run_until_complete(helper.run())
    except KeyboardInterrupt:
        pass
    finally:
        if icon:
            icon.stop()
        log.info("%s stopped", APP_NAME)


if __name__ == "__main__":
    main()
