"""The Hebrew test in Settings: asks a few AI models the same Hebrew requests and
writes a report, so you can pick the model that speaks the best Hebrew.

Each model gets 8 requests (chat, a timer, a reminder, to-dos, music) with a made-up
screen (a pasta timer running, a short to-do list). The report shows every answer,
whether the right tags came out, and how long each answer took.
"""

import asyncio
import time

import lang
from brain import Brain, BrainError
from timekeeper import TimeKeeper
from todolist import TodoKeeper

# Models worth trying, in order (the first match of each that the account has)
CANDIDATES = ["gpt-oss:20b", "gpt-oss:120b", "gemma4", "deepseek-v4", "mistral-large", "qwen3"]

# (what you say, what should come out: tag names, or None for just talk)
REQUESTS = [
    ("היי גורגי, מה שלומך היום?", None),
    ("תשים טיימר של עשר דקות לפסטה", ["timer"]),
    ("תזכיר לי מחר בשמונה בבוקר להתקשר לרופא", ["remind"]),
    ("תוסיף לרשימה לקנות חלב וביצים", ["todo"]),
    ("סיימתי לשלם את חשבון החשמל", ["todo"]),
    ("תעביר לשיר הבא ותגביר קצת", ["next", "volume_up|volume"]),
    ("כמה זמן נשאר לטיימר?", None),
    ("ספר לי בדיחה קצרה", None),
]


def pick_models(available, current):
    """Up to 5 models to test: the one in use, then the candidates the account has."""
    chosen = [current] if current else []
    for prefix in CANDIDATES:
        match = next((m for m in available if m.startswith(prefix) and m not in chosen), None)
        if match:
            chosen.append(match)
    return chosen[:5]


def _context():
    tk = TimeKeeper()
    tk.update({"timers": [{"id": 1, "kind": "timer", "label": "פסטה", "left": 420, "paused": False}],
               "reminders": []})
    todos = TodoKeeper()
    todos.update({"today": time.strftime("%Y-%m-%d"), "tasks": [
        {"id": 1, "text": "לשלם חשבון חשמל"}, {"id": 2, "text": "להתקשר לבנק על הכרטיס", "star": True}]})
    return "\n".join([f"Right now it is {time.strftime('%A %d %B %Y, %H:%M')}.",
                      "Music on the PC: \"תגידי לי\" by עומר אדם, playing (paused while you talk).",
                      "PC volume: 40%.", tk.prompt_block(), todos.prompt_block()])


def _tags_ok(actions, wanted):
    names = [name for name, _ in actions]
    if wanted is None:
        return not names
    return all(any(n in w.split("|") for n in names) for w in wanted)


async def test_model(config, model, progress=None):
    """Returns (lines for the report, tags right, answers in Hebrew, seconds per answer)."""
    lines = [f"=== {model} ==="]
    right = hebrew = 0
    seconds = []
    for i, (said, wanted) in enumerate(REQUESTS, 1):
        if progress:
            progress(f"{model}: {i}/{len(REQUESTS)}")
        brain = Brain({**config, "model": model})  # a fresh brain each time: no memory, no history
        started = time.monotonic()
        try:
            mood, spoken, actions = await brain.ask(said, _context())
        except BrainError as e:
            lines.append(f"{i}. {said}\n   PROBLEM: {e.spoken}")
            continue
        took = time.monotonic() - started
        seconds.append(took)
        ok = _tags_ok(actions, wanted)
        right += ok
        hebrew += lang.has_hebrew(spoken)
        tags = ", ".join(f"{n}:{v}" if v is not None else n for n, v in actions) or "-"
        lines.append(f"{i}. {said}\n   [{mood}] {spoken}\n   tags: {tags}  {'OK' if ok else 'WRONG'}  ({took:.1f} s)")
    avg = sum(seconds) / len(seconds) if seconds else 0.0
    lines.append(f"-> {right}/{len(REQUESTS)} actions right, {hebrew}/{len(REQUESTS)} answers in Hebrew, "
                 f"{avg:.1f} s per answer\n")
    return lines, right, hebrew, avg


async def run(config, models, progress=None):
    """Tests the models in Hebrew. Returns (report text, one-line summary)."""
    old = lang._config
    lang.bind({**config, "language": "he"})
    try:
        report = ["Gorgi - Hebrew test of AI models", time.strftime("%Y-%m-%d %H:%M"), ""]
        results = []
        for model in models:
            lines, right, hebrew, avg = await test_model(config, model, progress)
            report += lines
            results.append((right + hebrew, -avg, model, right, hebrew, avg))
    finally:
        lang.bind(old)
    if not results:
        return "\n".join(report), "No models to test."
    results.sort(reverse=True)
    _, _, best, right, hebrew, avg = results[0]
    summary = (f"Best: {best} ({right}/{len(REQUESTS)} actions right, {hebrew}/{len(REQUESTS)} in Hebrew, "
               f"{avg:.1f} s per answer)")
    report.insert(2, summary)
    return "\n".join(report), summary


if __name__ == "__main__":  # python model_test.py KEY model1 model2 ...
    import sys

    text, line = asyncio.run(run({"ollama_api_key": sys.argv[1]}, sys.argv[2:], print))
    print(text)
