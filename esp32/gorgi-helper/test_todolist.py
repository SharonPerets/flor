import datetime as dt
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # run: python test_todolist.py
import brain  # noqa: E402
from todolist import TodoKeeper, extract, find, parse_spoken, parse_when  # noqa: E402

now = dt.datetime(2026, 10, 24, 10, 0)  # a Saturday morning
today = now.date()
D = dt.timedelta

# ---- spoken tasks
cases = [
    ("buy milk tomorrow", "Buy milk", today + D(days=1), False, None),
    ("Buy milk and eggs.", "Buy milk and eggs", None, False, None),
    ("Add call the bank to my list", "Call the bank", None, False, None),
    ("remind me to call mom on Friday", "Call mom", dt.date(2026, 10, 30), False, None),
    ("I need to book the dentist tomorrow at 5", "Book the dentist", today + D(days=1), False,
     dt.datetime(2026, 10, 25, 17, 0)),
    ("Renew the passport, it's important", "Renew the passport", None, True, None),
    ("learn to juggle someday", "Learn to juggle", None, False, None),
    ("tomorrow buy flowers", "Buy flowers", today + D(days=1), False, None),
    ("Send the report next week", "Send the report", dt.date(2026, 10, 25), False, None),
    ("pay the bills today at 18:30", "Pay the bills", today, False, dt.datetime(2026, 10, 24, 18, 30)),
    ("Call Dana at 9 pm", "Call Dana", today, False, dt.datetime(2026, 10, 24, 21, 0)),
    ("Read chapter 5", "Read chapter 5", None, False, None),
    ("Okay, add water the plants.", "Water the plants", None, False, None),
]
for said, text, due, star, at in cases:
    t = parse_spoken(said, now)
    print(f"{said!r:45} -> {t}")
    assert t["text"] == text, (said, t)
    assert t["due"] == due, (said, t)
    assert t["star"] == star, (said, t)
    assert t["at"] == at, (said, t)
assert parse_spoken("learn to juggle someday", now)["someday"]
assert parse_when("friday 17:00", now)["at"] == dt.datetime(2026, 10, 30, 17, 0)

# ---- finding tasks by name
tasks = [{"id": 1, "text": "Call the bank about the card"}, {"id": 2, "text": "Book the dentist"},
         {"id": 3, "text": "Buy milk and eggs"}, {"id": 4, "text": "Buy flowers for Noa"}]
assert [t["id"] for t in find("the bank thing", tasks)] == [1]
assert [t["id"] for t in find("dentists", tasks)] == [2]
assert [t["id"] for t in find("milk", tasks)] == [3]
assert len(find("buy", tasks)) == 2  # not clear: ask
assert find("walk the dog", tasks) == []

# ---- the keeper and AI tags
k = TodoKeeper()
msgs, problem = k.handle("add buy milk", now)
assert problem and "update" in problem  # Gorgi hasn't sent a list: old firmware
k.update({"type": "todo_list", "today": "2026-10-24", "max": 40, "tasks": [
    {"id": 1, "text": "Call the bank about the card", "due": "2026-10-24", "star": True},
    {"id": 2, "text": "Review Dana's pull request", "due": "2026-10-22"},
    {"id": 3, "text": "Buy milk and eggs", "due": "2026-10-25"},
    {"id": 4, "text": "Learn to juggle", "someday": True},
    {"id": 5, "text": "Send the weekly report", "done": True},
]})
print(k.prompt_block(now))


def go(value):
    msgs, problem = k.handle(value, now)
    print(f"[todo: {value}] ->", msgs, "|", problem)
    return msgs, problem


m, p = go("add book the dentist | tomorrow 17:00")
assert m[0]["text"] == "Book the dentist" and m[0]["due"] == "2026-10-25" and "at" in m[0]
m, p = go("add renew the passport | friday important")
assert m[0]["due"] == "2026-10-30" and m[0]["star"]
m, p = go("add water the plants")
assert m == [{"type": "todo_add", "text": "Water the plants"}]
assert go("done the bank thing")[0] == [{"type": "todo_done", "id": 1, "done": True}]
assert go("done bank")[1] == "That one is already done!"
assert go("undone weekly report")[0] == [{"type": "todo_done", "id": 5, "done": False}]
assert go("remove juggling")[0] == [{"type": "todo_remove", "id": 4}]
assert "couldn't find" in go("done walk the dog")[1]
assert go("complete the pull request")[0][0]["id"] == 2

# ---- tags come out of the reply
mood, spoken, actions = brain.parse_reply("[happy] Added! [todo: add buy milk | tomorrow] [todo: done bank]")
print(mood, spoken, actions)
assert spoken == "Added!" and actions == [("todo", "add buy milk | tomorrow"), ("todo", "done bank")]
print(extract("Sure [TODO: remove milk]"))

# ---- Hebrew (4.8)
import lang  # noqa: E402

lang.bind({"language": "he"})


def heb(said, text, due=None, at=None, star=False, someday=False):
    t = parse_spoken(said, now)
    print(f"{said!r} ->", t)
    assert t["text"] == text, t["text"]
    assert t["due"] == due, t["due"]
    assert (t["at"].strftime("%H:%M") if t["at"] else None) == at, t["at"]
    assert t["star"] == star and t["someday"] == someday


import datetime as _dt  # noqa: E402

D = now.date()
heb("תוסיף לקנות חלב מחר", "לקנות חלב", D + _dt.timedelta(days=1))
heb("לקנות חלב מחר בחמש", "לקנות חלב", D + _dt.timedelta(days=1), "17:00")
heb("אני צריך להתקשר לבנק היום ב-10:30", "להתקשר לבנק", D, "10:30")
heb("תזכיר לי לשלם חשבון חשמל ביום שישי, זה חשוב", "לשלם חשבון חשמל", D + _dt.timedelta(days=6), star=True)
heb("לסדר את המחסן מתישהו", "לסדר את המחסן", someday=True)
heb("משימה חדשה: לקבוע תור לרופא שיניים בשבוע הבא", "לקבוע תור לרופא שיניים", D + _dt.timedelta(days=1))
heb("לקנות מתנה לנועה", "לקנות מתנה לנועה")
heb("מחר בשמונה בבוקר ריצה", "ריצה", D + _dt.timedelta(days=1), "08:00")
long_text = "לסדר " * 40
assert len(parse_spoken(long_text, now)["text"].encode()) <= 112

k.update({"type": "todo_list", "today": "2026-10-24", "tasks": [
    {"id": 11, "text": "להתקשר לבנק על הכרטיס"},
    {"id": 12, "text": "לקנות חלב וביצים", "due": "2026-10-25"},
    {"id": 13, "text": "לשלם חשבון חשמל", "done": True},
]})
assert go("done הבנק")[0] == [{"type": "todo_done", "id": 11, "done": True}]
assert go("done חלב")[0] == [{"type": "todo_done", "id": 12, "done": True}]
assert go("undone חשבון החשמל")[0] == [{"type": "todo_done", "id": 13, "done": False}]
assert "לא מצאתי" in go("remove לטייל עם הכלב")[1]
m, p = go("add לקנות פרחים | friday")
assert m[0]["text"] == "לקנות פרחים" and m[0]["due"] == "2026-10-30"
lang.bind({})
print("ALL OK")
