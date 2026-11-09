"""Tests the "open YouTube Music and play" steps with a fake Windows. Run: python test_youtube_music.py"""

import asyncio

import youtube_music as ym


class FakeSession:
    def __init__(self, media):
        self.media = media

    async def try_play_async(self):
        self.media.playing = True
        return True


class FakeMedia:
    def __init__(self, has_session=False):
        self.playing = False
        self.has_session = has_session

    async def get_info(self):
        return {"playing": self.playing}

    async def _session(self):
        return FakeSession(self) if self.has_session else None


def run(window_at_start, has_session):
    media = FakeMedia(has_session)
    log = []
    state = {"window": 1 if window_at_start else None}

    def launch():
        log.append("launch")
        state["window"] = 7

    def press_space(hwnd):
        log.append(f"space {hwnd}")
        media.playing = True

    ym.find_window = lambda: state["window"]
    ym.launch = launch
    ym.press_space = press_space
    asyncio.run(ym.open_and_play(media, load_wait=1, timeout=3))
    return log, media.playing


assert run(False, False) == (["launch", "space 7"], True)
assert run(True, False) == (["space 1"], True)
assert run(False, True) == (["launch"], True)
print("youtube music: all good")
