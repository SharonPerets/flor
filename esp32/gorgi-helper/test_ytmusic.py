"""Tests for YouTube Music: song matching, what "play ..." opens, tags, like / dislike.
No internet: the YouTube Music add-on is replaced by a fake.  Run: python test_ytmusic.py"""

import asyncio
import tempfile
from pathlib import Path

import brain
import ytmusic
from conversation import Conversation

LIKED = [
    {"videoId": "v1", "title": "Supermassive Black Hole", "artists": [{"name": "Muse"}]},
    {"videoId": "v2", "title": "You Don't Know Nothing About Love", "artists": [{"name": "Chris Cornell"}]},
    {"videoId": "v3", "title": "Hysteria", "artists": [{"name": "Muse"}]},
]


class FakeApi:
    def __init__(self):
        self.rated = []

    def search(self, query, filter=None, limit=10):
        q = query.lower()
        if filter == "artists":
            return [{"resultType": "artist", "browseId": "UCmuse", "artist": "Muse"}] if "muse" in q else []
        if filter == "albums":
            return [{"resultType": "album", "browseId": "MPRE2", "title": "Absolution XX Anniversary",
                     "artist": "Muse"},
                    {"resultType": "album", "browseId": "MPRE1", "title": "Absolution", "artist": "Muse"}]
        if filter == "songs":
            if "hysteria" in q:
                return [{"resultType": "song", "videoId": "h2", "title": "Hysteria (Live from Wembley Stadium)",
                         "artists": [{"name": "Muse"}]},
                        {"resultType": "song", "videoId": "h0", "title": "Hysteria", "artists": [{"name": "Def Leppard"}]},
                        {"resultType": "song", "videoId": "h1", "title": "Hysteria", "artists": [{"name": "Muse"}]}]
            if "queen" in q or "bohemian" in q:
                return [{"resultType": "song", "videoId": "bo1", "title": "Bohemian Rhapsody",
                         "artists": [{"name": "Queen"}]}]
            if "stand by me" in q:
                return [{"resultType": "song", "videoId": "sb1", "title": "Stand by Me", "artists": [{"name": "Ben E. King"}]}]
            return [{"resultType": "song", "videoId": "s9", "title": "Starlight", "artists": [{"name": "Muse"}]}]
        if "jazz" in q:
            return [{"category": "Top result", "resultType": "playlist", "browseId": "VLjazz", "title": "Chill Jazz"},
                    {"category": "Songs", "resultType": "song", "videoId": "j1", "title": "Blue in Green",
                     "artists": [{"name": "Miles Davis"}]}]
        return []

    def get_artist(self, browse_id):
        return {"shuffleId": "RDAOmuse", "radioId": "RDEMmuse"}

    def get_album(self, browse_id):
        return {"audioPlaylistId": "OLAKabsolution"}

    def get_liked_songs(self, limit=100):
        return {"trackCount": 3, "tracks": [dict(t) for t in LIKED]}

    def rate_song(self, video_id, rating):
        self.rated.append((video_id, rating))


def account(signed_in=True):
    folder = Path(tempfile.mkdtemp())
    acc = ytmusic.Account(folder)
    api = FakeApi()
    acc._anyone = lambda: api
    if signed_in:
        (folder / ytmusic.AUTH_FILE).write_text("{}")
        acc._signed_in = lambda: api
    else:
        acc._signed_in = lambda: None
    return acc, api


def test_matching():
    assert ytmusic.find_liked(LIKED, "Hysteria", "Muse")["videoId"] == "v3"
    assert ytmusic.find_liked(LIKED, "Hysteria (Remastered)", "MuseVEVO")["videoId"] == "v3"
    assert ytmusic.find_liked(LIKED, "Supermassive Black Hole", "Muse - Topic")["videoId"] == "v1"
    assert ytmusic.find_liked(LIKED, "Hysteria", "Red Hot Chili Peppers") is None
    assert ytmusic.find_liked(LIKED, "Hysteria", "") is not None  # no artist given: the title is enough
    assert ytmusic.find_liked(LIKED, "Starlight", "Muse") is None


def test_links():
    acc, api = account()
    assert acc.url_for("Muse") == ("https://music.youtube.com/watch?list=RDAOmuse", "Muse")
    assert acc.url_for("Muse", "band") == ("https://music.youtube.com/watch?list=RDAOmuse", "Muse")
    url, name = acc.url_for("Bohemian Rhapsody by Queen", "song")
    assert url == "https://music.youtube.com/watch?v=bo1&list=RDAMVMbo1" and name == "Bohemian Rhapsody by Queen"
    assert acc.url_for("Bohemian Rhapsody")[0].endswith("RDAMVMbo1")
    assert acc.url_for("Hysteria by Muse", "song")[0].endswith("v=h1&list=RDAMVMh1")  # the studio one, by Muse
    assert acc.url_for("Stand by Me", "song")[0].endswith("RDAMVMsb1")
    assert acc.url_for("Absolution by Muse", "album") == ("https://music.youtube.com/watch?list=OLAKabsolution",
                                                         "Absolution")
    assert acc.url_for("chill jazz")[0].endswith("RDAMVMj1")
    assert acc.url_for("zzzz") == (None, None) or acc.url_for("zzzz")[0].endswith("RDAMVMs9")
    assert acc.liked_url() == "https://music.youtube.com/watch?list=LM"


def test_liked_and_rating():
    acc, api = account()
    assert acc.is_liked("Hysteria", "Muse") is None  # not read yet
    assert acc.refresh_liked(force=True)
    assert acc.is_liked("Hysteria", "Muse") is True
    assert acc.is_liked("Starlight", "Muse") is False
    assert acc.liked_url(shuffle=True).startswith("https://music.youtube.com/watch?v=v")
    assert acc.rate("Starlight", "Muse", "LIKE") is None
    assert api.rated[-1] == ("s9", "LIKE") and acc.is_liked("Starlight", "Muse")
    assert acc.rate("Hysteria", "Muse", "INDIFFERENT") is None
    assert api.rated[-1] == ("v3", "INDIFFERENT") and acc.is_liked("Hysteria", "Muse") is False
    nobody, _ = account(signed_in=False)
    assert nobody.rate("Hysteria", "Muse", "LIKE") == ytmusic.NOT_CONNECTED
    assert nobody.refresh_liked(force=True) is False and nobody.is_liked("Hysteria", "Muse") is None


def test_tags():
    mood, text, actions = brain.parse_reply("[excited] Here's some Muse! [music: band Muse]")
    assert mood == "excited" and text == "Here's some Muse!" and actions == [("music", "band Muse")], actions
    mood, text, actions = brain.parse_reply("[love] Me too! [music: like]")
    assert text == "Me too!" and actions == [("music", "like")]
    assert "[music: band Muse]" in brain.PERSONALITY


class FakeMedia:
    def __init__(self):
        self.calls = []

    async def play_link(self, url):
        self.calls.append(("link", url))

    async def command(self, action):
        self.calls.append(("cmd", action))

    async def set_playing(self, play):
        self.calls.append(("playing", play))

    def get_volume(self):
        return 50


def conversation(acc):
    c = Conversation(None, FakeMedia(), None, None, None, None, music=acc)
    c._info = {"title": "Hysteria", "artist": "Muse", "playing": True}
    return c


def test_conversation_actions():
    acc, api = account()
    acc.refresh_liked(force=True)

    async def run():
        c = conversation(acc)
        keep, resume, problem = await c._do_actions([("music", "band Muse")], True)
        assert not keep and problem is None and c._play_link.endswith("RDAOmuse")
        c = conversation(acc)
        keep, resume, problem = await c._do_actions([("music", "shuffle liked")], True)
        assert not keep and "list=LM" in c._play_link
        c = conversation(acc)
        keep, resume, problem = await c._do_actions([("music", "like")], True)
        assert keep and problem is None and api.rated[-1] == ("v3", "LIKE")
        c = conversation(acc)
        keep, resume, problem = await c._do_actions([("music", "dislike")], True)
        assert c._skip_after and api.rated[-1] == ("v3", "DISLIKE")
        c = conversation(acc)
        keep, resume, problem = await c._do_actions([("music", "song Hysteria by Muse")], True)
        assert not keep and c._play_link.endswith("RDAMVMh1")
        c = conversation(acc)
        c._info = {}
        keep, resume, problem = await c._do_actions([("music", "like")], True)
        assert "Nothing is playing" in problem

    asyncio.run(run())


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
    print("all YouTube Music tests passed")
