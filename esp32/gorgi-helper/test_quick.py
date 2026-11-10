"""Tests for quick.py (timers and weather without the AI). Run: python test_quick.py"""

import time

import quick
import timekeeper
import weather


def keeper(timers=()):
    k = timekeeper.TimeKeeper()
    k.update({"timers": list(timers), "reminders": []})
    return k


def check(text, k, expect):
    got = quick.answer(text, k)
    if expect is None:
        assert got is None, f"{text!r} should go to the AI, got {got}"
        return
    assert got is not None, f"{text!r} should be quick"
    mood, spoken, actions = got
    print(f"  {text!r} -> {spoken!r} {actions}")
    if callable(expect):
        assert expect(spoken, actions), f"{text!r}: {spoken!r} {actions}"
    return got


def secs(actions):
    name, value = actions[0]
    return name, timekeeper.parse_duration(value.split("|")[0]), value.split("|")[1].strip()


k = keeper()
for text, want, label in [
    ("Set a timer for 10 minutes.", 600, ""),
    ("set a timer for ten minutes", 600, ""),
    ("Hey Gorgi, set a timer for 5 minutes for the pasta.", 300, "pasta"),
    ("Can you set a 3 minute timer for tea please?", 180, "tea"),
    ("Timer for 90 seconds", 90, ""),
    ("Start a timer for an hour and a half", 5400, ""),
    ("set a timer for half an hour", 1800, ""),
    ("set a timer for 1 hour and 15 minutes", 4500, ""),
    ("10 minute timer", 600, ""),
    ("Set a twenty five minute timer", 1500, ""),
    ("Timer 2 minutes.", 120, ""),
]:
    got = check(text, k, lambda s, a: True)
    name, seconds, lab = secs(got[2])
    assert name == "timer" and seconds == want and lab == label, (text, name, seconds, lab)

# goes to the AI
for text in ["Set a timer for 10 minutes and play some music", "what's a good timer app",
             "remind me in 10 minutes to call mom", "how are you", "set a reminder for tomorrow at 5",
             "set a timer"]:
    check(text, k, None)

# cancel / left
check("cancel the timer", k, lambda s, a: a == [] and "no timer" in s.lower())
k = keeper([{"id": 7, "kind": "timer", "label": "pasta", "left": 125, "paused": False},
            {"id": 8, "kind": "focus", "label": "Focus", "left": 600, "paused": False}])
check("Cancel the timer.", k, lambda s, a: a == [("cancel", "the timer")])
check("stop the pasta timer", k, lambda s, a: a == [("cancel", "pasta")])
check("cancel all timers", k, lambda s, a: a == [("cancel", "all timers")])
check("How much time is left?", k, lambda s, a: "pasta" in s and "2 minutes" in s)
check("how long is left on the focus session", k, lambda s, a: "focus" in s and "pasta" not in s)

# not connected: timers go to the AI (which says it needs an update)
nk = timekeeper.TimeKeeper()
check("set a timer for 10 minutes", nk, None)

# weather
check("what's the weather", k, None)  # no forecast yet
weather.update({"city": "Nahariya", "now": {"temp": 21.6, "sky": "Partly cloudy"},
                "days": [{"day": "Today", "sky": "Partly cloudy", "hi": 24, "lo": 17, "rain": 10},
                         {"day": "Wed", "sky": "Rain", "hi": 19, "lo": 14, "rain": 80}]})
check("What's the weather like?", k, lambda s, a: "22 degrees" in s and "Nahariya" in s and "24" in s)
check("what's the weather tomorrow", k, lambda s, a: "Tomorrow" in s and "rain" in s and "80%" in s)
check("Will it rain tomorrow?", k, lambda s, a: s.startswith("Yes"))
check("is it going to rain today", k, lambda s, a: "Doesn't look like it" in s)
check("Do I need an umbrella tomorrow?", k, lambda s, a: "umbrella" in s)
check("Is it cold outside?", k, lambda s, a: "22 degrees" in s)
check("What's the temperature?", k, lambda s, a: "22" in s)
check("What's the weather in London?", k, None)
check("What's the weather on Friday", k, None)
weather._received = time.time() - 4 * 3600  # old forecast: let the AI handle it
check("what's the weather", k, None)
print("quick: all good")
