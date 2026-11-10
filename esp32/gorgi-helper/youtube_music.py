"""Opens YouTube Music and starts it, for Play when no music app is playing.

- YouTube Music has no song loaded when it opens fresh, so pressing play does
  nothing. So it is opened straight on a "play this" link: your Liked songs.
- The installed YouTube Music app (a Chrome or Edge app with a Start menu
  shortcut) is opened on that link; without the app, the default browser is.
- Once a song is loaded it is started through Windows' media controls (no key
  presses into whatever window happens to be in front).
"""

import asyncio
import glob
import logging
import os
import subprocess
import time

log = logging.getLogger("gorgi.music")

TITLE = "YouTube Music"
PLAY_URL = "https://music.youtube.com/watch?list=LM"  # your Liked songs
CREATE_NO_WINDOW = 0x08000000
APP_URL_SWITCH = "--app-launch-url-for-shortcuts-menu-item="  # Chrome/Edge: open the app on this link


def find_shortcut():
    """The Start menu shortcut of the installed YouTube Music app, or None."""
    roots = [os.path.join(os.environ.get(v, ""), "Microsoft", "Windows", "Start Menu", "Programs")
             for v in ("APPDATA", "PROGRAMDATA") if os.environ.get(v)]
    for root in roots:
        found = sorted(glob.glob(os.path.join(root, "**", "YouTube Music*.lnk"), recursive=True))
        if found:
            return found[0]
    return None


def read_shortcut(path):
    """(program, arguments) of a Windows shortcut, or (None, None)."""
    script = ("$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:GORGI_LNK); "
              "Write-Output $s.TargetPath; Write-Output $s.Arguments")
    try:
        out = subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
            capture_output=True, text=True, timeout=10, creationflags=CREATE_NO_WINDOW,
            env=dict(os.environ, GORGI_LNK=path)).stdout.splitlines()
        if out and out[0].strip():
            return out[0].strip(), (out[1].strip() if len(out) > 1 else "")
    except Exception as e:
        log.info("couldn't read the YouTube Music shortcut: %s", e)
    return None, None


def find_windows():
    """Visible windows with "YouTube Music" in their title (the app, or a browser on it)."""
    import ctypes

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
        return True

    user32.EnumWindows(each, 0)
    return found


def find_window():
    """A visible YouTube Music window, or None (used by the self-test)."""
    found = find_windows()
    return found[0] if found else None


BROWSER_NAMES = ("google chrome", "microsoft edge", "mozilla firefox", "brave", "opera", "vivaldi")


def window_title(hwnd):
    import ctypes

    user32 = ctypes.windll.user32
    n = user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.GetWindowTextW(hwnd, buf, n + 1)
    return buf.value


def is_app_window(hwnd):
    """The YouTube Music app's own window (not a whole browser window, which may hold other tabs)."""
    title = window_title(hwnd).lower()
    return not any(name in title for name in BROWSER_NAMES)


def close_window(hwnd):
    import ctypes

    ctypes.windll.user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE: like clicking its X


def launch(url=PLAY_URL):
    """Opens YouTube Music on url: in the installed app if there is one, else in the browser."""
    shortcut = find_shortcut()
    if shortcut:
        program, args = read_shortcut(shortcut)
        if program and "--app-id=" in (args or ""):
            log.info("opening the YouTube Music app on %s", url)
            subprocess.Popen(f'"{program}" {args} {APP_URL_SWITCH}{url}', creationflags=CREATE_NO_WINDOW)
            return
        log.info("YouTube Music shortcut %s isn't a browser app (%s %s): using the browser", shortcut,
                 program, args)
    log.info("opening %s in the browser", url)
    os.startfile(url)


async def _playing(media):
    info = await media.get_info()
    return bool(info.get("playing"))


async def open_and_play(media, url=PLAY_URL, timeout=40.0, play_after=5.0):
    """media: WindowsMedia (get_info, _session). Opens YouTube Music on url and makes
    sure it plays. Never raises."""
    try:
        await asyncio.to_thread(launch, url)
        start = time.monotonic()
        loaded_at = None  # when a song first showed up in Windows' media controls
        nudged = 0
        while time.monotonic() - start < timeout:
            await asyncio.sleep(1)
            if await _playing(media):
                log.info("YouTube Music: playing after %.0f s", time.monotonic() - start)
                return True
            if await media._session() is None:
                continue
            now = time.monotonic()
            if loaded_at is None:
                loaded_at = now
            # The browser may block playing by itself: give it a few seconds, then press play
            if now - loaded_at >= play_after * (nudged + 1) and nudged < 3:
                nudged += 1
                log.info("YouTube Music: loaded but not playing, pressing play (%d)", nudged)
                await media.play_session()
        log.warning("YouTube Music didn't start playing within %d s", timeout)
    except Exception as e:
        log.warning("couldn't open YouTube Music: %s", e)
    return False


async def play_url(media, url, timeout=40.0, play_after=5.0):
    """Plays a YouTube Music link (a song, a band, your Liked songs) and makes sure it plays.

    The app opens a new window for a link, so the old YouTube Music window is paused
    first and closed once the new one is there (two players would fight over the
    media keys). Never raises. Returns True when it plays."""
    try:
        old = set(await asyncio.to_thread(find_windows))
        if old and (await media.get_info()).get("playing"):
            await media.set_playing(False)
        await asyncio.to_thread(launch, url)
        start = time.monotonic()
        fresh = not old  # True once the link's window is there
        loaded_at = None
        nudged = 0
        while time.monotonic() - start < timeout:
            await asyncio.sleep(1)
            if not fresh:
                new = [h for h in await asyncio.to_thread(find_windows) if h not in old]
                if new:
                    fresh = True
                    for hwnd in old:
                        if is_app_window(hwnd):  # never a browser window with your other tabs
                            log.info("closing the old YouTube Music window")
                            close_window(hwnd)
                    continue
                if time.monotonic() - start < 12:
                    continue  # still waiting for the new window
                fresh = True  # no new window (a browser tab, or the app reused its window)
            if await _playing(media):
                log.info("YouTube Music: playing the link after %.0f s", time.monotonic() - start)
                return True
            if await media._session() is None:
                continue
            now = time.monotonic()
            if loaded_at is None:
                loaded_at = now
            if now - loaded_at >= play_after * (nudged + 1) and nudged < 3:
                nudged += 1
                log.info("YouTube Music: loaded but not playing, pressing play (%d)", nudged)
                await media.play_session()
        log.warning("YouTube Music didn't start playing the link within %d s", timeout)
    except Exception as e:
        log.warning("couldn't play %s: %s", url, e)
    return False
