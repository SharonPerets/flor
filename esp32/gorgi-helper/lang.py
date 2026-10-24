"""Which language Gorgi hears and speaks: English ("en") or Hebrew ("he").

The choice lives in config.json ("language"), changed in Settings or with the
switch in Gorgi's Action Center. Everything asks `lang.code()` when it needs it,
so a change works at once. `lang.t("English", "עברית")` picks the right sentence.
"""

import re

LANGUAGES = ("en", "he")
_config = {}

HEBREW_RE = re.compile(r"[א-ת]")


def bind(config):
    """Reads the choice from this settings dict from now on."""
    global _config
    _config = config


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
