import sys, datetime as dt
sys.path.insert(0, __import__("os").path.dirname(__file__))  # run: python test_timekeeper.py
from timekeeper import TimeKeeper, parse_duration, parse_clock, extract
for t, want in [("10m", 600), ("1h30m", 5400), ("90s", 90), ("10 minutes", 600), ("25", 1500), ("1.5h", 5400), ("2 hours", 7200), ("half an hour", 1800), ("5 mins", 300)]:
    assert parse_duration(t) == want, (t, parse_duration(t))
for t, want in [("at 17:00", (17, 0)), ("5pm", (17, 0)), ("8:30 am", (8, 30)), ("noon", (12, 0)), ("tomorrow 08:30", (8, 30)), ("2026-11-03 09:00", (9, 0)), ("every sun,tue 19:00", (19, 0)), ("12am", (0, 0))]:
    assert parse_clock(t) == want, (t, parse_clock(t))
now = dt.datetime(2026, 10, 21, 9, 0)  # a Wednesday
k = TimeKeeper()
k.update({"timers": [], "reminders": [], "focus_today": 1, "focus_minutes": 25})
def go(name, value):
    msgs, problem = k.handle(name, value, now)
    print(f"[{name}: {value}] ->", msgs, "|", problem)
    return msgs, problem
go("timer", "10m | pasta")
go("timer", "pasta 5 minutes")
go("timer", "1h30m")
m, p = go("timer", "3m | tea"); assert p and "3 timers" in p
go("focus", "")
m, p = go("focus", "25m"); assert p
for when in ["in 20m", "at 17:00", "at 08:00", "tomorrow 08:30", "friday 17:00", "wednesday 08:00", "2026-11-03 09:00",
             "daily 08:00", "every sun,tue 19:00", "weekdays 07:30", "every monday 8pm"]:
    m, p = go("remind", f"{when} | do the thing")
    assert m and not p, when
    if "at" in m[0]:
        print("    ->", dt.datetime.fromtimestamp(m[0]["at"]))
go("remind", "at 17:00")
go("remind", "2020-01-01 09:00 | old")
# cancel by name
k.update({"timers": [{"id": 3, "kind": "timer", "label": "pasta", "left": 300, "paused": False},
                     {"id": 4, "kind": "timer", "label": "tea", "left": 100, "paused": True},
                     {"id": 5, "kind": "focus", "label": "Focus", "left": 1000, "paused": False}],
          "reminders": [{"id": 9, "text": "call mom", "when": "Today 17:00", "repeat": False},
                        {"id": 10, "text": "take vitamins", "when": "Every day 08:00", "repeat": True}]})
print(k.prompt_block())
assert go("cancel", "pasta")[0] == [{"type": "time_cancel", "id": 3}]
assert go("cancel", "the timer")[0] == [{"type": "time_cancel", "id": 4}]
assert go("cancel", "focus")[0] == [{"type": "time_cancel", "id": 5}]
assert go("cancel", "vitamin reminder")[0] == [{"type": "time_cancel", "id": 10}]
assert go("cancel", "dinner")[1]
assert len(go("cancel", "all reminders")[0]) == 1
print(extract("[happy] Ten minutes, got it! [timer: 10m | pasta] [remember: x]"))
print("ALL OK")
