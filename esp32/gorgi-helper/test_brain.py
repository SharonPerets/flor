"""Brain: Groq first, Gemini when Groq's limit runs out, Ollama last; retries on hiccups.
Run: python test_brain.py"""
import asyncio
import json

import httpx

import brain

calls = []  # (host, request body)


def make_brain(config):
    return brain.Brain(config)


def run(responses, config=None, b=None):
    """responses: {host: [(code, body, headers), ...]} answered in order per host."""
    calls.clear()
    seqs = {h: list(v) for h, v in responses.items()}

    def handler(request):
        host = request.url.host
        calls.append((host, json.loads(request.content) if request.content else None))
        code, body, *rest = seqs[host].pop(0)
        return httpx.Response(code, json=body, headers=rest[0] if rest else None)

    real = httpx.AsyncClient
    brain.httpx.AsyncClient = lambda **kw: real(transport=httpx.MockTransport(handler), **kw)
    orig_sleep = asyncio.sleep
    brain.asyncio.sleep = lambda s: orig_sleep(0)
    try:
        b = b or make_brain(config or {"groq_api_key": "g", "gemini_api_key": "m"})
        return asyncio.run(b.ask("hi", "ctx"))
    finally:
        brain.httpx.AsyncClient = real
        brain.asyncio.sleep = orig_sleep


def expect_error(responses, words, config=None):
    try:
        run(responses, config)
    except brain.BrainError as e:
        assert words in e.spoken, e.spoken
        return
    raise SystemExit("expected BrainError")


GROQ, GEMINI, OLLAMA = "api.groq.com", "generativelanguage.googleapis.com", "ollama.com"
ok_openai = (200, {"choices": [{"message": {"role": "assistant", "content": "[happy] Hello!"}}]})
ok_ollama = (200, {"message": {"content": "[happy] Hi there!"}})

# Groq answers: same model as before, thinking kept low
assert run({GROQ: [ok_openai]})[1] == "Hello!"
assert calls[0][1]["model"] == "openai/gpt-oss-20b" and calls[0][1]["reasoning_effort"] == "low"

# Groq's limit -> Gemini answers at once; Groq then rests, so the next question goes straight to Gemini
b = make_brain({"groq_api_key": "g", "gemini_api_key": "m"})
assert run({GROQ: [(429, {"error": "rate limit"}, {"retry-after": "120"})], GEMINI: [ok_openai]}, b=b)[1] == "Hello!"
assert [h for h, _ in calls] == [GROQ, GEMINI] and calls[1][1]["model"] == "gemini-3.5-flash-lite"
assert run({GEMINI: [ok_openai]}, b=b)[1] == "Hello!" and [h for h, _ in calls] == [GEMINI]

# Gemini's newest model missing -> its next model
assert run({GEMINI: [(404, {}), ok_openai]}, {"gemini_api_key": "m"})[1] == "Hello!"
assert calls[1][1]["model"] == "gemini-3.1-flash-lite"

# Both used up -> says so; all three -> Ollama answers last
expect_error({GROQ: [(429, {})], GEMINI: [(429, {})]}, "used up my free thinking")
assert run({GROQ: [(429, {})], GEMINI: [(429, {})], OLLAMA: [ok_ollama]},
           {"groq_api_key": "g", "gemini_api_key": "m", "ollama_api_key": "o"})[1] == "Hi there!"

# A server hiccup gets another try; a refused thinking setting is dropped
assert run({GROQ: [(502, {}), ok_openai]})[1] == "Hello!" and len(calls) == 2
assert run({GROQ: [(400, {"error": "bad param"}), ok_openai]})[1] == "Hello!"
assert "reasoning_effort" in calls[0][1] and "reasoning_effort" not in calls[1][1]

# A bad Groq key: Gemini still answers. Bad keys are named; Gemini's "API key not valid" comes as a 400
assert run({GROQ: [(401, {})], GEMINI: [ok_openai]})[1] == "Hello!"
try:
    run({GEMINI: [(400, {"error": {"message": "API key not valid."}})]}, {"gemini_api_key": "bad"})
    raise SystemExit("expected BrainError")
except brain.BrainError as e:
    assert "Gemini key" in e.spoken and len(calls) == 1, (e.spoken, calls)
expect_error({GROQ: [(500, {})] * 3, GEMINI: [(500, {})] * 3}, "went wrong")

# Old setups with only an Ollama key keep working; no key at all asks for one
assert run({OLLAMA: [ok_ollama]}, {"ollama_api_key": "o", "model": "gpt-oss:20b"})[1] == "Hi there!"
assert calls[0][1]["think"] == "low"
expect_error({}, "Groq or Gemini key", {"model": "gpt-oss:20b"})
print("brain: ok")
