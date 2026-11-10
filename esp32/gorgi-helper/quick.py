"""Quick answers: timers and weather without asking the AI.

"Set a timer for 10 minutes", "cancel the timer", "how long is left?" and
"what's the weather?" are answered right here, about a second or two faster
than the AI, and they don't use up the free AI's daily limit.

Only short, plain sentences are caught. Anything longer or mixed ("set a
timer and play some music") goes to the AI as before, so nothing is lost.
"""

import random
import re

import timekeeper
import weather

NUMBER_WORDS = {
    "a": 1, "an": 1, "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7, "eight": 8,
    "nine": 9, "ten": 10, "eleven": 11, "twelve": 12, "thirteen": 13, "fourteen": 14, "fifteen": 15,
    "sixteen": 16, "seventeen": 17, "eighteen": 18, "nineteen": 19, "twenty": 20, "twenty five": 25,
    "thirty": 30, "forty": 40, "forty five": 45, "fifty": 50, "sixty": 60, "ninety": 90,
}
_NUM = r"(?:\d+(?:\.\d+)?|" + "|".join(sorted((re.escape(w) for w in NUMBER_WORDS), key=len, reverse=True)) + r")"
_UNIT = r"(?:hours?|hrs?|minutes?|mins?|seconds?|secs?)"
# "10 minutes", "an hour and a half", "1 hour 30 minutes", "half an hour", "ten-minute"
DUR = (rf"(?:half an hour|half a minute|{_NUM}[\s-]+{_UNIT}(?:\s+and a half)?"
       rf"(?:(?:\s+and)?\s+{_NUM}[\s-]+{_UNIT})?)")

LEAD = r"^(?:(?:hey|hi|ok|okay|so|um|uh|please|gorgi|georgie|georgi)[\s,.!]+)*"
ASK = r"(?:(?:can|could|would|will) you\s+|i (?:need|want) (?:you to\s+)?|let's\s+|please\s+)?"
TAIL = r"(?:[\s,]+(?:please|thanks|thank you|now|for me))*[\s.!?]*$"
LABEL = r"(?:\s+(?:for the|for my|for|called|named)\s+(?P<label>[a-z][a-z' ]{1,30}?))?"

TIMER_RES = [
    # set a timer for 10 minutes (for pasta)
    re.compile(LEAD + ASK + r"(?:set|start|put|make|give me)?\s*(?:a|an|the|me a)?\s*(?:timer|countdown)"
               r"\s*(?:for|of|on)?\s*(?P<dur>" + DUR + r")" + LABEL + TAIL),
    # set a 10 minute timer (for pasta) / 10 minutes timer
    re.compile(LEAD + ASK + r"(?:set|start|put|make|give me)?\s*(?:a|an|me a)?\s*(?P<dur>" + DUR + r")\s+"
               r"(?:timer|countdown)" + LABEL + TAIL),
    # time me for 10 minutes / remind me in 10 minutes (with no "about"/"to")
    re.compile(LEAD + ASK + r"(?:time me for|count down)\s+(?P<dur>" + DUR + r")" + TAIL),
]
CANCEL_RE = re.compile(LEAD + ASK + r"(?:cancel|stop|delete|remove|kill|turn off|end|clear)\s+(?:the|my|that)?\s*"
                       r"(?P<label>[a-z][a-z' ]{0,30}?\s+)?(?P<what>timers?)" + TAIL)
LEFT_RE = re.compile(LEAD + r"(?:how (?:much time|long)(?: do i have| is| has)?|what's|what is)\s*(?:left|remaining|"
                     r"the time left|the time remaining)(?:\s+(?:on|for|in)\s+(?:the|my)\s*(?P<label>[a-z][a-z' ]{0,30}?)?"
                     r"\s*(?:timer|countdown|focus(?: session)?)?)?" + TAIL)

_DAY = r"(?P<day>today|tonight|now|right now|outside|tomorrow|this week)?"
WEATHER_RES = [
    re.compile(LEAD + ASK + r"(?:what's|what is|how's|how is|whats|tell me|check|give me)\s+(?:the\s+)?"
               r"(?:weather|forecast)(?:\s+like)?(?:\s+going to be)?(?:\s+(?:for|in|of))?\s*" + _DAY + TAIL),
    re.compile(LEAD + r"(?:weather|forecast)\s*" + _DAY + TAIL),
    re.compile(LEAD + r"(?:what's|what is|whats)\s+(?:the\s+)?(?:temperature|temp)(?:\s+(?:outside|now|today|"
               r"right now))?" + TAIL),
    re.compile(LEAD + r"(?:is it|will it|is it going to|it's going to)\s+(?:be\s+)?"
               r"(?P<ask>rain|raining|hot|cold|warm|sunny|cloudy|snow)(?:\s+(?:be\s+)?)?(?:\s*(?:outside))?\s*"
               + _DAY + TAIL),
    re.compile(LEAD + r"(?:do i|should i)\s+(?:need|take|bring)\s+(?:an?\s+)?(?P<ask>umbrella|jacket|coat)"
               r"\s*" + _DAY + TAIL),
]


def _seconds(text):
    """'ten minutes' / 'an hour and a half' / 'half an hour' -> seconds."""
    t = text.lower().replace("-", " ")
    if "half an hour" in t:
        return 1800
    if "half a minute" in t:
        return 30
    half = 0
    m = re.search(rf"({_UNIT})\s+and a half", t)
    if m:  # "an hour and a half": add half of that unit
        half = timekeeper.parse_duration("1 " + m.group(1)) // 2
        t = t.replace(" and a half", "")
    for word in sorted(NUMBER_WORDS, key=len, reverse=True):
        t = re.sub(rf"\b{word}\b", str(NUMBER_WORDS[word]), t)
    seconds = timekeeper.parse_duration(t)
    return seconds + half if seconds else None


def _clean(text):
    t = text.lower().strip()
    t = re.sub(r"[’`]", "'", t)
    t = re.sub(r"[,;]+", ",", t)
    return " ".join(t.split())


def _say_length(seconds):
    return timekeeper.describe_left(seconds)


def answer(text, keeper=None):
    """Returns (mood, spoken, actions) for a quick question, or None to ask the AI.
    actions are the same (name, value) pairs the AI's tags give."""
    t = _clean(text)
    if not t or len(t) > 80:
        return None

    if keeper is not None and keeper.known:
        for r in TIMER_RES:
            m = r.match(t)
            if m:
                seconds = _seconds(m.group("dur"))
                if not seconds:
                    return None
                label = (m.group("label") or "").strip() if "label" in r.groupindex else ""
                label = re.sub(r"^(?:the|my|a)\s+", "", label)
                said = _say_length(seconds)
                spoken = random.choice([f"{said}, got it!", f"Okay, {said} on the clock!", f"Timer set for {said}!"])
                if label:
                    spoken = f"{said} for the {label}, got it!"
                return "happy", spoken[0].upper() + spoken[1:], [("timer", f"{seconds}s | {label}")]

        m = CANCEL_RE.match(t)
        if m:
            label = (m.group("label") or "").strip()
            label = re.sub(r"^(?:the|my)\s+", "", label)
            if label in ("all", "all the", "all my", "every", "both") or (m.group("what") == "timers" and not label):
                value, spoken = "all timers", "Okay, all timers cancelled!"
            else:
                value, spoken = label or "the timer", "Okay, timer cancelled!"
            timers = [x for x in keeper.timers if x.get("kind") != "focus"]
            if not timers:
                return "neutral", "There's no timer running.", []
            return "happy", spoken, [("cancel", value)]

        m = LEFT_RE.match(t)
        if m:
            return "happy", _time_left(keeper, (m.group("label") or "").strip()), []

    for r in WEATHER_RES:
        m = r.match(t)
        if m:
            spoken = _weather(m.groupdict().get("day") or "", m.groupdict().get("ask") or "")
            if spoken is None:
                return None  # no recent forecast: let the AI say so
            return "happy", spoken, []
    return None


def _time_left(keeper, label):
    timers = [x for x in keeper.timers if x.get("kind") != "focus"]
    focus = [x for x in keeper.timers if x.get("kind") == "focus"]
    if label and "focus" not in label:
        named = [x for x in timers if label in (x.get("label") or "").lower()]
        timers = named or timers
    if label and "focus" in label:
        timers = []
    parts = []
    for x in timers[:3]:
        left = _say_length(keeper._left(x))
        name = x.get("label") or "timer"
        parts.append(f"{left} left on the {name} timer" if name != "timer" else f"{left} left on the timer")
    if focus and (not label or "focus" in label):
        parts.append(f"{_say_length(keeper._left(focus[0]))} left in your focus session")
    if not parts:
        return "There's no timer running right now."
    paused = any(x.get("paused") for x in timers)
    out = ", and ".join(parts) + (" (one is paused)" if paused else "") + "."
    return out[0].upper() + out[1:]


SKY_WORDS = {
    "Clear sky": "clear skies", "Mostly clear": "mostly clear", "Partly cloudy": "partly cloudy",
    "Cloudy": "cloudy", "Foggy": "foggy",
}


def _a(number):
    """'an 80%', 'a 60%'"""
    return "an" if str(number).startswith(("8", "11", "18")) else "a"


def _sky(sky):
    return SKY_WORDS.get(sky, (sky or "").lower())


def _weather(day, ask):
    w = weather.latest()
    if not w:
        return None
    days = w.get("days") or []
    now = w.get("now") or {}
    city = w.get("city") or "here"
    tomorrow = day == "tomorrow"
    if tomorrow and len(days) < 2:
        return None
    d = days[1] if tomorrow else (days[0] if days else {})
    when = "Tomorrow" if tomorrow else "Today"
    rain = int(d.get("rain") or 0)
    hi, lo = d.get("hi"), d.get("lo")

    def deg(v):
        return f"{round(float(v))}" if v is not None else "?"

    if ask in ("rain", "raining", "umbrella", "snow"):
        if rain >= 60:
            yes = "Yes, take an umbrella!" if ask == "umbrella" else "Yes, probably."
            return f"{yes} {when} there's {_a(rain)} {rain}% chance of rain in {city}."
        if rain >= 25:
            return f"Maybe. {when} there's {_a(rain)} {rain}% chance of rain in {city}."
        no = "No umbrella needed." if ask == "umbrella" else "Doesn't look like it."
        return f"{no} {when} there's only a {rain}% chance of rain in {city}."

    if ask in ("hot", "cold", "warm", "jacket", "coat"):
        if not tomorrow and now.get("temp") is not None:
            temp = float(now["temp"])
            line = f"It's {deg(temp)} degrees in {city} right now, going up to {deg(hi)} today."
        else:
            temp = float(hi) if hi is not None else 20
            line = f"{when} it goes from {deg(lo)} to {deg(hi)} degrees in {city}."
        if ask in ("jacket", "coat"):
            low = float(lo) if lo is not None else temp
            tip = "Yes, bring a jacket!" if low < 15 else ("Maybe a light one for the evening." if low < 19
                                                          else "No, it's warm enough.")
            return f"{tip} {line}"
        return line

    if tomorrow:
        out = f"Tomorrow in {city}: {_sky(d.get('sky'))}, {deg(lo)} to {deg(hi)} degrees"
    elif now.get("temp") is not None:
        out = f"It's {deg(now['temp'])} degrees and {_sky(now.get('sky'))} in {city}. Today goes up to {deg(hi)}"
    else:
        out = f"Today in {city}: {_sky(d.get('sky'))}, {deg(lo)} to {deg(hi)} degrees"
    if rain >= 25:
        out += f", with {_a(rain)} {rain}% chance of rain"
    return out + "."
