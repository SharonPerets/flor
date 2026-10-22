"""Windows media and volume control for Gorgi Helper.

- Song info comes from the Windows media session (the same info shown in the
  volume pop-up), so it works with Spotify, browsers, the Media Player app...
- Play/pause/next/previous use the media session, with the keyboard media keys
  as a fallback.
- Volume is the PC's master volume.
"""

import ctypes
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


def press_media_key(vk):
    user32 = ctypes.windll.user32
    user32.keybd_event(vk, 0, KEYEVENTF_EXTENDEDKEY, 0)
    user32.keybd_event(vk, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0)


class WindowsMedia:
    def __init__(self):
        self._manager = None

    async def _session(self):
        if self._manager is None:
            self._manager = await MediaManager.request_async()
        return self._manager.get_current_session()

    async def get_info(self):
        """Returns {"title", "artist", "playing"} for whatever is playing."""
        empty = {"title": "", "artist": "", "playing": False}
        try:
            session = await self._session()
            if session is None:
                return empty
            props = await session.try_get_media_properties_async()
            playback = session.get_playback_info()
            return {
                "title": props.title or "",
                "artist": props.artist or "",
                "playing": playback.playback_status == PlaybackStatus.PLAYING,
            }
        except Exception as e:  # media apps come and go; never crash on it
            log.debug("media info failed: %s", e)
            self._manager = None
            return empty

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

    def _endpoint(self):
        speakers = AudioUtilities.GetSpeakers()
        endpoint = getattr(speakers, "EndpointVolume", None)  # newer pycaw versions
        if endpoint is not None:
            return endpoint
        interface = speakers.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
        return cast(interface, POINTER(IAudioEndpointVolume))

    def get_volume(self):
        """0..100, or 0 when muted."""
        try:
            ep = self._endpoint()
            if ep.GetMute():
                return 0
            return round(ep.GetMasterVolumeLevelScalar() * 100)
        except Exception as e:
            log.debug("get volume failed: %s", e)
            return None

    def set_volume(self, value):
        try:
            ep = self._endpoint()
            value = max(0, min(100, int(value)))
            ep.SetMasterVolumeLevelScalar(value / 100.0, None)
            ep.SetMute(1 if value == 0 else 0, None)
        except Exception as e:
            log.warning("set volume failed: %s", e)
