"""Your YouTube Music account, for Gorgi: search, your Liked songs, like and dislike.

Uses ytmusicapi (a free, unofficial add-on). Searching works without signing in.
Your Liked songs and like / dislike need your YouTube Music sign-in, copied once
from your browser in Gorgi Helper's Settings ("Connect YouTube Music"). That copy
is kept only in youtube-music.json in the helper's settings folder.

Playing is done by the YouTube Music app on the PC (see youtube_music.py): this
module only works out which link to open.
"""

import logging
import random
import re
import threading
import time
from pathlib import Path

log = logging.getLogger("gorgi.ytmusic")

AUTH_FILE = "youtube-music.json"
LIKED_LIMIT = 2000         # songs read from your Liked songs
REFRESH_SECONDS = 5 * 60   # the Liked songs list is read again this often
WATCH = "https://music.youtube.com/watch?"

PROMPT = """You can also play music from YouTube Music on your friend's PC. Put a tag at the END of your reply:
[music: band Muse]  plays a band or singer (their songs)
[music: song Bohemian Rhapsody by Queen]  plays a song (the song and, if you know it, "by" the artist)
[music: album Absolution by Muse]  plays an album
[music: play chill jazz]  when it's not clear (a mood, a style, anything else)
[music: liked]  plays your friend's Liked songs; [music: shuffle liked] plays them from a random song
[music: like]  likes the song that is playing now; [music: dislike] dislikes it (and skips it)
Use these when your friend asks to play, put on or listen to something, or says they love or hate this song.
For "play" / "resume" with nothing named, use [action:play] instead. Say it in a few words, like "Here's some Muse!"."""

TAG_RE = re.compile(r"\[music\s*:\s*([^\]]+)\]", re.IGNORECASE)

NOT_CONNECTED = ("I can't see your YouTube Music account yet. Connect it in Gorgi Helper's settings on your PC.")


def extract(reply):
    """Takes the music tags out of a reply. Returns (reply without them, [("music", value)])."""
    tags = [("music", m.group(1).strip()) for m in TAG_RE.finditer(reply)]
    return TAG_RE.sub("", reply), tags


# ---------------------------------------------------------------- matching song names


def _plain(text):
    """'Hysteria (Remastered) [feat. X]' -> 'hysteria'"""
    text = (text or "").lower()
    text = re.sub(r"[\(\[].*?[\)\]]", " ", text)           # (Remastered), [Official Video]
    text = re.sub(r"\s(feat|ft|featuring)\.?\s.*$", " ", text)
    text = re.sub(r"[^\w\s]", " ", text, flags=re.UNICODE)  # keeps Hebrew letters
    return " ".join(text.split())


def _artist_words(artist):
    """'Muse - Topic' / 'MuseVEVO' / 'Muse & Friends' -> {'muse', ...}"""
    a = (artist or "").lower().replace(" - topic", "")
    a = re.sub(r"vevo$", "", a.strip())
    parts = re.split(r"\s*(?:,|&|\bx\b|\band\b|\bfeat\.?|\bft\.?)\s*", a)
    return {_plain(p) for p in parts if _plain(p)}


def _names(track):
    return {_plain(a.get("name")) for a in (track.get("artists") or []) if a.get("name")}


def find_liked(liked, title, artist):
    """The Liked songs entry for the song playing now, or None."""
    t = _plain(title)
    if not t:
        return None
    same_title = [s for s in liked if _plain(s.get("title")) == t]
    if not same_title:
        return None
    wanted = _artist_words(artist)
    for s in same_title:
        names = _names(s)
        if not wanted or not names or wanted & names or any(w in n or n in w for w in wanted for n in names):
            return s
    return None


# ---------------------------------------------------------------- links


def song_url(video_id):
    """A song, then more like it (YouTube Music's song radio)."""
    return f"{WATCH}v={video_id}&list=RDAMVM{video_id}"


def playlist_url(playlist_id, video_id=None):
    return f"{WATCH}v={video_id}&list={playlist_id}" if video_id else f"{WATCH}list={playlist_id}"


EXTRA_WORDS = ("live", "remix", "acoustic", "version", "karaoke", "instrumental", "cover", "edit", "mix",
               "anniversary", "deluxe", "remaster", "demo", "sped up", "slowed")


def split_by(query):
    """'Hysteria by Muse' -> ('Hysteria', 'Muse'); 'Muse' -> ('Muse', '')"""
    m = re.match(r"^(.*\S)\s+by\s+(\S.*)$", (query or "").strip(), re.IGNORECASE)
    return (m.group(1), m.group(2)) if m else ((query or "").strip(), "")


def best(results, title, by):
    """The result that fits best: same title, the right artist, not a live / remix version you didn't ask for."""
    t, wanted = _plain(title), _artist_words(by)
    asked = (title + " " + by).lower()

    def score(r):
        name = r.get("title") or ""
        names = _names(r) or ({_plain(r.get("artist"))} if r.get("artist") else set())
        s = 0
        if _plain(name) == t:
            s += 4
        elif t and t in _plain(name):
            s += 1
        if wanted and (wanted & names or any(w in n or n in w for w in wanted for n in names)):
            s += 3
        if any(w in name.lower() and w not in asked for w in EXTRA_WORDS):
            s -= 2
        return s

    return max(results, key=score) if results else None  # max keeps the first of equal ones


def choose(results, query):
    """From search results, what to play: (kind, name, item). kind: artist / song / album, or None."""
    q = _plain(query)
    top = [r for r in results if r.get("category") == "Top result"] or results[:1]
    for r in top + results:
        kind = r.get("resultType")
        if kind == "artist" and r.get("browseId"):
            return "artist", r.get("artist") or r.get("title") or query, r
        if kind in ("song", "video") and r.get("videoId"):
            names = ", ".join(a.get("name", "") for a in (r.get("artists") or [])[:1])
            return "song", (r.get("title") or query) + (f" by {names}" if names else ""), r
        if kind == "album" and r.get("browseId"):
            return "album", r.get("title") or query, r
    log.info("nothing to play for %r (%d results)", q, len(results))
    return None, None, None


# ---------------------------------------------------------------- the account


class Account:
    def __init__(self, folder):
        self.path = Path(folder) / AUTH_FILE
        self._api = None
        self._api_stamp = None   # the sign-in file's time when _api was made
        self._anon = None
        self._lock = threading.Lock()
        self.liked = []          # [{"videoId", "title", "artists": [{"name"}]}], newest first
        self.liked_count = 0
        self.liked_at = 0.0      # when the list was last read (time.monotonic)
        self.problem = None      # last problem with the sign-in, or None

    # -- connecting

    def connected(self):
        return self.path.exists()

    def _signed_in(self):
        """YTMusic with your sign-in (made again when the file changes), or None."""
        if not self.path.exists():
            self._api = None
            return None
        stamp = self.path.stat().st_mtime
        if self._api is None or stamp != self._api_stamp:
            from ytmusicapi import YTMusic

            self._api = YTMusic(str(self.path))
            self._api_stamp = stamp
            self.liked_at = 0.0  # read the list again with the new sign-in
        return self._api

    def _anyone(self):
        """YTMusic without a sign-in (search works the same)."""
        if self._anon is None:
            from ytmusicapi import YTMusic

            self._anon = YTMusic()
        return self._anon

    def _search_api(self):
        try:
            return self._signed_in() or self._anyone()
        except Exception as e:
            log.warning("YouTube Music sign-in unusable (%s): searching without it", e)
            return self._anyone()

    # -- liked songs

    def refresh_liked(self, force=False):
        """Reads your Liked songs (every few minutes). Returns True if it worked."""
        with self._lock:
            if not force and time.monotonic() - self.liked_at < REFRESH_SECONDS:
                return True
            api = self._signed_in()
            if api is None:
                self.liked, self.liked_count = [], 0
                return False
            try:
                data = api.get_liked_songs(limit=LIKED_LIMIT)
            except Exception as e:
                self.problem = f"{type(e).__name__}: {e}"
                log.warning("couldn't read your Liked songs: %s", self.problem)
                self.liked_at = time.monotonic() - REFRESH_SECONDS + 60  # try again in a minute
                return False
            tracks = [t for t in (data.get("tracks") or []) if t.get("videoId")]
            self.liked = [{"videoId": t["videoId"], "title": t.get("title") or "",
                           "artists": [{"name": a.get("name", "")} for a in (t.get("artists") or [])]}
                          for t in tracks]
            self.liked_count = data.get("trackCount") or len(self.liked)
            self.liked_at = time.monotonic()
            self.problem = None
            log.info("Liked songs: %d (read %d)", self.liked_count, len(self.liked))
            return True

    def is_liked(self, title, artist):
        """True / False for the song playing now, or None when we can't tell (not connected)."""
        if not self.liked_at or not title:
            return None
        return find_liked(self.liked, title, artist) is not None

    def liked_url(self, shuffle=False):
        if shuffle and self.liked:
            return playlist_url("LM", random.choice(self.liked)["videoId"])
        return playlist_url("LM")

    # -- finding and playing

    def find_song_id(self, title, artist):
        """The video id of a song (playing now): from your Liked songs, else a search."""
        hit = find_liked(self.liked, title, artist)
        if hit:
            return hit["videoId"]
        results = self._search_api().search(f"{title} {artist}".strip(), filter="songs", limit=5)
        songs = [r for r in results if r.get("videoId")]
        t = _plain(title)
        exact = [r for r in songs if _plain(r.get("title")) == t]
        pick = (exact or songs or [None])[0]
        return pick["videoId"] if pick else None

    def url_for(self, query, kind=None):
        """What to open for "play <query>": (link, name for Gorgi to say), or (None, None).
        kind: "band", "song", "album", or None (work it out)."""
        api = self._search_api()
        title, by = split_by(query)
        if kind in ("band", None) and not by:
            artists = [r for r in api.search(title, filter="artists", limit=5) if r.get("browseId")]
            exact = [r for r in artists if _plain(r.get("artist") or r.get("title")) == _plain(title)]
            pick = (exact or (artists if kind == "band" else []) or [None])[0]
            if pick:
                url = self._artist_url(api, pick["browseId"])
                if url:
                    return url, pick.get("artist") or title
        if kind == "album":
            albums = [r for r in api.search(query, filter="albums", limit=10) if r.get("browseId")]
            pick = best(albums, title, by)
            if pick:
                try:
                    album = api.get_album(pick["browseId"])
                    if album.get("audioPlaylistId"):
                        return playlist_url(album["audioPlaylistId"]), pick.get("title") or title
                except Exception as e:
                    log.info("album %s: %s", pick["browseId"], e)
        songs = [r for r in api.search(query, filter="songs", limit=10) if r.get("videoId")]
        pick = best(songs, title, by)
        if pick and (kind == "song" or _plain(pick.get("title")) == _plain(title) or by):
            names = ", ".join(a.get("name", "") for a in (pick.get("artists") or [])[:1])
            return song_url(pick["videoId"]), (pick.get("title") or title) + (f" by {names}" if names else "")
        # Not clear: whatever YouTube Music puts on top
        kind2, name, item = choose(api.search(query, limit=10), query)
        log.info("search %r -> %s %s", query, kind2, name)
        if kind2 == "song":
            return song_url(item["videoId"]), name
        if kind2 == "artist":
            url = self._artist_url(api, item["browseId"])
            if url:
                return url, name
        if kind2 == "album":
            try:
                album = api.get_album(item["browseId"])
                if album.get("audioPlaylistId"):
                    return playlist_url(album["audioPlaylistId"]), name
            except Exception as e:
                log.info("album %s: %s", item["browseId"], e)
        if pick:
            return song_url(pick["videoId"]), pick.get("title") or title
        return None, None

    @staticmethod
    def _artist_url(api, browse_id):
        try:
            artist = api.get_artist(browse_id)
        except Exception as e:
            log.info("artist %s: %s", browse_id, e)
            return None
        for key in ("shuffleId", "radioId"):  # the artist's own Shuffle button, else their radio
            if artist.get(key):
                return playlist_url(artist[key])
        songs = (artist.get("songs") or {}).get("results") or []
        if songs and songs[0].get("videoId"):
            return song_url(songs[0]["videoId"])
        return None

    # -- rating

    def rate(self, title, artist, rating):
        """rating: LIKE / DISLIKE / INDIFFERENT for the song playing now. Returns None, or what went wrong."""
        api = self._signed_in()
        if api is None:
            return NOT_CONNECTED
        try:
            video_id = self.find_song_id(title, artist)
            if not video_id:
                return "I couldn't find this song on YouTube Music."
            api.rate_song(video_id, rating)
        except Exception as e:
            log.warning("rating %r failed: %s", title, e)
            return "YouTube Music didn't take that. Maybe reconnect it in Gorgi Helper's settings."
        log.info("rated %r by %r: %s", title, artist, rating)
        with self._lock:  # keep the list right at once; the next read confirms it
            if rating == "LIKE":
                if not find_liked(self.liked, title, artist):
                    self.liked.insert(0, {"videoId": video_id, "title": title, "artists": [{"name": artist}]})
            else:
                self.liked = [s for s in self.liked
                              if s["videoId"] != video_id and not find_liked([s], title, artist)]
            self.liked_at = self.liked_at or time.monotonic()
        return None


def connect(folder, headers_raw):
    """For Settings: checks the copied request headers and saves them. Returns (ok, message)."""
    from ytmusicapi import YTMusic, setup

    text = (headers_raw or "").strip()
    if not text:
        return False, "Paste the copied request headers first."
    path = Path(folder) / AUTH_FILE
    trial = path.with_suffix(".new.json")
    try:
        setup(filepath=str(trial), headers_raw=text)
    except Exception as e:
        log.info("YouTube Music headers refused: %s", e)
        missing = "cookie" in str(e).lower() or "authuser" in str(e).lower()
        return False, ("That doesn't look like the right request. In the Network tab, pick a line called "
                       "\"browse\" (after clicking Library) and copy its request headers." if missing
                       else f"Couldn't read that: {e}")
    try:
        data = YTMusic(str(trial)).get_liked_songs(limit=1)
        count = data.get("trackCount")
    except Exception as e:
        trial.unlink(missing_ok=True)
        log.info("YouTube Music sign-in check failed: %s", e)
        return False, "YouTube Music didn't accept it. Make sure you're signed in there, then copy it again."
    trial.replace(path)
    return True, f"Connected: {count} liked songs." if count is not None else "Connected."


def disconnect(folder):
    (Path(folder) / AUTH_FILE).unlink(missing_ok=True)
