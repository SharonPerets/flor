"""Tests "Play opens YouTube Music" with a fake Windows. Run: python test_youtube_music.py"""

import asyncio
import importlib
import os
import time
import types

import youtube_music as ym


class FakeMedia:
    """Windows' media controls: a song shows up `loads_after` s after opening;
    it plays by itself only if `autoplay`, otherwise after play is pressed."""

    def __init__(self, loads_after=2, autoplay=False, never_loads=False):
        self.opened_at = None
        self.loads_after, self.autoplay, self.never_loads = loads_after, autoplay, never_loads
        self.playing = False
        self.presses = 0

    def loaded(self):
        return (not self.never_loads and self.opened_at is not None
                and time.monotonic() - self.opened_at >= self.loads_after)

    async def get_info(self):
        if self.loaded() and self.autoplay:
            self.playing = True
        return {"playing": self.playing}

    async def _session(self):
        return object() if self.loaded() else None

    async def play_session(self):
        self.presses += 1
        self.playing = True
        return True


def run(media, **kw):
    opened = []

    async def go():
        def launch(url):
            opened.append(url)
            media.opened_at = time.monotonic()

        ym.launch = launch
        ok = await ym.open_and_play(media, **kw)
        return ok

    return asyncio.run(go()), opened


# Plays by itself once loaded: no play press needed
m = FakeMedia(autoplay=True)
assert run(m, timeout=6, play_after=2) == (True, [ym.PLAY_URL]) and m.presses == 0
# The browser blocks playing by itself: play is pressed once the song is there
m = FakeMedia(autoplay=False)
assert run(m, timeout=8, play_after=1) == (True, [ym.PLAY_URL]) and m.presses == 1
# Never loads: gives up, no presses
m = FakeMedia(never_loads=True)
assert run(m, timeout=3, play_after=1) == (False, [ym.PLAY_URL]) and m.presses == 0


# launch(): the installed app is opened on the link; otherwise the browser
started = []
os.startfile = lambda u: started.append("browser " + u)
real = importlib.reload(ym)
real.find_shortcut = lambda: r"C:\Start Menu\YouTube Music.lnk"
real.read_shortcut = lambda p: (r"C:\chrome_proxy.exe", "--profile-directory=Default --app-id=cinhim")
real.subprocess = types.SimpleNamespace(Popen=lambda cmd, **kw: started.append(cmd), CREATE_NO_WINDOW=0)
real.launch()
assert started[-1] == ('"C:\\chrome_proxy.exe" --profile-directory=Default --app-id=cinhim '
                       '--app-launch-url-for-shortcuts-menu-item=' + real.PLAY_URL), started
real.find_shortcut = lambda: None
real.launch()
assert started[-1] == "browser " + real.PLAY_URL
print("youtube music: all good")
