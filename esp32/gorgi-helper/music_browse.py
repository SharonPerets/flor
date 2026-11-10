"""The Browse view on Gorgi's Music screen (since 4.8.19): your Liked songs, search,
an artist's albums and songs, an album's songs, mood chips, song radio and lyrics.

Gorgi sends {"type": "music", "do": ...}:
  home                      -> your Liked songs list (also sent again when it changes)
  search {q}                -> songs / albums / artists for q
  pick {list, index}        -> plays that song, or opens that album / artist (a new list)
  play / shuffle {list}     -> the list's Play / Shuffle (an artist's Play is their radio)
  mood {name}               -> plays a YouTube Music mood playlist at once
  radio                     -> more songs like the one playing now
  lyrics {title, artist}    -> the song's lyrics (with times when YouTube has them)
  thumbs {ids}              -> small pictures for the rows on screen

Gorgi gets back:
  music_list {list, kind, title, sub, tabs, tab, total, offset, items: [[kind, img, title, sub, right]...], query}
     in pieces of PIECE items; kind: "l" liked, "q" search, "r" artist, "a" album
  music_status {text}       "Searching...", "Nothing found", problems
  lyrics {title, artist, synced, lines: [[ms, text]...]}
  binary "GTHM" + picture id (4 bytes) + THUMB x THUMB RGB565 pixels
"""

import asyncio
import logging
import random
import struct
import zlib
from collections import OrderedDict

import ytmusic

log = logging.getLogger("gorgi.browse")

THUMB = 48
PIECE = 20          # list items per message (Gorgi takes ~15 KB per message)
MAX_ITEMS = 120     # Gorgi keeps this many items of a list
MAX_LYRICS = 120
LIKED_LIST = 1      # the Liked songs list always has this id
KEEP_LISTS = 12     # lists kept so a tap on an older page still works
MOODS_TTL = 24 * 3600


def picture_id(url):
    """A short number for a picture's link (never 0)."""
    return (zlib.crc32(url.encode()) & 0x7FFFFFFF) | 1 if url else 0


def small_thumb(thumbnails):
    """The smallest picture that is still at least THUMB pixels wide."""
    good = [t for t in (thumbnails or []) if t.get("url")]
    if not good:
        return ""
    good.sort(key=lambda t: t.get("width") or 0)
    for t in good:
        if (t.get("width") or 0) >= THUMB:
            return t["url"]
    return good[-1]["url"]


def artists_of(item):
    names = [a.get("name", "") for a in (item.get("artists") or []) if a.get("name")]
    return ", ".join(names[:2]) or item.get("artist") or ""


def join(*parts):
    return " - ".join(p for p in parts if p)


class Browser:
    def __init__(self, account, media, send, send_bytes, get_bytes=None):
        """account: ytmusic.Account; media: WindowsMedia (play_link, get_info);
        send(dict) / send_bytes(bytes): async, to Gorgi; get_bytes(url) -> bytes (for pictures)."""
        self.ytm = account
        self.media = media
        self.send = send
        self.send_bytes = send_bytes
        self.get_bytes = get_bytes or _download
        self.lists = OrderedDict()   # id -> {"kind", "entries": [...], ...}
        self.next_id = LIKED_LIST + 1
        self.pictures = {}           # picture id -> link
        self.pixels = OrderedDict()  # picture id -> RGB565 bytes (recent ones)
        self.sent_liked = None       # liked_version Gorgi has
        self.wants_home = False
        self._moods = None
        self._moods_at = 0.0

    # ------------------------------------------------------------ messages from Gorgi

    async def handle(self, msg):
        what = msg.get("do")
        try:
            if what == "home":
                self.wants_home = True
                await self.send_home(force=True)
            elif what == "search":
                await self.search(str(msg.get("q") or "").strip())
            elif what == "pick":
                await self.pick(msg.get("list"), msg.get("index"))
            elif what in ("play", "shuffle"):
                await self.play_list(msg.get("list"), what == "shuffle")
            elif what == "mood":
                await self.mood(str(msg.get("name") or ""))
            elif what == "radio":
                await self.radio()
            elif what == "lyrics":
                await self.lyrics(str(msg.get("title") or ""), str(msg.get("artist") or ""))
            elif what == "thumbs":
                await self.thumbs([i for i in (msg.get("ids") or []) if isinstance(i, int)])
        except Exception as e:
            log.warning("music %s failed: %s", what, e)
            await self.status("YouTube Music didn't answer. Try again in a moment.")

    async def status(self, text):
        await self.send({"type": "music_status", "text": text})

    def _not_connected(self):
        return self.ytm is None

    # ------------------------------------------------------------ lists

    def _remember(self, kind, entries, **extra):
        list_id = self.next_id
        self.next_id = self.next_id + 1 if self.next_id < 0x7FFFFFFF else LIKED_LIST + 1
        self.lists[list_id] = dict(kind=kind, entries=entries, **extra)
        while len(self.lists) > KEEP_LISTS:
            self.lists.popitem(last=False)
        return list_id

    def _row(self, kind, thumbs, title, sub, right=""):
        url = small_thumb(thumbs)
        img = picture_id(url)
        if img:
            self.pictures[img] = url
        return [kind, img, title or "", sub or "", right or ""]

    async def _send_list(self, list_id, kind, title, rows, total=None, sub="", tabs="", tab=0, query=""):
        rows = rows[:MAX_ITEMS]
        total = len(rows) if total is None else total
        for offset in range(0, max(len(rows), 1), PIECE):
            await self.send({"type": "music_list", "list": list_id, "kind": kind, "title": title, "sub": sub,
                             "tabs": tabs, "tab": tab, "total": total, "offset": offset,
                             "items": rows[offset:offset + PIECE], "query": query})

    async def send_home(self, force=False):
        """Your Liked songs, when Gorgi hasn't got this version of the list yet."""
        if self.ytm is None or not self.ytm.connected():
            if force:
                await self.status("Connect YouTube Music in Gorgi Helper's settings to see your Liked songs")
            return
        if not self.ytm.liked_at:
            await asyncio.to_thread(self.ytm.refresh_liked)
        if not force and self.sent_liked == self.ytm.liked_version:
            return
        self.sent_liked = self.ytm.liked_version
        liked = list(self.ytm.liked)
        self.lists[LIKED_LIST] = {"kind": "l", "entries": liked}
        rows = [self._row("s", s.get("thumbnails"), s.get("title"), artists_of(s), s.get("duration")) for s in liked]
        await self._send_list(LIKED_LIST, "l", "Liked songs", rows, total=self.ytm.liked_count or len(liked))

    async def search(self, query):
        if not query:
            return
        await self.status("Searching...")
        api = self.ytm._search_api()

        def run():
            songs = [r for r in api.search(query, filter="songs", limit=12) if r.get("videoId")]
            albums = [r for r in api.search(query, filter="albums", limit=10) if r.get("browseId")]
            artists = [r for r in api.search(query, filter="artists", limit=6) if r.get("browseId")]
            return songs, albums, artists

        songs, albums, artists = await asyncio.to_thread(run)
        log.info("search %r: %d songs, %d albums, %d artists", query, len(songs), len(albums), len(artists))
        if not (songs or albums or artists):
            await self.status(f"Nothing found for \"{query}\"")
            return
        q = ytmusic._plain(query)
        entries, rows = [], []
        for s in songs:
            entries.append(("song", s))
            rows.append(self._row("s", s.get("thumbnails"), s.get("title"),
                                  join(artists_of(s), (s.get("album") or {}).get("name")), s.get("duration")))
        for a in albums:
            entries.append(("album", a))
            rows.append(self._row("a", a.get("thumbnails"), a.get("title"),
                                  join(a.get("type") or "Album", artists_of(a), a.get("year"))))
        for a in artists:
            entries.append(("artist", a))
            rows.append(self._row("r", a.get("thumbnails"), a.get("artist") or a.get("title"), "Artist"))
        # Open the tab that fits: the band you named, else an album with that name, else songs
        tabs = "".join(k for k, group in (("s", songs), ("a", albums), ("r", artists)) if group)
        tab = 0
        if any(ytmusic._plain(a.get("artist") or a.get("title")) == q for a in artists):
            tab = tabs.index("r")
            # the band you named first in its list
            exact = [i for i, (k, a) in enumerate(entries) if k == "artist"
                     and ytmusic._plain(a.get("artist") or a.get("title")) == q]
            if exact and exact[0] != len(songs) + len(albums):
                first = len(songs) + len(albums)
                i = exact[0]
                entries.insert(first, entries.pop(i))
                rows.insert(first, rows.pop(i))
        elif "a" in tabs and any(ytmusic._plain(a.get("title")) == q for a in albums) \
                and not any(ytmusic._plain(s.get("title")) == q for s in songs):
            tab = tabs.index("a")
        list_id = self._remember("q", entries, query=query)
        await self._send_list(list_id, "q", query, rows, tabs=tabs, tab=tab, query=query)
        await self.status("")

    async def open_artist(self, browse_id, name=""):
        await self.status("Opening...")
        api = self.ytm._search_api()

        def run():
            artist = api.get_artist(browse_id)
            albums = (artist.get("albums") or {})
            found = albums.get("results") or []
            if albums.get("params") and albums.get("browseId"):
                try:
                    found = api.get_artist_albums(albums["browseId"], albums["params"], limit=60) or found
                except Exception as e:
                    log.info("all albums of %s: %s", browse_id, e)
            return artist, found

        artist, albums = await asyncio.to_thread(run)
        name = artist.get("name") or name
        entries, rows = [], []
        for a in albums:
            if not a.get("browseId"):
                continue
            entries.append(("album", a))
            rows.append(self._row("a", a.get("thumbnails"), a.get("title"), join(a.get("type") or "Album", a.get("year"))))
        for s in (artist.get("songs") or {}).get("results") or []:
            if not s.get("videoId"):
                continue
            entries.append(("song", s))
            rows.append(self._row("s", s.get("thumbnails"), s.get("title"), (s.get("album") or {}).get("name")
                                  if isinstance(s.get("album"), dict) else s.get("album")))
        tabs = ("a" if any(k == "album" for k, _ in entries) else "") + ("s" if any(k == "song" for k, _ in entries) else "")
        log.info("artist %s: %d albums, %d songs", name, sum(k == "album" for k, _ in entries),
                 sum(k == "song" for k, _ in entries))
        list_id = self._remember("r", entries, name=name, shuffle=artist.get("shuffleId"), radio=artist.get("radioId"))
        await self._send_list(list_id, "r", name, rows, tabs=tabs)
        await self.status("")

    async def open_album(self, browse_id, name=""):
        await self.status("Opening...")
        api = self.ytm._search_api()
        album = await asyncio.to_thread(api.get_album, browse_id)
        tracks = [t for t in (album.get("tracks") or []) if t.get("videoId")]
        entries = [("song", t) for t in tracks]
        rows = [["s", 0, t.get("title") or "", artists_of(t), t.get("duration") or ""] for t in tracks]
        count = f"{len(tracks)} songs" if len(tracks) != 1 else "1 song"
        sub = join(artists_of(album), album.get("year"), count)
        name = album.get("title") or name
        log.info("album %s: %d songs", name, len(tracks))
        list_id = self._remember("a", entries, name=name, playlist=album.get("audioPlaylistId"))
        await self._send_list(list_id, "a", name, rows, sub=sub)
        await self.status("")

    # ------------------------------------------------------------ playing

    async def _play(self, url, name):
        if not url:
            await self.status("I couldn't play that one")
            return
        log.info("music: playing %s (%s)", name, url)
        await self.status(f"Playing {name}" if name else "Playing...")
        await self.media.play_link(url)

    async def pick(self, list_id, index):
        found = self.lists.get(list_id)
        if not found or not isinstance(index, int):
            await self.status("That list is old. Search again?")
            return
        entries = found["entries"]
        if not 0 <= index < len(entries):
            return
        if found["kind"] == "l":  # your Liked songs, from this song on
            song = entries[index]
            await self._play(ytmusic.playlist_url("LM", song["videoId"]), song.get("title"))
            return
        kind, item = entries[index]
        if kind == "artist":
            await self.open_artist(item["browseId"], item.get("artist") or item.get("title"))
        elif kind == "album":
            await self.open_album(item["browseId"], item.get("title"))
        elif found["kind"] == "a" and found.get("playlist"):  # an album's song: the album from there on
            await self._play(ytmusic.playlist_url(found["playlist"], item["videoId"]), item.get("title"))
        else:
            await self._play(ytmusic.song_url(item["videoId"]), item.get("title"))

    async def play_list(self, list_id, shuffle):
        if list_id == LIKED_LIST or list_id is None:
            if self.ytm is None or not self.ytm.connected():
                await self.status("Connect YouTube Music in Gorgi Helper's settings first")
                return
            await self._play(self.ytm.liked_url(shuffle=shuffle), "your Liked songs")
            return
        found = self.lists.get(list_id)
        if not found:
            return
        if found["kind"] == "r":
            url = found.get("shuffle") if shuffle else (found.get("radio") or found.get("shuffle"))
            await self._play(ytmusic.playlist_url(url) if url else None, found.get("name"))
        elif found["kind"] == "a" and found.get("playlist"):
            start = random.choice(found["entries"])[1]["videoId"] if shuffle and found["entries"] else None
            await self._play(ytmusic.playlist_url(found["playlist"], start), found.get("name"))

    async def mood(self, name):
        await self.status(f"Finding {name} music...")
        api = self.ytm._search_api()

        def run():
            import time

            if self._moods is None or time.monotonic() - self._moods_at > MOODS_TTL:
                self._moods = api.get_mood_categories()
                self._moods_at = time.monotonic()
            wanted = name.lower()
            params = None
            for section in ("Moods & moments", *self._moods.keys()):
                for cat in self._moods.get(section) or []:
                    if (cat.get("title") or "").lower() == wanted:
                        params = cat.get("params")
                        break
                if params:
                    break
            playlists = []
            if params:
                playlists = [p for p in api.get_mood_playlists(params) if p.get("playlistId")]
            if not playlists:  # no such mood: a search does it
                playlists = [p for p in api.search(f"{name} music", filter="playlists", limit=8)
                             if p.get("browseId") or p.get("playlistId")]
            return playlists[:8]

        playlists = await asyncio.to_thread(run)
        if not playlists:
            await self.status(f"No {name} music found")
            return
        p = random.choice(playlists)
        pid = p.get("playlistId") or (p.get("browseId") or "").removeprefix("VL")
        await self._play(ytmusic.playlist_url(pid), f"{name}: {p.get('title') or ''}".rstrip(": "))

    async def radio(self):
        info = await self.media.get_info()
        if not info.get("title"):
            await self.status("Nothing is playing")
            return
        await self.status("Starting song radio...")
        video_id = await asyncio.to_thread(self.ytm.find_song_id, info["title"], info.get("artist", ""))
        if not video_id:
            await self.status("I couldn't find this song on YouTube Music")
            return
        await self._play(ytmusic.song_url(video_id), f"radio: {info['title']}")

    # ------------------------------------------------------------ lyrics

    async def lyrics(self, title, artist):
        answer = {"type": "lyrics", "title": title, "artist": artist, "synced": False, "lines": []}
        try:
            answer.update(await asyncio.to_thread(self._find_lyrics, title, artist))
        except Exception as e:
            log.info("lyrics for %r: %s", title, e)
        log.info("lyrics for %r: %d lines%s", title, len(answer["lines"]), " (timed)" if answer["synced"] else "")
        await self.send(answer)

    def _find_lyrics(self, title, artist):
        if not title:
            return {}
        api = self.ytm._search_api()
        video_id = self.ytm.find_song_id(title, artist)
        if not video_id:
            return {}
        browse_id = (api.get_watch_playlist(videoId=video_id, limit=1) or {}).get("lyrics")
        if not browse_id:
            return {}
        found = api.get_lyrics(browse_id, timestamps=True) or {}
        raw = found.get("lyrics")
        if found.get("hasTimestamps") and isinstance(raw, list):
            lines = [[int(getattr(l, "start_time", 0) or 0), str(getattr(l, "text", "") or "")] for l in raw]
            synced = True
        else:
            lines = [[0, t.strip()] for t in str(raw or "").split("\n")]
            synced = False
        lines = [[ms, text[:70]] for ms, text in lines][:MAX_LYRICS]
        while len(str(lines).encode()) > 11000:  # Gorgi takes ~15 KB per message
            lines = lines[:-10]
        return {"synced": synced, "lines": lines}

    # ------------------------------------------------------------ pictures

    async def thumbs(self, ids):
        for img in ids[:12]:
            data = self.pixels.get(img)
            if data is None:
                url = self.pictures.get(img)
                if not url:
                    continue
                try:
                    data = await asyncio.to_thread(self._picture, url)
                except Exception as e:
                    log.info("picture %s: %s", url[:60], e)
                    continue
                self.pixels[img] = data
                while len(self.pixels) > 300:
                    self.pixels.popitem(last=False)
            await self.send_bytes(b"GTHM" + struct.pack("<I", img) + data)

    def _picture(self, url):
        import album_art

        return album_art.to_rgb565(self.get_bytes(url), size=THUMB)


def _download(url):
    import requests

    r = requests.get(url, timeout=8)
    r.raise_for_status()
    return r.content
