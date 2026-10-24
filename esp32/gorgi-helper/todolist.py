"""The to-do list on Gorgi's To-do screen, for the AI and for voice adding.

The list lives on Gorgi. Gorgi sends it ("todo_list") whenever it changes; the
AI sees it and changes it with tags at the end of its reply:

    [todo: add buy milk | tomorrow]   [todo: done call the bank]   [todo: remove buy milk]

This module turns those tags into messages for Gorgi, finds tasks by name
(forgiving: "the bank thing" finds "Call the bank about the card"), and turns
spoken words into a task with a day ("buy milk tomorrow" -> "Buy milk", tomorrow).
"""

import datetime as dt
import difflib
import logging
import os
import re

import lang
import timekeeper

log = logging.getLogger("gorgi.todo")

TAG_RE = re.compile(r"\[(todo|todos|to-do)\s*:\s*([^\]]+)\]", re.IGNORECASE)

PROMPT = """Your friend has a to-do list on your screen (shown below). When asked, change it with a tag at the END of your reply:
[todo: add buy milk]  adds a task (write it short, like a to-do item, starting with a verb)
[todo: add call the bank | friday]  with a day: today, tomorrow, a weekday, next week, someday or 2026-11-03
[todo: add book the dentist | tomorrow 17:00]  with a time it also rings as a reminder
[todo: add renew the passport | important]  stars it ("important" can go with a day: friday important)
[todo: done call the bank]  ticks a task off (use words from the task)
[todo: undone call the bank]  puts a ticked task back
[todo: remove buy milk]  deletes a task
Use one tag per task. Use these tags whenever your friend says they need to do something, asks to add, finish,
tick, complete or remove a to-do, or says they did something that is on the list.
"Remind me at 5 to ..." without a list means a [remind:] reminder; "add to my list" means a to-do.
Confirm in a few words, like "Added!" or "Nice, ticked off!". To answer "what's on my list?", read the list below."""

WEEKDAYS = timekeeper.DAY_WORDS
STOP_WORDS = {"the", "a", "an", "my", "to", "for", "about", "with", "on", "of", "and", "thing", "task", "todo",
              "to-do", "item", "one", "that", "this", "it", "please", "list", "from", "off", "in", "at", "i",
              "את", "של", "על", "עם", "זה", "הזה", "הזאת", "לי", "שלי", "אני", "משימה", "המשימה", "רשימה",
              "מהרשימה", "ברשימה", "לרשימה", "בבקשה", "גם", "כבר", "הדבר", "דבר"}
TEXT_BYTES = 112  # Gorgi keeps 112 bytes of a task (a Hebrew letter takes 2)


# ---------------------------------------------------------------- days


def _next_weekday(today, day):
    """The next such weekday (Sunday = 0); today counts as 'this' day."""
    py_today = (today.weekday() + 1) % 7  # Python: Monday = 0; here Sunday = 0
    return today + dt.timedelta(days=(day - py_today) % 7)


HE_DAYS = {"ראשון": 0, "שני": 1, "שלישי": 2, "רביעי": 3, "חמישי": 4, "שישי": 5, "שבת": 6}
HE_NUMBERS = {"אחת": 1, "שתיים": 2, "שתים": 2, "שלוש": 3, "ארבע": 4, "חמש": 5, "שש": 6, "שבע": 7, "שמונה": 8,
              "תשע": 9, "עשר": 10, "אחת עשרה": 11, "שתיים עשרה": 12, "שתים עשרה": 12}
_NB = r"(?<![\w])"   # not in the middle of a word
_NA = r"(?![\w])"
HE_DAY_RE = re.compile(_NB + r"(?:[בל]?יום |[בל])(ראשון|שני|שלישי|רביעי|חמישי|שישי|שבת)" + _NA)
HE_NUMBER_WORDS = "|".join(sorted(HE_NUMBERS, key=len, reverse=True))
HE_CLOCK_RE = re.compile(
    _NB + r"(?:בשעה |ב-?|עד |לקראת )(?:(\d{1,2})(?::(\d{2}))?|(" + HE_NUMBER_WORDS + r"))( וחצי| ורבע)?" + _NA)


def _parse_when_hebrew(w, now, out):
    """The Hebrew words for days and times (parse_when calls this when the text has Hebrew)."""
    if re.search(_NB + r"(חשוב|חשובה|דחוף|דחופה|עם כוכב|בכוכב)" + _NA, w):
        out["star"] = True
    if re.search(_NB + r"(מתישהו|מתי שהוא|יום אחד|בלי תאריך)" + _NA, w):
        out["someday"] = True
        return out
    today = now.date()
    if re.search(_NB + r"[ולב]?מחרתיים" + _NA, w):
        out["due"] = today + dt.timedelta(days=2)
    elif re.search(_NB + r"[ולב]?מחר" + _NA, w):
        out["due"] = today + dt.timedelta(days=1)
    elif re.search(_NB + r"[ולב]?(היום|הערב|הלילה|הבוקר)" + _NA, w):
        out["due"] = today
    elif re.search(_NB + r"[בל]?שבוע הבא" + _NA, w):
        out["due"] = _next_weekday(today + dt.timedelta(days=1), 0)  # the coming Sunday
    elif re.search(_NB + r"(?:[בל]?סופ\"?ש|[בל]סוף השבוע)" + _NA, w):
        out["due"] = _next_weekday(today, 5)
    elif re.search(r"בעוד (\d+) ימים", w):
        out["due"] = today + dt.timedelta(days=int(re.search(r"בעוד (\d+) ימים", w).group(1)))
    elif "בעוד יומיים" in w:
        out["due"] = today + dt.timedelta(days=2)
    elif "בעוד שבוע" in w:
        out["due"] = today + dt.timedelta(days=7)
    else:
        m = HE_DAY_RE.search(w)
        if m:
            out["due"] = _next_weekday(today, HE_DAYS[m.group(1)])
    m = HE_CLOCK_RE.search(w)
    if m:
        hour = int(m.group(1)) if m.group(1) else HE_NUMBERS[m.group(3)]
        minute = int(m.group(2) or 0)
        if m.group(4) == " וחצי":
            minute = 30
        elif m.group(4) == " ורבע":
            minute = 15
        evening = re.search(r"(בערב|אחר הצהריים|אחה\"צ|בלילה|הערב)", w)
        morning = re.search(r"(בבוקר|הבוקר)", w)
        if hour < 12 and (evening or (not morning and m.group(2) is None and 1 <= hour < 8)):
            hour += 12  # "בחמש" is in the afternoon
        if 0 <= hour < 24 and 0 <= minute < 60:
            date = out["due"] or today
            at = dt.datetime.combine(date, dt.time(hour, minute))
            if at <= now and out["due"] is None:
                at += dt.timedelta(days=1)
            if at > now:
                out["at"] = at
                out["due"] = at.date()
    return out


def parse_when(text, now):
    """'tomorrow' / 'friday 17:00' / 'next week' / 'someday' / '2026-11-03' / 'important' ->
    {"due": date or None, "someday": bool, "at": datetime or None, "star": bool}"""
    w = " " + " ".join(text.lower().split()) + " "
    out = {"due": None, "someday": False, "at": None, "star": False}
    if lang.has_hebrew(text):
        _parse_when_hebrew(w.strip(), now, out)
        if out["someday"] or out["due"] or out["at"]:
            return out
    if re.search(r"\b(important|urgent|starred|star|priority|high priority)\b", w):
        out["star"] = True
    if re.search(r"\b(someday|some day|sometime|one day|eventually|later on|no date)\b", w):
        out["someday"] = True
        return out
    today = now.date()
    m = re.search(r"\b(\d{4})-(\d{1,2})-(\d{1,2})\b", w)
    if m:
        try:
            out["due"] = dt.date(int(m.group(1)), int(m.group(2)), int(m.group(3)))
        except ValueError:
            pass
    elif re.search(r"\b(day after tomorrow)\b", w):
        out["due"] = today + dt.timedelta(days=2)
    elif re.search(r"\b(tomorrow|tmrw|tomorow)\b", w):
        out["due"] = today + dt.timedelta(days=1)
    elif re.search(r"\b(today|tonight|this (morning|afternoon|evening))\b", w):
        out["due"] = today
    elif re.search(r"\bnext week\b", w):
        out["due"] = _next_weekday(today + dt.timedelta(days=1), 0)  # the coming Sunday
    elif re.search(r"\b(this weekend|weekend)\b", w):
        out["due"] = _next_weekday(today, 5)  # Friday
    elif re.search(r"\bin (\d+) days?\b", w):
        out["due"] = today + dt.timedelta(days=int(re.search(r"\bin (\d+) days?\b", w).group(1)))
    elif re.search(r"\bin a week\b", w):
        out["due"] = today + dt.timedelta(days=7)
    else:
        # full day names only ("sat" or "wed" could be other words)
        day = next((WEEKDAYS[x] for x in re.findall(r"[a-z]+", w) if x in WEEKDAYS and len(x) > 3), None)
        if day is not None:
            out["due"] = _next_weekday(today, day)
    clock = timekeeper.parse_clock(w.strip())
    if clock:
        hour, minute = clock
        said_ampm = re.search(r"\d\s*(am|pm|a\.m\.|p\.m\.)", w) or ":" in w
        if hour < 12 and (re.search(r"\b(tonight|evening|afternoon)\b", w) or (not said_ampm and 1 <= hour < 8)):
            hour += 12  # "at 5" is in the afternoon
        date = out["due"] or today
        at = dt.datetime.combine(date, dt.time(hour, minute))
        if at <= now and out["due"] is None:  # just a time that has passed: tomorrow
            at += dt.timedelta(days=1)
        if at > now:
            out["at"] = at
            out["due"] = at.date()
    return out


# words that say *when*, taken off the end (or the start) of a spoken task
WHEN_RE = re.compile(
    r"(?:\s*,?\s*\b(?:"
    r"(?:on |by |for |this |next |until )?(?:sunday|monday|tuesday|wednesday|thursday|friday|saturday)s?"
    r"|(?:by |for )?(?:the )?day after tomorrow|(?:by |for )?tomorrow(?: morning| afternoon| evening| night)?"
    r"|(?:by |for )?today|tonight|this (?:morning|afternoon|evening|weekend)|next week|(?:in )?a week"
    r"|in \d+ days?|someday|some day|sometime|eventually"
    r"|(?:at |by |around )?\d{1,2}(?::\d{2})?\s*(?:am|pm|a\.m\.|p\.m\.)|at \d{1,2}(?::\d{2})?|at noon|noon"
    r"|on \d{4}-\d{1,2}-\d{1,2}|\d{4}-\d{1,2}-\d{1,2}"
    r"|(?:it's |it is |that's |this is )?(?:important|urgent|high priority)"
    r")\b\.?)+\s*$",
    re.IGNORECASE)

LEAD_RE = re.compile(
    r"^(?:(?:ok(?:ay)?|hey|so|um+|uh+|please|gorgi)[,.!]?\s+)*"
    r"(?:(?:can you |could you |please |would you )?(?:add|put|write(?: down)?|note(?: down)?|create(?: a)? (?:task|to-?do)"
    r"(?: to)?)\s+|remind me to\s+|i (?:need|have|want|got|gotta|should|must) to\s+|i need\s+|i have to\s+"
    r"|(?:a )?(?:new )?(?:task|to-?do)[:,]?\s+|don'?t forget to\s+|need to\s+|have to\s+|gotta\s+)",
    re.IGNORECASE)

TAIL_RE = re.compile(r"\s*,?\s*(?:to|on|onto|in) (?:my|the) (?:to-?do )?(?:list|to-?dos?|tasks?)\.?\s*$", re.IGNORECASE)

# the same in Hebrew: "תוסיף לרשימה לקנות חלב מחר בחמש" -> "לקנות חלב"
HE_WHEN_PART = (
    r"[ולב]?מחרתיים|[ולב]?מחר(?: בבוקר| בערב| בצהריים)?|[ולב]?היום|הערב|הלילה|הבוקר|[בל]?שבוע הבא|[בל]?סופ\"?ש"
    r"|בסוף השבוע|בעוד \d+ ימים|בעוד יומיים|בעוד שבוע|מתישהו|יום אחד"
    r"|(?:[בל]?יום |[בל])(?:ראשון|שני|שלישי|רביעי|חמישי|שישי|שבת)"
    r"|(?:בשעה |ב-?|עד )(?:\d{1,2}(?::\d{2})?|" + HE_NUMBER_WORDS + r")(?: וחצי| ורבע)?(?: בבוקר| בערב| בלילה| אחר הצהריים)?"
    r"|(?:זה )?(?:חשוב|חשובה|דחוף|דחופה)")
HE_WHEN_RE = re.compile(r"(?:\s*,?\s*(?:" + HE_WHEN_PART + r")[.!]?)+\s*$")
HE_WHEN_START_RE = re.compile(r"^(?:(?:" + HE_WHEN_PART + r")[,]?\s+)+")
HE_LEAD_RE = re.compile(
    r"^(?:(?:היי|הי|אוקיי|טוב|בבקשה|גורגי|תגיד|תגידי)[,.!]?\s+)*"
    r"(?:(?:תוסיף|תוסיפי|להוסיף|תרשום|תרשמי|לרשום|תכתוב|תכתבי|לכתוב|תשים|תשימי|תזכיר לי|תזכירי לי"
    r"|אני צריך|אני צריכה|צריך|צריכה|אני חייב|אני חייבת|חייב|חייבת|אל תשכח|אל תשכחי|משימה חדשה|משימה)[:,]?\s+)"
    r"(?:(?:ל|אל ה|ב)?(?:רשימה|משימות|רשימת המשימות|רשימה שלי)[:,]?\s+)?")
HE_TAIL_RE = re.compile(r"\s*,?\s*(?:ל|אל ה|ב)(?:רשימה|משימות|רשימת המשימות|רשימה שלי)\.?\s*$")


def parse_spoken(text, now=None):
    """What was said on the To-do screen -> {"text", "due", "someday", "at", "star"}."""
    now = now or dt.datetime.now()
    said = " ".join((text or "").split()).strip(" .!?")
    when = parse_when(said, now)
    task = said
    for _ in range(3):  # "add buy milk to my list tomorrow" etc.
        before = task
        task = LEAD_RE.sub("", task, count=1)
        task = TAIL_RE.sub("", task)
        task = WHEN_RE.sub("", task).strip(" ,.!?")
        if task == before:
            break
    # "tomorrow buy milk": a day at the start
    m = re.match(r"^(today|tomorrow|tonight|on (?:sun|mon|tue|wed|thu|fri|sat)[a-z]*|next week|someday)[, ]+(.+)$",
                 task, re.IGNORECASE)
    if m:
        task = m.group(2)
        task = LEAD_RE.sub("", task, count=1)
    if lang.has_hebrew(task):
        for _ in range(3):
            before = task
            task = HE_LEAD_RE.sub("", task, count=1)
            task = HE_TAIL_RE.sub("", task)
            task = HE_WHEN_RE.sub("", task).strip(" ,.!?")
            task = HE_WHEN_START_RE.sub("", task)  # "מחר בחמש לקנות חלב"
            if task == before:
                break
    task = task.strip(" ,.!?") or said
    task = task[:1].upper() + task[1:]
    return {"text": lang.cut_bytes(task, TEXT_BYTES), "due": when["due"], "someday": when["someday"], "at": when["at"],
            "star": when["star"]}


def to_message(task, msg_type="todo_add"):
    """A parsed task -> a message for Gorgi."""
    msg = {"type": msg_type, "text": task["text"]}
    if task.get("due"):
        msg["due"] = task["due"].isoformat()
    if task.get("someday"):
        msg["someday"] = True
    if task.get("star"):
        msg["star"] = True
    if task.get("at"):
        msg["at"] = int(task["at"].timestamp())
    return msg


def describe_day(due, today):
    if due is None:
        return ""
    days = (due - today).days
    if days < 0:
        return f"late, was due {due.strftime('%a %d %b')}"
    if days == 0:
        return "today"
    if days == 1:
        return "tomorrow"
    if days < 7:
        return due.strftime("%A")
    return due.strftime("%a %d %b")


# ---------------------------------------------------------------- finding a task by name


def _stem(word):
    """Hebrew glues little words to the front ("and", "the", "to", "in"...): "לבנק", "הבנק" -> "בנק"."""
    if not lang.has_hebrew(word):
        return word
    if len(word) >= 4 and word[0] == "ו":
        word = word[1:]
    if len(word) >= 4 and word[0] in "הבלמשכ":
        word = word[1:]
    return word


def _words(text):
    return [_stem(w) for w in re.findall(r"[\w']+", text.lower()) if w not in STOP_WORDS]


def _word_match(a, b):
    if a == b:
        return 1.0
    if len(a) >= 4 and len(b) >= 4 and (a.startswith(b) or b.startswith(a)):
        return 0.9  # "dentist" ~ "dentists"
    common = len(os.path.commonprefix([a, b]))
    if common >= 4 and common >= 0.7 * min(len(a), len(b)):
        return 0.85  # "juggling" ~ "juggle"
    if len(a) >= 4 and len(b) >= 4:
        r = difflib.SequenceMatcher(None, a, b).ratio()
        return r if r >= 0.8 else 0.0
    return 0.0


def score(query, text):
    """How well spoken words fit a task: 0 (not at all) .. 1."""
    q, t = _words(query), _words(text)
    if not q or not t:
        return 0.0
    hits = sum(max(_word_match(w, x) for x in t) for w in q)
    return hits / len(q)


def find(query, tasks):
    """The tasks that fit best (one when it's clear, several when it isn't, none when nothing fits)."""
    scored = sorted(((score(query, t.get("text", "")), t) for t in tasks), key=lambda x: -x[0])
    scored = [(s, t) for s, t in scored if s >= 0.5]
    if not scored:
        return []
    best = scored[0][0]
    close = [t for s, t in scored if s >= best - 0.05]
    return close


# ---------------------------------------------------------------- the keeper


class TodoKeeper:
    def __init__(self):
        self.known = False  # got a list from Gorgi (older versions don't have a to-do list)
        self.tasks = []     # [{"id", "text", "due", "someday", "star", "done", "at"}]
        self.today = None
        self.max_open = 40

    def update(self, msg):
        """A "todo_list" message from Gorgi."""
        self.known = True
        self.tasks = [t for t in msg.get("tasks", []) if isinstance(t, dict)]
        self.max_open = int(msg.get("max") or 40)
        try:
            self.today = dt.date.fromisoformat(msg.get("today")) if msg.get("today") else None
        except ValueError:
            self.today = None

    def forget(self):
        self.known = False

    @staticmethod
    def _due(t):
        try:
            return dt.date.fromisoformat(t["due"]) if t.get("due") else None
        except ValueError:
            return None

    def prompt_block(self, now=None):
        if not self.known:
            return "Your friend's to-do list can't be seen right now."
        today = (now or dt.datetime.now()).date()
        open_ = [t for t in self.tasks if not t.get("done")]
        done = [t for t in self.tasks if t.get("done")]
        if not open_ and not done:
            return "Your friend's to-do list is empty."

        def line(t):
            bits = []
            if t.get("star"):
                bits.append("important")
            if t.get("someday"):
                bits.append("someday")
            day = describe_day(self._due(t), today)
            if day:
                bits.append(day)
            if t.get("at"):
                bits.append("reminder at " + dt.datetime.fromtimestamp(int(t["at"])).strftime("%H:%M"))
            return t.get("text", "") + (f" ({', '.join(bits)})" if bits else "")

        now_list = [t for t in open_ if not t.get("someday") and (self._due(t) is None or self._due(t) <= today)]
        later = [t for t in open_ if t not in now_list]
        lines = ["Your friend's to-do list (on your screen):"]
        lines.append("Today: " + ("; ".join(line(t) for t in now_list) if now_list else "nothing left") + ".")
        if later:
            lines.append("Upcoming: " + "; ".join(line(t) for t in later) + ".")
        if done:
            lines.append("Finished lately: " + "; ".join(t.get("text", "") for t in done[-8:]) + ".")
        return "\n".join(lines)

    def handle(self, value, now=None):
        """One [todo: ...] tag. Returns (messages for Gorgi, problem) - problem is said instead of
        the AI's answer when it can't be done, else None."""
        now = now or dt.datetime.now()
        if not self.known:
            return [], lang.t("I need my new update for the to-do list. Right-click my icon on your PC and update me!", "אני צריכה את העדכון החדש שלי בשביל רשימת המשימות. תעדכן אותי מהאייקון שלי במחשב!")
        v = " ".join(value.split()).strip()
        m = re.match(r"^(add|new|create|done|complete|completed|finish|finished|tick|check|undone|undo|reopen"
                     r"|remove|delete|cancel|drop)\b[:\s]*(.*)$", v, re.IGNORECASE)
        if not m:
            verb, rest = "add", v
        else:
            verb, rest = m.group(1).lower(), m.group(2)
        try:
            if verb in ("add", "new", "create"):
                return self._add(rest, now)
            if verb in ("undone", "undo", "reopen"):
                return self._mark(rest, False)
            if verb in ("remove", "delete", "cancel", "drop"):
                return self._remove(rest)
            return self._mark(rest, True)
        except Exception:
            log.exception("todo tag: %s", value)
        return [], lang.t("Hmm, I got confused with your list. Can you say it again?", "אופס, התבלבלתי עם הרשימה. אפשר להגיד שוב?")

    def _add(self, rest, now):
        text, sep, when = rest.partition("|")
        if sep:
            task = parse_spoken(text, now)
            w = parse_when(when, now)
            task.update({k: w[k] for k in ("due", "someday", "at") if w[k]})
            task["star"] = task["star"] or w["star"]
        else:
            task = parse_spoken(text, now)
        if not task["text"].strip():
            return [], lang.t("What should I add to your list?", "מה להוסיף לרשימה?")
        if len([t for t in self.tasks if not t.get("done")]) >= self.max_open:
            return [], lang.t(f"Your list is full, {self.max_open} tasks! Let's finish a few first.", f"הרשימה מלאה, {self.max_open} משימות! בוא נסיים כמה קודם.")
        msg = to_message(task)
        self.tasks.append({"id": None, "text": task["text"], "due": msg.get("due"), "star": task["star"]})
        log.info("to-do add: %s", msg)
        return [msg], None

    def _pick(self, rest, pool, verb):
        found = find(rest, pool)
        if not found:
            return None, lang.t(f"I couldn't find \"{rest}\" on your list.", f"לא מצאתי את \"{rest}\" ברשימה.")
        if len(found) > 1 and len({t.get("text") for t in found}) > 1:
            names = lang.t(" or ", " או ").join(t.get("text", "") for t in found[:3])
            return None, lang.t(f"Which one: {names}?", f"איזו מהן: {names}?")
        if not found[0].get("id"):
            return None, lang.t(f"Give me a second to {verb} that, I just added it.", "רגע, הרגע הוספתי את זה. נסה שוב עוד שנייה.")
        return found[0], None

    def _mark(self, rest, done):
        pool = [t for t in self.tasks if bool(t.get("done")) != done]
        task, problem = self._pick(rest, pool, "tick" if done else "bring back")
        if problem:
            if not pool and done:
                return [], lang.t("There's nothing left on your list to tick off!", "אין יותר מה לסמן ברשימה!")
            if find(rest, [t for t in self.tasks if bool(t.get("done")) == done]):
                return [], lang.t("That one is already done!", "זה כבר בוצע!") if done else lang.t("That one isn't done yet.", "זה עוד לא בוצע.")
            return [], problem
        task["done"] = done
        log.info("to-do %s: %s", "done" if done else "not done", task.get("text"))
        return [{"type": "todo_done", "id": task["id"], "done": done}], None

    def _remove(self, rest):
        pool = sorted(self.tasks, key=lambda t: bool(t.get("done")))  # open ones first
        task, problem = self._pick(rest, pool, "remove")
        if problem:
            return [], problem
        self.tasks = [t for t in self.tasks if t is not task]
        log.info("to-do remove: %s", task.get("text"))
        return [{"type": "todo_remove", "id": task["id"]}], None


def extract(reply):
    """Takes the to-do tags out of a reply. Returns (reply without them, [("todo", value)])."""
    tags = [("todo", m.group(2).strip()) for m in TAG_RE.finditer(reply)]
    return TAG_RE.sub("", reply), tags
