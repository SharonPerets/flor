"""Gorgi's long-term memory: short facts about you, kept in memory.json on your PC.

The AI adds a fact by ending a reply with [remember: ...] and removes facts
with [forget: ...]. The facts are given to the AI at the start of every chat.
You can also read or edit memory.json yourself (tray menu -> Memories).
"""

import json
import logging
import re
import time
from pathlib import Path

log = logging.getLogger("gorgi.memory")

MAX_FACTS = 60
MAX_FACT_LENGTH = 160
STOP_WORDS = {"the", "and", "that", "this", "about", "with", "for", "you", "your", "my", "me", "i", "a", "an",
              "is", "are", "was", "of", "to", "in", "on", "it", "what", "please", "forget", "remember"}


def _words(text):
    return [w for w in re.findall(r"[a-z0-9']+", text.lower()) if w not in STOP_WORDS and len(w) > 1]


class Memory:
    def __init__(self, path):
        self.path = Path(path)
        self.facts = []  # [{"text": ..., "date": "2026-10-22"}]
        self.load()

    def load(self):
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
            self.facts = [f for f in data.get("facts", []) if isinstance(f, dict) and f.get("text")]
        except FileNotFoundError:
            self.facts = []
        except Exception as e:
            log.warning("could not read %s: %s", self.path, e)

    def save(self):
        self.path.write_text(json.dumps({"facts": self.facts}, indent=2, ensure_ascii=False), encoding="utf-8")

    def add(self, text):
        text = " ".join(text.split()).strip(" .")[:MAX_FACT_LENGTH]
        if len(text) < 3:
            return False
        low = text.lower()
        for f in self.facts:
            old = f["text"].lower()
            if old == low or low in old:
                return False  # already known
            if old in low:  # the new fact says more: replace
                f["text"], f["date"] = text, time.strftime("%Y-%m-%d")
                self.save()
                return True
        self.facts.append({"text": text, "date": time.strftime("%Y-%m-%d")})
        self.facts = self.facts[-MAX_FACTS:]
        self.save()
        log.info("remembered: %s", text)
        return True

    def forget(self, what):
        """Removes facts matching the words in `what`. 'everything' clears all. Returns how many."""
        low = what.lower().strip()
        if low in ("everything", "all", "all of it", "everything about me"):
            n = len(self.facts)
            self.facts = []
        else:
            words = _words(what)
            if not words:
                return 0
            keep = [f for f in self.facts if not all(w in f["text"].lower() for w in words)]
            n = len(self.facts) - len(keep)
            self.facts = keep
        if n:
            self.save()
            log.info("forgot %d fact(s) about: %s", n, what)
        return n

    def prompt_block(self):
        if not self.facts:
            return "You don't know anything about your friend yet."
        lines = "\n".join(f"- {f['text']} (noted {f['date']})" for f in self.facts)
        return "Things you remember about your friend:\n" + lines
