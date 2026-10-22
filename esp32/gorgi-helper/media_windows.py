"""Windows media and volume control for Gorgi Helper.

- Song info comes from the Windows media session (the same info shown in the
  volume pop-up), so it works with Spotify, browsers, the Media Player app...
- Play/pause/next/previous use the media session, with the keyboard media keys
  as a fallback.
- Volume is the PC's master volume.
"""

import concurrent.futures
import ctypes
import datetime
import logging
import sys

# Use the multi-threaded COM mode for both libraries below, so they don't clash.
sys.coinit_flags = 0  # COINIT_MULTITHREADED, read by comtypes on import

from ctypes import POINTER, cast  # noqa: E402

from comtypes import CLSCTX_ALL  # noqa: E402
from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume  # noqa: E402
from winrt.windows.media.control import (  # noqa: E402
    GlobalSystemMediaTransportControlsSessionManager as MediaManager,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus,
)

log = logging.getLogger("gorgi.media")

VK_MEDIA_NEXT_TRACK = 0xB0
VK_MEDIA_PREV_TRACK = 0xB1
VK_MEDIA_PLAY_PAUSE = 0xB3
KEYEVENTF_EXTENDEDKEY = 0x1
KEYEVENTF_KEYUP = 0x2


# COM objects that must never be garbage collected (see "volume" below)
_keepalive = []


def _init_com_thread():
    """Gives the volume thread its own COM (multi-threaded) setup."""
    import comtypes

    try:
        comtypes.CoInitializeEx(comtypes.COINIT_MULTITHREADED)
    except OSError:
        pass  # already initialised


def press_media_key(vk):
    user32 = ctypes.windll.user32
    user32.keybd_event(vk, 0, KEYEVENTF_EXTENDEDKEY, 0)
    user32.keybd_event(vk, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0)


class WindowsMedia:
    def __init__(self):
        self._manager = None
        self._ep = None
        self._pool = concurrent.futures.ThreadPoolExecutor(
            max_workers=1, thread_name_prefix="gorgi-volume", initializer=_init_com_thread)

    async def _session(self):
        if self._manager is None:
            self._manager = await MediaManager.request_async()
        return self._manager.get_current_session()

    async def get_info(self):
        """Returns {"title", "artist", "album", "playing", "pos", "dur"} for whatever is playing.
        pos and dur (seconds) are None when the app doesn't say."""
        empty = {"title": "", "artist": "", "album": "", "playing": False, "pos": None, "dur": None}
        try:
            session = await self._session()
            if session is None:
                return empty
            props = await session.try_get_media_properties_async()
            playback = session.get_playback_info()
            playing = playback.playback_status == PlaybackStatus.PLAYING
            pos, dur = self._timeline(session, playing)
            return {
                "title": props.title or "",
                "artist": props.artist or "",
                "album": props.album_title or "",
                "playing": playing,
                "pos": pos,
                "dur": dur,
            }
        except Exception as e:  # media apps come and go; never crash on it
            log.debug("media info failed: %s", e)
            self._manager = None
            return empty

    @staticmethod
    def _timeline(session, playing):
        """(seconds into the song, song length), or (None, None)"""
        try:
            t = session.get_timeline_properties()
            dur = (t.end_time - t.start_time).total_seconds()
            if dur <= 1:
                return None, None
            pos = (t.position - t.start_time).total_seconds()
            if playing and t.last_updated_time:
                # the position is as of the last update; add the time since then
                updated = t.last_updated_time
                if updated.tzinfo is None:
                    updated = updated.replace(tzinfo=datetime.timezone.utc)
                since = (datetime.datetime.now(datetime.timezone.utc) - updated).total_seconds()
                if 0 < since < 24 * 3600:
                    pos += since
            return max(0.0, min(pos, dur)), dur
        except Exception as e:
            log.debug("timeline failed: %s", e)
            return None, None

    async def get_art(self):
        """The song's picture as file bytes (PNG/JPEG), or None."""
        try:
            session = await self._session()
            if session is None:
                return None
            props = await session.try_get_media_properties_async()
            if props.thumbnail is None:
                return None
            from winrt.windows.storage.streams import Buffer, DataReader, InputStreamOptions

            stream = await props.thumbnail.open_read_async()
            size = int(stream.size)
            if not 0 < size < 20_000_000:
                return None
            buf = await stream.read_async(Buffer(size), size, InputStreamOptions.READ_AHEAD)
            try:
                return bytes(memoryview(buf))
            except TypeError:  # older winrt: read it out by hand
                reader = DataReader.from_buffer(buf)
                data = bytearray(buf.length)
                reader.read_bytes(data)
                return bytes(data)
        except Exception as e:
            log.info("couldn't get the album art: %s", e)
            return None

    async def command(self, action):
        """action: "playpause", "next" or "prev"."""
        try:
            session = await self._session()
            if session is not None:
                if action == "playpause":
                    ok = await session.try_toggle_play_pause_async()
                elif action == "next":
                    ok = await session.try_skip_next_async()
                else:
                    ok = await session.try_skip_previous_async()
                if ok:
                    return
        except Exception as e:
            log.debug("media session command failed: %s", e)
            self._manager = None
        # Fallback: press the keyboard media key
        key = {"playpause": VK_MEDIA_PLAY_PAUSE, "next": VK_MEDIA_NEXT_TRACK, "prev": VK_MEDIA_PREV_TRACK}[action]
        press_media_key(key)

    async def set_playing(self, play):
        """Explicit play or pause (used to pause music while you talk to Gorgi)."""
        try:
            session = await self._session()
            if session is not None:
                ok = await (session.try_play_async() if play else session.try_pause_async())
                if ok:
                    return
        except Exception as e:
            log.debug("play/pause failed: %s", e)
            self._manager = None
        info = await self.get_info()
        if info["playing"] != play:
            press_media_key(VK_MEDIA_PLAY_PAUSE)

    # ----- volume -----
    #
    # The Windows volume objects (COM) are created ONCE, on one dedicated
    # thread, and are never released. Creating them again every second and
    # letting Python's garbage collector release them from whatever thread
    # happened to run it crashed the whole app (access violation in Release).

    def _endpoint(self):
        """Runs on the volume thread only."""
        if self._ep is None:
            speakers = AudioUtilities.GetSpeakers()
            keep = [speakers]
            ep = getattr(speakers, "EndpointVolume", None)  # newer pycaw versions
            if ep is None:
                interface = speakers.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
                keep.append(interface)
                ep = cast(interface, POINTER(IAudioEndpointVolume))
            keep.append(ep)
            _keepalive.append(keep)  # never let the garbage collector release these
            self._ep = ep
        return self._ep

    def _on_volume_thread(self, fn):
        """Runs fn(endpoint) on the volume thread and returns its result."""

        def job():
            try:
                return fn(self._endpoint())
            except Exception:
                self._ep = None  # e.g. the default speakers changed: look them up again next time
                raise

        return self._pool.submit(job).result(timeout=3)

    def get_volume(self):
        """0..100, or 0 when muted."""

        def read(ep):
            if ep.GetMute():
                return 0
            return round(ep.GetMasterVolumeLevelScalar() * 100)

        try:
            return self._on_volume_thread(read)
        except Exception as e:
            log.debug("get volume failed: %s", e)
            return None

    def set_volume(self, value):
        value = max(0, min(100, int(value)))

        def write(ep):
            ep.SetMasterVolumeLevelScalar(value / 100.0, None)
            ep.SetMute(1 if value == 0 else 0, None)

        try:
            self._on_volume_thread(write)
        except Exception as e:
            log.warning("set volume failed: %s", e)
