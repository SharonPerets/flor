"""Timers, focus sessions and reminders, set by voice.

They live on Gorgi (so they go off even when the PC is off). The AI asks for
them with tags at the end of its reply, like the memory tags:

    [timer: 10m | pasta]   [focus: 25m]   [remind: at 17:00 | call mom]   [cancel: pasta]

This module turns those tags into messages for Gorgi, with exact times worked
out from the PC's clock. Gorgi sends back its list ("time_list") whenever it
changes, so the AI can answer "how long is left?" and cancel things by name.
"""

import datetime as dt
import logging
import re
import time

log = logging.getLogger("gorgi.time")

TAG_RE = re.compile(r"\[(timer|focus|remind|reminder|cancel)\s*:\s*([^\]]+)\]", re.IGNORECASE)

DAY_NAMES = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"]
DAY_WORDS = {
    "sun": 0, "sunday": 0, "sundays": 0,
    "mon": 1, "monday": 1, "mondays": 1,
    "tue": 2, "tues": 2, "tuesday": 2, "tuesdays": 2,
    "wed": 3, "wednesday": 3, "wednesdays": 3,
    "thu": 4, "thur": 4, "thurs": 4, "thursday": 4, "thursdays": 4,
    "fri": 5, "friday": 5, "fridays": 5,
    "sat": 6, "saturday": 6, "saturdays": 6,
}
EVERY_DAY = 0x7F
WEEKDAYS = 0x1F   # Sunday to Thursday (the Israeli work week)
WEEKENDS = 0x60   # Friday and Saturday

PROMPT = """You can set timers, focus sessions and reminders on your screen. When asked, put a tag at the END of your reply:
[timer: 10m | pasta]  a timer; the length uses h, m and s (like 1h30m or 90s); the name after | is optional
[focus: 25m]  starts a focus session (25 minutes if no length is given); [focus: stop] ends it
[remind: in 20m | take the laundry out]
[remind: at 17:00 | call mom]  (today, or tomorrow if that time has passed)
[remind: tomorrow 08:30 | dentist appointment]
[remind: friday 17:00 | buy flowers]
[remind: 2026-11-03 09:00 | pay the bills]
[remind: daily 08:00 | take vitamins]
[remind: every sun,tue 19:00 | take the trash out]  (days: sun mon tue wed thu fri sat; weekdays = Sunday to Thursday, weekends = Friday and Saturday)
[cancel: pasta] cancels the timer or reminder with those words; also [cancel: focus], [cancel: all timers], [cancel: all reminders]
Use 24-hour times. Write the reminder text short, as what your friend should do.
Only use these tags when your friend asks. Confirm in a few words, like "Ten minutes, got it!"
Use the list below to answer questions like "how long is left?" or "what are my reminders?"."""


# ---------------------------------------------------------------- reading times


DURATION_RE = re.compile(
    r"(\d+(?:\.\d+)?)\s*(hours?|hrs?|h|minutes?|mins?|m|seconds?|secs?|s)(?![a-z])", re.IGNORECASE)


def parse_duration(text):
    """'1h30m' / '90 seconds' / '10 minutes' / '25' (minutes) -> seconds, or None."""
    text = text.strip().lower()
    total = 0.0
    found = False
    for m in DURATION_RE.finditer(text):
        value, unit = float(m.group(1)), m.group(2)[0]
        total += value * {"h": 3600, "m": 60, "s": 1}[unit]
        found = True
    if not found:
        m = re.fullmatch(r"(\d+(?:\.\d+)?)", text)
        if m:
            return int(round(float(m.group(1)) * 60))
        m = re.fullmatch(r"(\d+):(\d\d)", text)  # "1:30" = 1 minute 30 seconds
        if m:
            return int(m.group(1)) * 60 + int(m.group(2))
        if re.search(r"\b(half an hour|half hour)\b", text):
            return 1800
        if re.search(r"\b(an hour|one hour)\b", text):
            return 3600
        return None
    return int(round(total))


CLOCK_RE = re.compile(r"\b(\d{1,2})(?::(\d{2}))?\s*(am|pm|a\.m\.|p\.m\.)?(?=\s|$)", re.IGNORECASE)


def parse_clock(text):
    """'17:00' / '5pm' / '8:30 am' / 'noon' -> (hour, minute), or None."""
    t = text.lower()
    if "noon" in t or "midday" in t:
        return 12, 0
    if "midnight" in t:
        return 0, 0
    for m in CLOCK_RE.finditer(t):
        hour, minute, ampm = int(m.group(1)), int(m.group(2) or 0), (m.group(3) or "").replace(".", "")
        if m.group(2) is None and not ampm:
            continue  # a lone number ("3") isn't a time; might be a day of the month
        if ampm == "pm" and hour < 12:
            hour += 12
        if ampm == "am" and hour == 12:
            hour = 0
        if 0 <= hour < 24 and 0 <= minute < 60:
            return hour, minute
    # a lone number as the very last word: "at 5" -> 5:00 (17:00 in the evening hours is the AI's job)
    m = re.search(r"\bat (\d{1,2})$", t.strip())
    if m and int(m.group(1)) < 24:
        return int(m.group(1)), 0
    return None


def parse_days(text):
    """Days a repeating reminder goes off, as bits (Sunday = 1), or 0."""
    t = text.lower()
    if re.search(r"\b(daily|every ?day|each day)\b", t):
        return EVERY_DAY
    if re.search(r"\bweekdays?\b|\bworkdays?\b", t):
        return WEEKDAYS
    if re.search(r"\bweekends?\b", t):
        return WEEKENDS
    mask = 0
    for word in re.findall(r"[a-z]+", t):
        if word in DAY_WORDS:
            mask |= 1 << DAY_WORDS[word]
    return mask


def describe_left(seconds):
    seconds = max(0, int(seconds))
    h, rest = divmod(seconds, 3600)
    m, s = divmod(rest, 60)
    parts = []
    if h:
        parts.append(f"{h} hour{'s' if h != 1 else ''}")
    if m:
        parts.append(f"{m} minute{'s' if m != 1 else ''}")
    if s and not h:
        parts.append(f"{s} second{'s' if s != 1 else ''}")
    return " ".join(parts) or "0 seconds"


# ---------------------------------------------------------------- the keeper


class TimeKeeper:
    def __init__(self):
        self.known = False      # got a list from Gorgi (older Gorgi versions don't send one)
        self.timers = []        # [{"id", "kind", "label", "left", "paused"}]
        self.reminders = []     # [{"id", "text", "when", "repeat"}]
        self.focus_today = 0
        self.focus_minutes = 25
        self.max_timers = 3
        self.max_reminders = 20
        self.received = 0.0     # time.monotonic() of the list

    def update(self, msg):
        """A "time_list" message from Gorgi."""
        self.known = True
        self.timers = [t for t in msg.get("timers", []) if isinstance(t, dict)]
        self.reminders = [r for r in msg.get("reminders", []) if isinstance(r, dict)]
        self.focus_today = int(msg.get("focus_today") or 0)
        self.focus_minutes = int(msg.get("focus_minutes") or 25)
        self.max_timers = int(msg.get("max_timers") or 3)
        self.max_reminders = int(msg.get("max_reminders") or 20)
        self.received = time.monotonic()

    def forget(self):
        """Gorgi disconnected."""
        self.known = False

    def _left(self, t):
        left = float(t.get("left") or 0)
        if not t.get("paused"):
            left -= time.monotonic() - self.received
        return max(0, left)

    def prompt_block(self):
        if not self.known:
            return "Your timers and reminders can't be seen right now (you're not connected)."
        lines = []
        timers = [t for t in self.timers if t.get("kind") != "focus"]
        focus = [t for t in self.timers if t.get("kind") == "focus"]
        if timers:
            parts = [f"{t.get('label')}: {describe_left(self._left(t))} left{' (paused)' if t.get('paused') else ''}"
                     for t in timers]
            lines.append("Timers running on your screen: " + "; ".join(parts) + ".")
        else:
            lines.append("No timers are running.")
        if focus:
            f = focus[0]
            lines.append(f"A focus session is running: {describe_left(self._left(f))} left"
                         f"{' (paused)' if f.get('paused') else ''}.")
        if self.focus_today:
            lines.append(f"Focus sessions finished today: {self.focus_today}.")
        if self.reminders:
            parts = [f"{r.get('text')} ({r.get('when')})" for r in self.reminders]
            lines.append("Reminders coming up: " + "; ".join(parts) + ".")
        else:
            lines.append("No reminders are set.")
        return "\n".join(lines)

    # ------------------------------------------------------------ tags -> messages

    def handle(self, name, value, now=None):
        """One tag from the AI. Returns (messages for Gorgi, problem) - problem is a
        sentence to say instead of the AI's answer when it can't be done, else None."""
        now = now or dt.datetime.now()
        name = name.lower()
        if not self.known:  # Gorgi runs an older version without timers
            return [], "I need my new update for timers. Right-click my icon on your PC and update me!"
        try:
            if name == "timer":
                return self._timer(value)
            if name == "focus":
                return self._focus(value)
            if name in ("remind", "reminder"):
                return self._remind(value, now)
            if name == "cancel":
                return self._cancel(value)
        except Exception:
            log.exception("time tag %s: %s", name, value)
        return [], "Hmm, I got confused setting that up. Can you say it again?"

    def _timer(self, value):
        when, sep, label = value.partition("|")
        seconds = parse_duration(when)
        if not sep:  # "pasta 10m": the name is what's left without the length
            label = DURATION_RE.sub("", value)
        label = " ".join(label.split()).strip(" .,")[:31]
        if not seconds:
            return [], "Hmm, how long should the timer be?"
        if seconds > 24 * 3600:
            return [], "That's too long for a timer. Try a reminder instead!"
        if len([t for t in self.timers if t.get("kind") != "focus"]) >= self.max_timers:
            return [], f"I can only run {self.max_timers} timers at once. Want me to cancel one?"
        self.timers.append({"id": None, "kind": "timer", "label": label or "timer", "left": seconds, "paused": False})
        log.info("timer: %d s (%s)", seconds, label)
        return [{"type": "time_add", "kind": "timer", "seconds": seconds, "label": label}], None

    def _focus(self, value):
        v = value.strip().lower()
        focus = [t for t in self.timers if t.get("kind") == "focus"]
        if v in ("stop", "end", "cancel", "off", "done", "finish"):
            if not focus:
                return [], "There's no focus session running."
            return [{"type": "time_cancel", "id": t["id"]} for t in focus if t.get("id")], None
        if focus:
            return [], "We're already focusing! I'll cheer when it's done."
        seconds = parse_duration(v) if v not in ("", "start", "on", "default") else None
        seconds = seconds or self.focus_minutes * 60
        if seconds > 4 * 3600:
            return [], "That's a really long focus session. Let's keep it under four hours."
        self.timers.append({"id": None, "kind": "focus", "label": "Focus", "left": seconds, "paused": False})
        log.info("focus: %d s", seconds)
        return [{"type": "time_add", "kind": "focus", "seconds": seconds}], None

    def _remind(self, value, now):
        when, sep, text = value.partition("|")
        text = " ".join(text.split()).strip(" .")
        if not sep or not text:
            return [], "What should I remind you about?"
        if len(self.reminders) >= self.max_reminders:
            return [], f"I already have {self.max_reminders} reminders, that's my limit. Want me to delete one?"
        w = " ".join(when.lower().split())

        # in 20 minutes
        if w.startswith("in ") or w.startswith("after "):
            seconds = parse_duration(w.split(" ", 1)[1])
            if not seconds:
                return [], "Hmm, in how long should I remind you?"
            at = now + dt.timedelta(seconds=seconds)
            return self._remind_at(at, text)

        clock = parse_clock(w)
        # repeating: daily, weekdays, every monday...
        repeating = bool(re.search(r"\b(daily|every|each|weekdays?|weekends?|workdays?|everyday)\b", w))
        if repeating:
            days = parse_days(w)
            if not days:
                return [], "Which days should I remind you?"
            if not clock:
                return [], "At what time should I remind you?"
            minute = clock[0] * 60 + clock[1]
            self.reminders.append({"id": None, "text": text, "when": w, "repeat": True})
            log.info("repeating reminder: %s %02d:%02d %s", [DAY_NAMES[d] for d in range(7) if days >> d & 1],
                     clock[0], clock[1], text)
            return [{"type": "time_add", "kind": "remind", "text": text[:63], "minute": minute, "days": days}], None

        if not clock:
            return [], "At what time should I remind you?"
        hour, minute = clock
        m = re.search(r"\b(\d{4})-(\d{1,2})-(\d{1,2})\b", w)
        if m:
            date = dt.date(int(m.group(1)), int(m.group(2)), int(m.group(3)))
        elif "tomorrow" in w:
            date = now.date() + dt.timedelta(days=1)
        else:
            day = next((DAY_WORDS[x] for x in re.findall(r"[a-z]+", w) if x in DAY_WORDS), None)
            if day is not None:
                today = (now.weekday() + 1) % 7  # Python: Monday = 0; here Sunday = 0
                ahead = (day - today) % 7
                date = now.date() + dt.timedelta(days=ahead)
                if ahead == 0 and dt.datetime.combine(date, dt.time(hour, minute)) <= now:
                    date += dt.timedelta(days=7)
            else:  # just a time: today, or tomorrow if it has passed
                date = now.date()
                if dt.datetime.combine(date, dt.time(hour, minute)) <= now:
                    date += dt.timedelta(days=1)
        at = dt.datetime.combine(date, dt.time(hour, minute))
        if at <= now:
            return [], "That time has already passed. When should I remind you?"
        return self._remind_at(at, text)

    def _remind_at(self, at, text):
        epoch = int(at.timestamp())
        self.reminders.append({"id": None, "text": text, "when": at.strftime("%a %H:%M"), "repeat": False})
        log.info("reminder at %s: %s", at.strftime("%Y-%m-%d %H:%M"), text)
        return [{"type": "time_add", "kind": "remind", "text": text[:63], "at": epoch}], None

    def _cancel(self, value):
        v = " ".join(value.lower().split()).strip(" .")
        timers = [t for t in self.timers if t.get("kind") != "focus" and t.get("id")]
        focus = [t for t in self.timers if t.get("kind") == "focus" and t.get("id")]
        reminders = [r for r in self.reminders if r.get("id")]

        if v in ("everything", "all", "all of them"):
            chosen = timers + focus + reminders
        elif v in ("all timers", "timers", "every timer"):
            chosen = timers
        elif v in ("all reminders", "reminders", "every reminder"):
            chosen = reminders
        elif v in ("focus", "focus session", "the focus session"):
            chosen = focus
        elif v in ("timer", "the timer", "my timer"):
            if len(timers) > 1:
                names = ", ".join(t.get("label", "") for t in timers)
                return [], f"I have {len(timers)} timers: {names}. Which one?"
            chosen = timers
        elif v in ("reminder", "the reminder", "my reminder"):
            if len(reminders) > 1:
                return [], "I have a few reminders. Which one should I cancel?"
            chosen = reminders
        else:
            skip = {"the", "my", "a", "timer", "reminder", "to", "about", "for", "one", "that"}
            words = [w for w in re.findall(r"[a-z0-9']+", v) if w not in skip]
            if not words:
                return [], "Which one should I cancel?"

            def matches(item):
                hay = (item.get("label") or item.get("text") or "").lower()
                return all(w in hay for w in words)

            chosen = [x for x in timers + focus + reminders if matches(x)]
            if not chosen:  # be a bit more forgiving: any word
                chosen = [x for x in timers + focus + reminders
                          if any(w in (x.get("label") or x.get("text") or "").lower() for w in words if len(w) > 2)]
        if not chosen:
            return [], "I couldn't find that one."
        log.info("cancel: %s", [c.get("label") or c.get("text") for c in chosen])
        ids = {c["id"] for c in chosen}
        self.timers = [t for t in self.timers if t.get("id") not in ids]
        self.reminders = [r for r in self.reminders if r.get("id") not in ids]
        return [{"type": "time_cancel", "id": i} for i in ids], None


def extract(reply):
    """Takes the time tags out of a reply. Returns (reply without them, [(name, value)])."""
    tags = [(m.group(1).lower(), m.group(2).strip()) for m in TAG_RE.finditer(reply)]
    return TAG_RE.sub("", reply), tags
