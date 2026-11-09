"""Opens YouTube Music and starts it, for Play when no music app is open.

- If a YouTube Music window is already there (the installed app, or a browser
  tab that's showing), that one is used.
- Otherwise the installed YouTube Music app is started (its Start menu
  shortcut, from Chrome or Edge), or music.youtube.com in the default browser.
- YouTube Music doesn't start playing by itself when it opens, so once the
  page has loaded its window is brought to the front and Space (YouTube Music's
  play key) is pressed.
"""

import asyncio
import ctypes
import glob
import logging
import os
import time

log = logging.getLogger("gorgi.music")

URL = "https://music.youtube.com/"
TITLE = "YouTube Music"
VK_SPACE = 0x20
KEYEVENTF_KEYUP = 0x2
SW_RESTORE = 9


def find_shortcut():
    """The Start menu shortcut of the installed YouTube Music app, or None."""
    roots = [os.path.join(os.environ.get(v, ""), "Microsoft", "Windows", "Start Menu", "Programs")
             for v in ("APPDATA", "PROGRAMDATA") if os.environ.get(v)]
    for root in roots:
        found = sorted(glob.glob(os.path.join(root, "**", "YouTube Music*.lnk"), recursive=True))
        if found:
            return found[0]
    return None


def find_window():
    """A visible window with "YouTube Music" in its title, or None."""
    user32 = ctypes.windll.user32
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def each(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            n = user32.GetWindowTextLengthW(hwnd)
            if n:
                buf = ctypes.create_unicode_buffer(n + 1)
                user32.GetWindowTextW(hwnd, buf, n + 1)
                if TITLE.lower() in buf.value.lower():
                    found.append(hwnd)
                    return False
        return True

    user32.EnumWindows(each, 0)
    return found[0] if found else None


def press_space(hwnd):
    """Brings the window to the front and presses Space in it."""
    user32 = ctypes.windll.user32
    if user32.IsIconic(hwnd):
        user32.ShowWindow(hwnd, SW_RESTORE)
    # Windows only lets the app that has the keyboard move focus: a tap of Alt
    # first makes SetForegroundWindow work from the background.
    user32.keybd_event(0x12, 0, 0, 0)
    user32.keybd_event(0x12, 0, KEYEVENTF_KEYUP, 0)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    user32.keybd_event(VK_SPACE, 0, 0, 0)
    user32.keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0)


def launch():
    shortcut = find_shortcut()
    if shortcut:
        log.info("opening the YouTube Music app: %s", shortcut)
        os.startfile(shortcut)
    else:
        log.info("no YouTube Music app found: opening %s in the browser", URL)
        os.startfile(URL)


async def _playing(media):
    info = await media.get_info()
    return bool(info.get("playing"))


async def open_and_play(media, load_wait=5.0, timeout=30.0):
    """media: WindowsMedia (get_info, _session). Never raises."""
    try:
        hwnd = await asyncio.to_thread(find_window)
        if hwnd is None:
            await asyncio.to_thread(launch)
        else:
            log.info("YouTube Music is open but not playing")
        # Wait for its window, then a few seconds for the page to load
        start = time.monotonic()
        while hwnd is None and time.monotonic() - start < timeout:
            await asyncio.sleep(1)
            hwnd = await asyncio.to_thread(find_window)
        if hwnd is None:
            log.warning("YouTube Music didn't open within %d s", timeout)
            return
        for _ in range(int(load_wait)):
            await asyncio.sleep(1)
            if await _playing(media):
                return
        # The page has a media session already (it played before): just play it
        session = await media._session()
        if session is not None:
            try:
                if await session.try_play_async():
                    log.info("YouTube Music: playing")
                    return
            except Exception as e:
                log.debug("play failed: %s", e)
        log.info("YouTube Music: pressing Space to play")
        await asyncio.to_thread(press_space, hwnd)
        for _ in range(5):
            await asyncio.sleep(1)
            if await _playing(media):
                log.info("YouTube Music: playing")
                return
        log.info("YouTube Music opened, but it didn't start playing")
    except Exception as e:
        log.warning("couldn't open YouTube Music: %s", e)
