"""Stage 4: local keyless LLM client — scripted stand-ins, no network."""
from __future__ import annotations

import pytest

from src.core.errors import CapabilityUnavailableError, EngineExecutionError
from src.llm.local_client import LocalClient


def test_no_endpoint_refuses_truthfully(monkeypatch):
    monkeypatch.delenv("TRINITY_MODEL_ENDPOINT", raising=False)
    client = LocalClient()
    assert client.available() is False
    with pytest.raises(CapabilityUnavailableError) as exc:
        client.generate("hello")
    assert "TRINITY_MODEL_ENDPOINT" in str(exc.value.details["env_var"])
    with pytest.raises(CapabilityUnavailableError):
        next(client.stream("hello"), None)


def test_generate_over_openai_compatible_body(monkeypatch):
    monkeypatch.setenv("TRINITY_MODEL_ENDPOINT", "http://localhost:9999/v1/chat/completions")
    monkeypatch.setenv("TRINITY_MODEL_NAME", "my-model-7b")

    captured = {}

    class FakeResponse:
        status_code = 200
        error = None

        def raise_for_status(self):
            pass

        def json(self):
            return {
                "id": "chatcmpl-x",
                "model": "my-model-7b",
                "choices": [{"message": {"content": "ok reply"}}],
            }

    def fake_post(endpoint, json=None, headers=None, timeout=None):
        captured["endpoint"] = endpoint
        captured["body"] = json
        return FakeResponse()

    import httpx

    monkeypatch.setattr(httpx, "post", fake_post)
    client = LocalClient()
    assert client.available()
    assert client.generate("plan this") == "ok reply"
    assert captured["endpoint"].endswith("/v1/chat/completions")
    assert captured["body"]["model"] == "my-model-7b"
    assert captured["body"]["stream"] is False
    assert captured["body"]["messages"][0]["content"] == "plan this"


def test_stream_yields_deltas_and_stops_at_done(monkeypatch):
    monkeypatch.setenv("TRINITY_MODEL_ENDPOINT", "http://localhost:9999/v1/chat/completions")
    events = [
        'data: {"choices":[{"delta":{"content":"Hel"}}]}',
        'data: {"choices":[{"delta":{"content":"lo"}}]}',
        'data: {"choices":[{"delta":{"content":null}}]}',
        "data: [DONE]",
    ]
    seen = {}

    class FakeStream:
        status_code = 200

        def raise_for_status(self):
            pass

        def iter_lines(self):
            yield from events

    class FakeCtx:
        def __enter__(self):
            return FakeStream()

        def __exit__(self, *args):
            return False

    def fake_stream(method, endpoint, json=None, headers=None, timeout=None):
        seen["body"] = json
        return FakeCtx()

    import httpx

    monkeypatch.setattr(httpx, "stream", fake_stream)
    client = LocalClient()
    assert list(client.stream("hi")) == ["Hel", "lo"]
    assert seen["body"]["stream"] is True


def test_stream_error_event_surfaces_truthfully(monkeypatch):
    monkeypatch.setenv("TRINITY_MODEL_ENDPOINT", "http://localhost:9999/v1/chat/completions")

    class FakeStream:
        status_code = 200

        def raise_for_status(self):
            pass

        def iter_lines(self):
            yield 'data: {"error":{"message":"overloaded"}}'

    class FakeCtx:
        def __enter__(self):
            return FakeStream()

        def __exit__(self, *args):
            return False

    import httpx

    monkeypatch.setattr(httpx, "stream", lambda *a, **k: FakeCtx())
    client = LocalClient()
    with pytest.raises(EngineExecutionError) as exc:
        list(client.stream("hi"))
    assert "overloaded" in str(exc.value)


def test_tool_call_stays_deterministic():
    client = LocalClient()
    # No endpoint required — reshaping a structured request is pure.
    assert client.tool_call({"engine": "cad", "operation": "generate"}) == {
        "engine": "cad",
        "operation": "generate",
        "parameters": {},
    }
