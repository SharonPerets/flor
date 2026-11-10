"""The latest weather forecast, sent by Gorgi from its weather screen.

Gorgi downloads the forecast itself (from Open-Meteo) and sends a copy here,
so the AI can answer "will it rain tomorrow?".
"""

import time

MAX_AGE = 3 * 60 * 60  # older than this and it's not worth mentioning

_latest = None
_received = 0.0


def update(msg):
    global _latest, _received
    if msg.get("city") and isinstance(msg.get("days"), list):
        _latest = msg
        _received = time.time()


def latest():
    """The latest forecast message from Gorgi, or None if there's no recent one."""
    if not _latest or time.time() - _received > MAX_AGE:
        return None
    return _latest


def summary():
    """A few lines for the AI, or "" if there's no recent forecast."""
    if not _latest or time.time() - _received > MAX_AGE:
        return ""
    w = _latest
    now = w.get("now") or {}
    lines = [f"Weather in {w['city']} (pull down from the top of your screen and tap the weather to see it):"]
    if now:
        lines.append(f"  Now: {now.get('sky', '?')}, {now.get('temp', '?')} C "
                     f"(feels like {now.get('feels', '?')} C), humidity {now.get('humidity', '?')}%, "
                     f"wind {now.get('wind', '?')} km/h.")
    for d in w["days"][:7]:
        lines.append(f"  {d.get('day', '?')}: {d.get('sky', '?')}, {d.get('lo', '?')} to {d.get('hi', '?')} C, "
                     f"{d.get('rain', 0)}% chance of rain.")
    lines.append("Only bring up the weather when it's relevant or asked. Say temperatures in Celsius.")
    return "\n".join(lines)
