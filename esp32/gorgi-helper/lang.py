"""Which language Gorgi speaks now: English ("en") or Hebrew ("he").

It follows you: whatever language you spoke last (the cloud hearing tells), Gorgi answers in it,
and alerts and messages use it too until you speak the other one. Saved in config.json
("language"). Everything asks `lang.code()` when it needs it. `lang.t("English", "עברית")`
picks the right sentence.
"""

import re

LANGUAGES = ("en", "he")
_config = {}

HEBREW_RE = re.compile(r"[א-ת]")


_listeners = []  # fn(code), called when the spoken language changes


def bind(config):
    """Reads the choice from this settings dict from now on."""
    global _config
    _config = config


def on_change(fn):
    _listeners.append(fn)


def set_spoken(code):
    """You just spoke this language: Gorgi uses it from now on."""
    if code not in LANGUAGES or code == _config.get("language", "en"):
        return
    _config["language"] = code
    for fn in list(_listeners):
        try:
            fn(code)
        except Exception:
            pass


def code():
    value = (_config.get("language") or "en").lower()
    return value if value in LANGUAGES else "en"


def hebrew():
    return code() == "he"


def t(english, hebrew_text):
    return hebrew_text if hebrew() else english


def has_hebrew(text):
    return bool(HEBREW_RE.search(text or ""))


def cut_bytes(text, limit):
    """Text cut to at most `limit` bytes of UTF-8 without breaking a letter (Gorgi's fields are bytes)."""
    data = (text or "").encode("utf-8")
    if len(data) <= limit:
        return text or ""
    return data[:limit].decode("utf-8", "ignore").rstrip()
