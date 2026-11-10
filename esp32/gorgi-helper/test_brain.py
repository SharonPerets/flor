"""Brain retries: a server hiccup or a refused "think" gets another try. Run: python test_brain.py"""
import asyncio

import httpx

import brain

calls = []


def run(responses):
    calls.clear()
    seq = list(responses)

    def handler(request):
        import json
        calls.append(json.loads(request.content))
        code, body = seq.pop(0)
        return httpx.Response(code, json=body)

    real = httpx.AsyncClient
    brain.httpx.AsyncClient = lambda **kw: real(transport=httpx.MockTransport(handler), **kw)
    orig_sleep = asyncio.sleep
    brain.asyncio.sleep = lambda s: orig_sleep(0)
    try:
        b = brain.Brain({"ollama_api_key": "k", "model": "gpt-oss:20b"})
        return asyncio.run(b.ask("hi", "ctx"))
    finally:
        brain.httpx.AsyncClient = real
        brain.asyncio.sleep = orig_sleep


ok = (200, {"message": {"content": "[happy] Hello!"}})
assert run([(502, {"error": "bad gateway"}), ok])[1] == "Hello!"
assert len(calls) == 2
assert run([(400, {"error": "think not supported"}), ok])[1] == "Hello!"
assert "think" in calls[0] and "think" not in calls[1]
try:
    run([(500, {}), (500, {}), (500, {})])
    raise SystemExit("expected BrainError")
except brain.BrainError as e:
    assert "went wrong" in e.spoken and len(calls) == 3
try:
    run([(401, {})])
    raise SystemExit("expected BrainError")
except brain.BrainError as e:
    assert "key" in e.spoken and len(calls) == 1
print("brain retries: ok")
