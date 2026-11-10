"""Gorgi's brain: asks a free AI service (Groq first, then Gemini as backup, then Ollama if set up).

The model answers in Gorgi's personality and starts every reply with a mood
tag like [happy]. It can also ask for music actions with tags like
[action:next]. This module turns a reply into (mood, spoken text, actions).
"""

import asyncio
import logging
import re
import time

import httpx

import timekeeper
import todolist
import ytmusic

log = logging.getLogger("gorgi.brain")

GROQ_URL = "https://api.groq.com/openai/v1"
GROQ_MODEL = "openai/gpt-oss-20b"
GEMINI_URL = "https://generativelanguage.googleapis.com/v1beta/openai"
GEMINI_MODELS = ["gemini-3.5-flash-lite", "gemini-3.1-flash-lite"]  # free: ~500 a day each
OLLAMA_CLOUD_URL = "https://ollama.com"
DEFAULT_MODEL = "gpt-oss:20b"  # Ollama's model
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

""" + ytmusic.PROMPT + "\n\n" + timekeeper.PROMPT + "\n\n" + todolist.PROMPT + "\n"

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
    ('todo', 'add buy milk | tomorrow'), YouTube Music tags as ('music', 'play Muse')."""
    mood = None
    actions = [(m.group(1).lower(), m.group(2).strip()) for m in MEMORY_RE.finditer(reply)]
    reply = MEMORY_RE.sub("", reply)
    reply, todo_tags = todolist.extract(reply)
    reply, time_tags = timekeeper.extract(reply)
    reply, music_tags = ytmusic.extract(reply)
    actions += time_tags + todo_tags + music_tags
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


class Service:
    """One place Gorgi can think: Groq, Gemini (both OpenAI-style) or Ollama."""

    def __init__(self, name, url, key, models, style="openai", effort="low"):
        self.name = name
        self.url = url
        self.key = key
        self.models = models          # first one that exists wins (later ones if a model is gone)
        self.style = style            # "openai" or "ollama"
        self.effort = effort          # thinking effort, or None
        self.rest_until = 0.0         # skip this service until then (its limit ran out)

    def headers(self):
        return {"Authorization": f"Bearer {self.key}"} if self.key else {}

    def chat_url(self):
        return self.url + ("/api/chat" if self.style == "ollama" else "/chat/completions")

    def models_url(self):
        return self.url + ("/api/tags" if self.style == "ollama" else "/models")

    def body(self, model, messages):
        body = {"model": model, "messages": messages, "stream": False}
        if self.style == "ollama":
            if model.startswith("gpt-oss"):
                body["think"] = "low"  # these models think before answering; keep it quick
        elif self.effort:
            body["reasoning_effort"] = self.effort
        return body

    def reply_text(self, data):
        if self.style == "ollama":
            return (data.get("message") or {}).get("content") or ""
        choices = data.get("choices") or [{}]
        return (choices[0].get("message") or {}).get("content") or ""


class _Failed(Exception):
    """A service couldn't answer. kind: limit / key / model / down / other."""

    def __init__(self, kind, detail=""):
        super().__init__(f"{kind}: {detail}")
        self.kind = kind


def _retry_after(r, default):
    try:
        return max(5.0, min(float(r.headers.get("retry-after", "")), 24 * 3600.0))
    except ValueError:
        return default


class Brain:
    def __init__(self, config, memory=None):
        self.config = config
        self.memory = memory
        self.history = []
        self.last_chat = 0.0
        self._services = {}  # name -> Service, kept so a "limit reached" pause is remembered

    def services(self):
        """The services that have a key, in the order Gorgi asks them: Groq, Gemini, Ollama."""
        c = self.config
        wanted = []
        groq = (c.get("groq_api_key") or "").strip()
        if groq:
            wanted.append(("Groq", GROQ_URL, groq, [c.get("groq_model") or GROQ_MODEL], "openai", "low"))
        gemini = (c.get("gemini_api_key") or "").strip()
        if gemini:
            models = [c["gemini_model"]] if c.get("gemini_model") else list(GEMINI_MODELS)
            wanted.append(("Gemini", GEMINI_URL, gemini, models, "openai", "low"))
        ollama = (c.get("ollama_api_key") or "").strip()
        ollama_url = (c.get("ollama_url") or "").strip()
        if ollama or ollama_url:
            wanted.append(("Ollama", (ollama_url or OLLAMA_CLOUD_URL).rstrip("/"), ollama,
                           [c.get("model") or DEFAULT_MODEL], "ollama", None))
        result = []
        for name, url, key, models, style, effort in wanted:
            s = self._services.get(name)
            if s is None or (s.url, s.key, s.models[:1]) != (url, key, models[:1]):
                s = self._services[name] = Service(name, url, key, models, style, effort)
            result.append(s)
        return result

    def needs_key(self):
        return not self.services()

    async def check(self, service):
        """For the Settings window: (ok, short text). Lists the service's models (uses no quota)."""
        try:
            async with httpx.AsyncClient(timeout=15) as client:
                r = await client.get(service.models_url(), headers=service.headers())
        except httpx.HTTPError as e:
            return False, f"can't reach it ({type(e).__name__})"
        if r.status_code in (400, 401, 403):
            return False, "the key doesn't work"
        if r.status_code >= 400:
            return False, f"error {r.status_code}"
        data = r.json()
        names = [m.get("name") or m.get("id") or "" for m in (data.get("models") or data.get("data") or [])]
        names = [n.split("/", 1)[1] if n.startswith("models/") else n for n in names]
        found = [m for m in service.models if m in names]
        if names and not found:
            return False, f"key works, but model {service.models[0]} is missing"
        return True, "works" + (f" ({found[0]})" if found else "")

    async def _post_chat(self, service, body):
        """Sends the chat request. A server hiccup (5xx, a dropped connection) or a 400 gets a second
        try after a short pause; a 400 for a thinking model is tried again without the thinking setting."""
        r = None
        for attempt in range(3):
            try:
                async with httpx.AsyncClient(timeout=60) as client:
                    r = await client.post(service.chat_url(), json=body, headers=service.headers())
            except httpx.HTTPError as e:
                log.warning("AI request to %s failed (try %d): %r", service.name, attempt + 1, e)
                if attempt == 2:
                    raise _Failed("down", repr(e))
                await asyncio.sleep(1.5)
                continue
            if r.status_code < 400 or r.status_code in (401, 402, 403, 404, 429):
                return r
            log.warning("AI error %s from %s (try %d, model %s): %s", r.status_code, service.name, attempt + 1,
                        body.get("model"), r.text[:300])
            if r.status_code == 400 and "api key" in r.text.lower():
                return r  # Gemini says "API key not valid" with a 400
            if attempt == 2:
                break
            if r.status_code == 400:
                body = dict(body)
                body.pop("think", None)  # some servers refuse it
                body.pop("reasoning_effort", None)
            await asyncio.sleep(1.5)
        return r

    async def _ask_service(self, service, messages):
        """One service's reply text, or raises _Failed."""
        for model in service.models:
            r = await self._post_chat(service, service.body(model, messages))
            code = r.status_code
            if code == 429 or code == 402:
                # 429 = slow down / today's limit reached; 402 = Ollama's usage ran out
                pause = _retry_after(r, 10 * 60.0 if code == 429 else 60 * 60.0)
                service.rest_until = time.monotonic() + pause
                log.warning("AI limit at %s (%s), resting it %.0f s: %s", service.name, code, pause, r.text[:300])
                raise _Failed("limit")
            if code in (401, 403) or (code == 400 and "api key" in r.text.lower()):
                log.warning("AI key refused by %s: %s", service.name, r.text[:200])
                raise _Failed("key")
            if code == 404:
                log.warning("AI model %s not found at %s", model, service.name)
                continue  # try the next model
            if code >= 400:
                raise _Failed("other", f"{code}")
            try:
                reply = service.reply_text(r.json()).strip()
            except ValueError:
                raise _Failed("other", "not JSON")
            if not reply:
                raise _Failed("blank")
            log.info("AI reply (%s, %s): %s", service.name, model, reply)
            return reply
        raise _Failed("model")

    async def _ask_any(self, messages):
        """Asks Groq, then Gemini, then Ollama (each only if it has a key and isn't resting)."""
        services = self.services()
        ready = [s for s in services if time.monotonic() >= s.rest_until]
        if not ready:  # all resting: try the one that rests the shortest anyway
            ready = [min(services, key=lambda s: s.rest_until)]
        problems = []
        for service in ready:
            try:
                return await self._ask_service(service, messages)
            except _Failed as e:
                problems.append((service.name, e.kind))
        log.warning("no AI could answer: %s", problems)
        kinds = {k for _, k in problems}
        bad_keys = [n for n, k in problems if k == "key"]
        if kinds == {"limit"}:
            raise BrainError("I've used up my free thinking for now. Let's try again in a little while.", "sleepy")
        if bad_keys:
            raise BrainError(f"My {bad_keys[0]} key doesn't work. Please check it in Settings.", "confused")
        if kinds == {"down"}:
            raise BrainError("I can't reach my brain right now. Is the internet working?")
        if "blank" in kinds:
            raise BrainError("Hmm, my mind went blank. Can you ask again?", "confused")
        if "model" in kinds:
            raise BrainError("I can't find my thinking model. Please check Settings.", "confused")
        raise BrainError("Something went wrong in my brain. Let's try again.", "confused")

    def note(self, text, reply):
        """A question answered without the AI (see quick.py): kept in the chat so a follow-up
        like "make it 15" still makes sense to the AI."""
        if time.monotonic() - self.last_chat > FORGET_AFTER_SECONDS:
            self.history = []
        self.history += [{"role": "user", "content": text}, {"role": "assistant", "content": reply}]
        self.history = self.history[-MAX_HISTORY_TURNS * 2:]
        self.last_chat = time.monotonic()

    async def ask(self, text, context):
        """Ask Gorgi something. context: a line about time / music to help the model."""
        if self.needs_key():
            raise BrainError("I need a Groq or Gemini key to think. Open Settings from my tray icon on your PC.",
                             "confused")
        if time.monotonic() - self.last_chat > FORGET_AFTER_SECONDS:
            self.history = []

        system = PERSONALITY + "\n" + context
        if self.memory is not None:
            system += "\n\n" + self.memory.prompt_block()
        messages = [{"role": "system", "content": system}] + self.history + [{"role": "user", "content": text}]

        reply = await self._ask_any(messages)

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
