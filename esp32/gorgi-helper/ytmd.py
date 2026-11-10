"""YouTube Music Desktop App (ytmdesktop, "YTMD"): plays songs in that app instead of a browser tab.

The app has a small built-in server for helper programs ("Companion server", port 9863,
https://github.com/ytmdesktop/ytmdesktop/wiki/v2-%E2%80%90-Companion-Server-API-v1).
Gorgi Helper asks it once for permission: the app shows a code and an Allow button
(it needs Settings > Integrations > "Companion server" and "Enable companion authorization").
The permission (a token) is kept in ytmd.json in the helper's settings folder.
Then "play X" sends the app {"command": "changeVideo", "data": {"videoId", "playlistId"}}.
"""

import glob
import json
import logging
import os
import subprocess
import time
from pathlib import Path
from urllib.parse import parse_qs, urlparse

log = logging.getLogger("gorgi.ytmd")

BASE = "http://127.0.0.1:9863"  # (not "localhost": on Windows that can be IPv6, where it doesn't listen)
APP_ID = "gorgihelper"
TOKEN_FILE = "ytmd.json"
CREATE_NO_WINDOW = 0x08000000

folder = None                       # the helper's settings folder (set by gorgi_helper)
notify = lambda title, text: None   # a pop-up on the PC  # noqa: E731
_asked_at = 0.0                     # when we last asked for permission (not again for a while)
_told = {}                          # message -> when it was last shown
problem = None                      # why the last play didn't work (shown on Gorgi), or None

TURN_ON = ("To play songs in YouTube Music Desktop App, open its Settings > Integrations and turn on "
           "\"Companion server\" and \"Enable companion authorization\". Then ask Gorgi again.")


def setup(settings_folder, on_notify):
    global folder, notify
    folder = Path(settings_folder)
    notify = on_notify


def _tell(text, every=300):
    """A pop-up, at most once every few minutes for the same text."""
    global problem
    problem = text
    if time.monotonic() - _told.get(text, -1e9) >= every:
        _told[text] = time.monotonic()
        try:
            notify("Gorgi", text)
        except Exception:
            pass


def program():
    """The app's program (YouTube Music Desktop App.exe), or None if it isn't installed."""
    local = os.environ.get("LOCALAPPDATA", "")
    for path in (os.path.join(local, "Programs", "youtube-music-desktop-app", "YouTube Music Desktop App.exe"),
                 os.path.join(local, "youtube_music_desktop_app", "YouTube Music Desktop App.exe")):
        if os.path.isfile(path):
            return path
    roots = [os.path.join(os.environ.get(v, ""), "Microsoft", "Windows", "Start Menu", "Programs")
             for v in ("APPDATA", "PROGRAMDATA") if os.environ.get(v)]
    for root in roots:
        found = sorted(glob.glob(os.path.join(root, "**", "YouTube Music Desktop*.lnk"), recursive=True))
        if found:
            return found[0]  # a shortcut starts it just as well
    return None


def installed():
    return program() is not None


def _http():
    import httpx

    return httpx


def running():
    """True when the app's Companion server answers."""
    try:
        return _http().get(BASE + "/metadata", timeout=1.5).status_code == 200
    except Exception:
        return False


def app_open():
    """True when YouTube Music Desktop App is running."""
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq YouTube Music Desktop App.exe", "/NH"],
                             capture_output=True, text=True, timeout=5, creationflags=CREATE_NO_WINDOW).stdout
        return "youtube music desktop app" in out.lower()
    except Exception:
        return False


def start(wait=25.0):
    """Starts the app if it isn't running. True once its server answers."""
    if running():
        return True
    if app_open():  # open, but its Companion server is off
        log.info("YouTube Music Desktop App is open but its Companion server doesn't answer")
        return False
    path = program()
    if not path:
        return False
    log.info("starting YouTube Music Desktop App")
    try:
        os.startfile(path)
    except Exception as e:
        log.warning("couldn't start YouTube Music Desktop App: %s", e)
        return False
    end = time.monotonic() + wait
    while time.monotonic() < end:
        time.sleep(1)
        if running():
            time.sleep(2)  # let it load its page
            return True
    log.info("YouTube Music Desktop App's Companion server didn't answer")
    return False


def _token_path():
    return folder / TOKEN_FILE if folder else None


def _token():
    path = _token_path()
    try:
        return json.loads(path.read_text()).get("token") if path and path.exists() else None
    except Exception:
        return None


def _save_token(token):
    path = _token_path()
    if path:
        if token:
            path.write_text(json.dumps({"token": token}))
        elif path.exists():
            path.unlink()


def authorize():
    """Asks the app for permission (you click Allow in it). Returns the token, or None."""
    global _asked_at
    if time.monotonic() - _asked_at < 60:
        return None
    _asked_at = time.monotonic()
    http = _http()
    try:
        r = http.post(BASE + "/api/v1/auth/requestcode", timeout=5,
                      json={"appId": APP_ID, "appName": "Gorgi Helper", "appVersion": "1.0.0"})
        if r.status_code != 200:
            log.info("YTMD permission code refused: %s %s", r.status_code, r.text[:200])
            _tell(TURN_ON)
            return None
        code = r.json().get("code")
        log.info("YTMD: asking for permission (code %s)", code)
        _tell(f"YouTube Music Desktop App is asking if Gorgi may control it (code {code}). Click Allow there.", 0)
        r = http.post(BASE + "/api/v1/auth/request", timeout=40, json={"appId": APP_ID, "code": code})
        if r.status_code != 200:
            log.info("YTMD permission not given: %s %s", r.status_code, r.text[:200])
            _tell("Gorgi didn't get permission from YouTube Music Desktop App. Ask Gorgi to play something to try again.")
            return None
        token = r.json().get("token")
        _save_token(token)
        log.info("YTMD: permission given")
        return token
    except Exception as e:
        log.info("YTMD permission failed: %s", e)
        return None


def command(name, data=None, ask=True):
    """Sends a command to the app. True if it took it."""
    token = _token() or (authorize() if ask else None)
    if not token:
        return False
    body = {"command": name}
    if data is not None:
        body["data"] = data
    try:
        r = _http().post(BASE + "/api/v1/command", json=body, headers={"Authorization": token}, timeout=5)
    except Exception as e:
        log.info("YTMD %s failed: %s", name, e)
        return False
    if r.status_code in (401, 403):  # the permission was taken back in the app
        log.info("YTMD refused the token (%s): asking again", r.status_code)
        _save_token(None)
        return command(name, data, ask=False) if ask and authorize() else False
    if r.status_code >= 300:
        log.info("YTMD %s: %s %s", name, r.status_code, r.text[:200])
        return False
    return True


def link_parts(url):
    """'https://music.youtube.com/watch?v=X&list=Y' -> ('X', 'Y')"""
    q = parse_qs(urlparse(url).query)
    return (q.get("v") or [None])[0], (q.get("list") or [None])[0]


def state():
    """What the app is playing (its /state), or None."""
    token = _token()
    if not token:
        return None
    try:
        r = _http().get(BASE + "/api/v1/state", headers={"Authorization": token}, timeout=3)
        return r.json() if r.status_code == 200 else None
    except Exception:
        return None


def playing_id(title):
    """The video id of the song playing in the app, if its name is title (exact song for lyrics / radio)."""
    if not title or not installed():
        return None
    video = (state() or {}).get("video") or {}
    if video.get("id") and (video.get("title") or "").strip().lower() == title.strip().lower():
        return video["id"]
    return None


def play(url):
    """Plays a YouTube Music link in YouTube Music Desktop App. True if the app took it."""
    global problem
    problem = None
    video_id, playlist_id = link_parts(url)
    if not (video_id or playlist_id):
        return False
    if not start():
        if installed():
            _tell(TURN_ON)
        return False
    ok = command("changeVideo", {"videoId": video_id, "playlistId": playlist_id})
    if not ok and not problem:
        problem = "YouTube Music Desktop App didn't take the song. Ask Gorgi to play it again to retry."
    log.info("YTMD: play %s / %s -> %s", video_id, playlist_id, "ok" if ok else "not taken")
    return ok
