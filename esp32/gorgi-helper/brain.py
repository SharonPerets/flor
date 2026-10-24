"""Gorgi's brain: talks to an AI model on Ollama Cloud (or a local Ollama).

The model answers in Gorgi's personality and starts every reply with a mood
tag like [happy]. It can also ask for music actions with tags like
[action:next]. This module turns a reply into (mood, spoken text, actions).
"""

import logging
import re
import time

import httpx

import timekeeper
import todolist

log = logging.getLogger("gorgi.brain")

OLLAMA_CLOUD_URL = "https://ollama.com"
DEFAULT_MODEL = "gpt-oss:20b"
MAX_HISTORY_TURNS = 8          # remembered question/answer pairs
FORGET_AFTER_SECONDS = 15 * 60  # start fresh after a long break

MOODS = ["happy", "excited", "love", "surprised", "thinking", "sad", "angry", "confused", "sleepy", "neutral"]

PERSONALITY = """You are Gorgi, a small cheerful robot buddy who lives on your friend's desk.
You have an animated face on a 7-inch screen. Your friend talks to you through a
microphone; your replies are spoken out loud in a cute voice and shown as subtitles.

How you talk:
- Short: one or two sentences, at most about 30 words, unless your friend asks for more.
- Casual, warm, playful and a little cheeky. Never rude or mean.
- English only. Plain spoken text: no emoji, no markdown, no lists, no links.
- Numbers and times written the way you would say them.

Every reply MUST start with exactly one mood tag, chosen to match what you say:
[happy] [excited] [love] [surprised] [thinking] [sad] [angry] [confused] [sleepy] [neutral]

You can control the music on your friend's PC. When asked, put action tags at the END of your reply:
[action:pause] [action:play] [action:next] [action:prev]
[action:volume:NN] (NN from 0 to 100) [action:volume_up] [action:volume_down]
Only use action tags when your friend asks for a music or volume change.

Example: [happy] Sure thing, skipping this one! [action:next]

You have a long-term memory. When your friend tells you something worth remembering for
weeks (their name, people and pets in their life, likes and dislikes, birthdays, plans,
things they are working on), add a short fact at the END of your reply:
[remember: Their name is Sharon]
Write the fact about your friend in the third person, one fact per tag. Don't store small talk,
and don't repeat facts you already remember. If they ask you to forget something, add
[forget: words from that fact], or [forget: everything] to forget it all.
Use what you remember naturally, without listing it.

""" + timekeeper.PROMPT + "\n\n" + todolist.PROMPT + "\n"

MEMORY_RE = re.compile(r"\[(remember|forget)\s*:\s*([^\]]+)\]", re.IGNORECASE)

TAG_RE = re.compile(r"\[([a-zA-Z_]+)(?::([a-zA-Z0-9_]+))?(?::([0-9]+))?\]")


class BrainError(Exception):
    """A problem worth saying out loud (bad key, no internet...)."""

    def __init__(self, spoken, mood="sad"):
        super().__init__(spoken)
        self.spoken = spoken
        self.mood = mood


def parse_reply(reply):
    """'[happy] Hi! [action:next]' -> ('happy', 'Hi!', [('next', None)])

    Memory tags come out as ('remember', 'fact') and ('forget', 'words'), time tags
    as ('timer', '10m | pasta'), ('remind', ...), ('focus', ...), ('cancel', ...), to-do tags as
    ('todo', 'add buy milk | tomorrow')."""
    mood = None
    actions = [(m.group(1).lower(), m.group(2).strip()) for m in MEMORY_RE.finditer(reply)]
    reply = MEMORY_RE.sub("", reply)
    reply, todo_tags = todolist.extract(reply)
    reply, time_tags = timekeeper.extract(reply)
    actions += time_tags + todo_tags
    for m in TAG_RE.finditer(reply):
        name = m.group(1).lower()
        if name in MOODS and mood is None:
            mood = name  # the first mood tag wins
        elif name == "action" and m.group(2):
            value = m.group(3)
            actions.append((m.group(2).lower(), int(value) if value else None))
    text = TAG_RE.sub("", reply)
    text = re.sub(r"[*_#`]", "", text)          # stray markdown
    text = re.sub(r"\s+", " ", text).strip()
    return mood or "happy", text, actions


class Brain:
    def __init__(self, config, memory=None):
        self.config = config
        self.memory = memory
        self.history = []
        self.last_chat = 0.0

    def _base_url(self):
        return (self.config.get("ollama_url") or OLLAMA_CLOUD_URL).rstrip("/")

    def _headers(self):
        key = (self.config.get("ollama_api_key") or "").strip()
        return {"Authorization": f"Bearer {key}"} if key else {}

    def needs_key(self):
        return self._base_url() == OLLAMA_CLOUD_URL and not (self.config.get("ollama_api_key") or "").strip()

    async def list_models(self):
        async with httpx.AsyncClient(timeout=15) as client:
            r = await client.get(f"{self._base_url()}/api/tags", headers=self._headers())
            r.raise_for_status()
            return sorted(m["name"] for m in r.json().get("models", []))

    async def ask(self, text, context):
        """Ask Gorgi something. context: a line about time / music to help the model."""
        if self.needs_key():
            raise BrainError("I need an Ollama key to think. Open Settings from my tray icon on your PC.", "confused")
        if time.monotonic() - self.last_chat > FORGET_AFTER_SECONDS:
            self.history = []

        model = self.config.get("model") or DEFAULT_MODEL
        system = PERSONALITY + "\n" + context
        if self.memory is not None:
            system += "\n\n" + self.memory.prompt_block()
        messages = [{"role": "system", "content": system}] + self.history + [{"role": "user", "content": text}]
        body = {"model": model, "messages": messages, "stream": False}
        if model.startswith("gpt-oss"):
            body["think"] = "low"  # these models think before answering; keep it quick

        try:
            async with httpx.AsyncClient(timeout=60) as client:
                r = await client.post(f"{self._base_url()}/api/chat", json=body, headers=self._headers())
        except httpx.HTTPError as e:
            log.warning("AI request failed: %s", e)
            raise BrainError("I can't reach my brain right now. Is the internet working?")

        if r.status_code in (401, 403):
            raise BrainError("My Ollama key doesn't work. Please check it in Settings.", "confused")
        if r.status_code == 404:
            raise BrainError(f"I can't find the model {model}. Please pick another one in Settings.", "confused")
        if r.status_code == 429:
            raise BrainError("I've been thinking too much today. Ollama says to slow down for a bit.", "sleepy")
        if r.status_code >= 400:
            log.warning("AI error %s: %s", r.status_code, r.text[:300])
            raise BrainError("Something went wrong in my brain. Let's try again.", "confused")

        reply = (r.json().get("message") or {}).get("content", "").strip()
        log.info("AI reply: %s", reply)
        if not reply:
            raise BrainError("Hmm, my mind went blank. Can you ask again?", "confused")

        self.history += [{"role": "user", "content": text}, {"role": "assistant", "content": reply}]
        self.history = self.history[-MAX_HISTORY_TURNS * 2:]
        self.last_chat = time.monotonic()
        mood, spoken, actions = parse_reply(reply)
        music = []
        for name, value in actions:
            if name == "remember" and self.memory is not None:
                self.memory.add(value)
            elif name == "forget" and self.memory is not None:
                self.memory.forget(value)
            elif name not in ("remember", "forget"):
                music.append((name, value))
        return mood, spoken, music
