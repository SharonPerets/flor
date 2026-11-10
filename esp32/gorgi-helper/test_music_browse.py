"""Tests for the Music screen's Browse view (music_browse.py) with a fake YouTube Music.
No internet.  Run: python test_music_browse.py"""

import asyncio
import io
import struct
import tempfile
from pathlib import Path

from PIL import Image

import music_browse
import ytmusic


def pic(w):
    return [{"url": f"https://img/{w}", "width": w, "height": w}]


class LyricLine:
    def __init__(self, text, start_time):
        self.text, self.start_time = text, start_time


class FakeApi:
    def search(self, query, filter=None, limit=10):
        if filter == "songs":
            return [{"videoId": "s1", "title": "Would?", "artists": [{"name": "Alice in Chains"}],
                     "album": {"name": "Dirt"}, "duration": "3:28", "thumbnails": pic(60)},
                    {"videoId": "s2", "title": "Rooster", "artists": [{"name": "Alice in Chains"}],
                     "album": {"name": "Dirt"}, "duration": "6:15", "thumbnails": pic(60)}]
        if filter == "albums":
            return [{"browseId": "MPdirt", "title": "Dirt", "type": "Album", "artist": "Alice in Chains", "year": "1992",
                     "thumbnails": pic(120)}]
        if filter == "artists":
            return [{"browseId": "UCother", "artist": "Alice in Chains Tribute", "thumbnails": pic(60)},
                    {"browseId": "UCaic", "artist": "Alice In Chains", "thumbnails": pic(60)}]
        if filter == "playlists":
            return [{"browseId": "VLPLsearch", "title": "Some mix"}]
        return []

    def get_artist(self, browse_id):
        return {"name": "Alice In Chains", "shuffleId": "RDAOaic", "radioId": "RDEMaic",
                "albums": {"results": [{"browseId": "MPdirt", "title": "Dirt", "year": "1992"}],
                           "browseId": "UCaic", "params": "p"},
                "songs": {"results": [{"videoId": "s1", "title": "Would?", "album": {"name": "Dirt"},
                                       "thumbnails": pic(60)}]}}

    def get_artist_albums(self, channel, params, limit=100):
        return [{"browseId": "MPdirt", "title": "Dirt", "type": "Album", "year": "1992", "thumbnails": pic(120)},
                {"browseId": "MPfacelift", "title": "Facelift", "type": "Album", "year": "1990", "thumbnails": pic(120)}]

    def get_album(self, browse_id):
        return {"title": "Dirt", "year": "1992", "artists": [{"name": "Alice In Chains"}],
                "audioPlaylistId": "OLAKdirt",
                "tracks": [{"videoId": "t1", "title": "Them Bones", "artists": [{"name": "Alice In Chains"}],
                            "duration": "2:30"},
                           {"videoId": "t2", "title": "Dam That River", "artists": [{"name": "Alice In Chains"}],
                            "duration": "3:09"}]}

    def get_mood_categories(self):
        return {"Moods & moments": [{"title": "Focus", "params": "focus"}, {"title": "Feel Good", "params": "good"}]}

    def get_mood_playlists(self, params):
        return [{"playlistId": f"PL{params}", "title": f"{params} mix"}]

    def get_watch_playlist(self, videoId=None, limit=25):
        return {"lyrics": "MPLYt1"}

    def get_lyrics(self, browse_id, timestamps=False):
        return {"hasTimestamps": True, "lyrics": [LyricLine("Know me broken by my master", 1200),
                                                  LyricLine("Teach thee on child, love hereafter", 5400)]}


class Media:
    def __init__(self):
        self.played = []

    async def play_link(self, url):
        self.played.append(url)

    async def get_info(self):
        return {"title": "Would?", "artist": "Alice in Chains", "playing": True}


def png():
    out = io.BytesIO()
    Image.new("RGB", (60, 60), (200, 30, 30)).save(out, "PNG")
    return out.getvalue()


def setup(signed_in=True):
    folder = Path(tempfile.mkdtemp())
    acc = ytmusic.Account(folder)
    api = FakeApi()
    acc._anyone = lambda: api
    acc._signed_in = (lambda: None) if not signed_in else (lambda: api)
    if signed_in:
        (folder / ytmusic.AUTH_FILE).write_text("{}")
        acc.liked = [{"videoId": "v1", "title": "Hysteria", "artists": [{"name": "Muse"}], "duration": "3:48",
                      "thumbnails": pic(60)}]
        acc.liked_count, acc.liked_at, acc.liked_version = 1, 1.0, 1
    sent, pictures, media = [], [], Media()

    async def send(msg):
        sent.append(msg)

    async def send_bytes(data):
        pictures.append(data)

    b = music_browse.Browser(acc, media, send, send_bytes, get_bytes=lambda url: png())
    return b, sent, pictures, media


def lists(sent):
    return [m for m in sent if m["type"] == "music_list"]


def test_search_artist_album():
    b, sent, pictures, media = setup()

    async def run():
        await b.search("alice in chains")
        q = lists(sent)[-1]
        assert q["kind"] == "q" and q["tabs"] == "sar" and q["tab"] == 2, q
        rows = q["items"]
        assert [r[0] for r in rows] == ["s", "s", "a", "r", "r"]
        assert rows[3][2] == "Alice In Chains", rows  # the band you named comes first in Artists
        assert rows[0][3] == "Alice in Chains - Dirt" and rows[0][4] == "3:28"
        assert rows[2][3] == "Album - Alice in Chains - 1992"
        await b.pick(q["list"], 3)  # the band -> its page
        artist = lists(sent)[-1]
        assert artist["kind"] == "r" and artist["title"] == "Alice In Chains" and artist["tabs"] == "as"
        assert [r[2] for r in artist["items"]] == ["Dirt", "Facelift", "Would?"]
        await b.play_list(artist["list"], True)
        await b.play_list(artist["list"], False)
        assert media.played[-2:] == ["https://music.youtube.com/watch?list=RDAOaic",
                                     "https://music.youtube.com/watch?list=RDEMaic"]
        await b.pick(artist["list"], 0)  # Dirt -> its songs
        album = lists(sent)[-1]
        assert album["kind"] == "a" and album["sub"] == "Alice In Chains - 1992 - 2 songs"
        assert album["items"][1] == ["s", 0, "Dam That River", "Alice In Chains", "3:09"]
        await b.pick(album["list"], 1)
        assert media.played[-1] == "https://music.youtube.com/watch?v=t2&list=OLAKdirt"
        await b.play_list(album["list"], False)
        assert media.played[-1] == "https://music.youtube.com/watch?list=OLAKdirt"
        await b.pick(q["list"], 0)  # a song from the search: the song, then more like it
        assert media.played[-1] == "https://music.youtube.com/watch?v=s1&list=RDAMVMs1"
        await b.pick(9999, 0)
        assert sent[-1]["type"] == "music_status"

    asyncio.run(run())


def test_home_mood_radio_lyrics_pictures():
    b, sent, pictures, media = setup()

    async def run():
        await b.handle({"type": "music", "do": "home"})
        home = lists(sent)[-1]
        assert home["list"] == music_browse.LIKED_LIST and home["items"][0][2] == "Hysteria" and home["total"] == 1
        n = len(sent)
        await b.send_home()  # same list: nothing sent again
        assert len(sent) == n
        await b.handle({"type": "music", "do": "pick", "list": music_browse.LIKED_LIST, "index": 0})
        assert media.played[-1] == "https://music.youtube.com/watch?v=v1&list=LM"
        await b.handle({"type": "music", "do": "shuffle", "list": music_browse.LIKED_LIST})
        assert media.played[-1] == "https://music.youtube.com/watch?v=v1&list=LM"
        await b.handle({"type": "music", "do": "mood", "name": "Feel good"})
        assert media.played[-1] == "https://music.youtube.com/watch?list=PLgood"
        await b.handle({"type": "music", "do": "radio"})
        assert media.played[-1] == "https://music.youtube.com/watch?v=s1&list=RDAMVMs1"
        await b.handle({"type": "music", "do": "lyrics", "title": "Would?", "artist": "Alice in Chains"})
        ly = sent[-1]
        assert ly["type"] == "lyrics" and ly["synced"] and ly["lines"][1] == [5400, "Teach thee on child, love hereafter"]
        img = home["items"][0][1]
        await b.handle({"type": "music", "do": "thumbs", "ids": [img, 12345]})
        assert len(pictures) == 1 and pictures[0][:4] == b"GTHM"
        assert struct.unpack("<I", pictures[0][4:8])[0] == img
        assert len(pictures[0]) == 8 + music_browse.THUMB * music_browse.THUMB * 2

    asyncio.run(run())


def test_plain_lyrics():
    """Songs with only plain lyrics: the timed ask finds nothing, the plain one does (Space Cadet, 4.8.20)."""
    b, sent, pictures, media = setup()
    api = b.ytm._anyone()
    api.get_lyrics = lambda browse_id, timestamps=False: (
        None if timestamps else {"hasTimestamps": False, "lyrics": "I lay on my side\nShe never expects"})

    async def run():
        await b.lyrics("Would?", "Alice in Chains")
        ly = sent[-1]
        assert not ly["synced"] and ly["lines"] == [[0, "I lay on my side"], [0, "She never expects"]], ly

    asyncio.run(run())


def test_not_connected():
    b, sent, pictures, media = setup(signed_in=False)

    async def run():
        await b.handle({"type": "music", "do": "home"})
        assert sent[-1]["type"] == "music_status" and "Connect YouTube Music" in sent[-1]["text"]
        await b.handle({"type": "music", "do": "search", "q": "dirt"})  # search works without a sign-in
        assert lists(sent)[-1]["kind"] == "q"

    asyncio.run(run())


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
    print("all Browse tests passed")
