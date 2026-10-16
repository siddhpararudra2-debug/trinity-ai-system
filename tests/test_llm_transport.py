"""LLM transports (P2): mocked-SDK units always run; live calls gated on keys."""

import os

import pytest

from src.core.errors import CapabilityUnavailableError, EngineExecutionError
from src.llm.claude_client import ClaudeClient
from src.llm.openai_client import OpenAIClient


# ------------------------------------------------------------- fakes ---


class _FakeOAIChoice:
    def __init__(self, text):
        self.message = type("M", (), {"content": text})()
        self.delta = type("D", (), {"content": text})()


class _FakeOAIResponse:
    def __init__(self, text="hello"):
        self.choices = [_FakeOAIChoice(text)]


class _FakeOAICompletions:
    def __init__(self, text="hello", fail=None):
        self.text = text
        self.fail = fail
        self.calls = []

    def create(self, **kwargs):
        self.calls.append(kwargs)
        if self.fail is not None:
            raise self.fail
        if kwargs.get("stream"):
            return [_FakeOAIResponse(self.text), _FakeOAIResponse(" world")]
        return _FakeOAIResponse(self.text)


class _FakeOpenAI:
    instance = None

    def __init__(self, api_key=None):
        self.api_key = api_key
        self.chat = type("C", (), {})()
        self.chat.completions = _FakeOAICompletions()
        _FakeOpenAI.instance = self


class _FakeBlock:
    def __init__(self, text):
        self.type = "text"
        self.text = text


class _FakeAnthropicResponse:
    def __init__(self, text="hello"):
        self.content = [_FakeBlock(text)]


class _FakeEvent:
    def __init__(self, text):
        self.delta = type("D", (), {"text": text})()


class _FakeAnthropicMessages:
    def __init__(self, fail=None):
        self.fail = fail
        self.calls = []

    def create(self, **kwargs):
        self.calls.append(kwargs)
        if self.fail is not None:
            raise self.fail
        if kwargs.get("stream"):
            return [_FakeEvent("hello"), _FakeEvent(" world")]
        return _FakeAnthropicResponse()


class _FakeAnthropic:
    instance = None

    def __init__(self, api_key=None):
        self.api_key = api_key
        self.messages = _FakeAnthropicMessages()
        _FakeAnthropic.instance = self


# ------------------------------------------------------------------ - ---


def test_openai_refuses_without_key(monkeypatch):
    monkeypatch.delenv("OPENAI_API_KEY", raising=False)
    with pytest.raises(CapabilityUnavailableError, match="OPENAI_API_KEY"):
        OpenAIClient().generate("hi")
    with pytest.raises(CapabilityUnavailableError):
        list(OpenAIClient().stream("hi"))


def test_anthropic_refuses_without_key(monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    with pytest.raises(CapabilityUnavailableError, match="ANTHROPIC_API_KEY"):
        ClaudeClient().generate("hi")
    with pytest.raises(CapabilityUnavailableError):
        list(ClaudeClient().stream("hi"))


def test_openai_generate_uses_sdk(monkeypatch):
    monkeypatch.setenv("OPENAI_API_KEY", "test-key")
    monkeypatch.delenv("OPENAI_MODEL", raising=False)
    monkeypatch.setattr("openai.OpenAI", _FakeOpenAI)
    client = OpenAIClient(model="gpt-test", temperature=0.1)
    assert client.generate("hi") == "hello"
    call = _FakeOpenAI.instance.chat.completions.calls[0]
    assert call["model"] == "gpt-test"
    assert call["temperature"] == 0.1
    assert _FakeOpenAI.instance.api_key == "test-key"


def test_openai_generate_wraps_transport_errors(monkeypatch):
    monkeypatch.setenv("OPENAI_API_KEY", "test-key")

    class Boom:
        def __init__(self, api_key=None):
            self.chat = type("C", (), {})()

            class _B:
                def create(self, **kwargs):
                    raise RuntimeError("down")

            self.chat.completions = _B()

    monkeypatch.setattr("openai.OpenAI", Boom)
    with pytest.raises(EngineExecutionError, match="OpenAI request failed"):
        OpenAIClient().generate("hi")


def test_openai_stream_yields_chunks(monkeypatch):
    monkeypatch.setenv("OPENAI_API_KEY", "test-key")
    monkeypatch.setattr("openai.OpenAI", _FakeOpenAI)
    assert "".join(OpenAIClient().stream("hi")) == "hello world"


def test_anthropic_generate_uses_sdk(monkeypatch):
    monkeypatch.setenv("ANTHROPIC_API_KEY", "test-key")
    monkeypatch.delenv("ANTHROPIC_MODEL", raising=False)
    monkeypatch.setattr("anthropic.Anthropic", _FakeAnthropic)
    client = ClaudeClient(model="claude-test")
    assert client.generate("hi") == "hello"
    call = _FakeAnthropic.instance.messages.calls[0]
    assert call["model"] == "claude-test"
    assert "max_tokens" in call


def test_anthropic_generate_wraps_transport_errors(monkeypatch):
    monkeypatch.setenv("ANTHROPIC_API_KEY", "test-key")

    class Boom:
        def __init__(self, api_key=None):
            class _B:
                def create(self, **kwargs):
                    raise RuntimeError("down")

            self.messages = _B()

    monkeypatch.setattr("anthropic.Anthropic", Boom)
    with pytest.raises(EngineExecutionError, match="Anthropic request failed"):
        ClaudeClient().generate("hi")


def test_anthropic_stream_yields_chunks(monkeypatch):
    monkeypatch.setenv("ANTHROPIC_API_KEY", "test-key")
    monkeypatch.setattr("anthropic.Anthropic", _FakeAnthropic)
    assert "".join(ClaudeClient().stream("hi")) == "hello world"


def test_tool_call_stays_deterministic_without_keys(monkeypatch):
    monkeypatch.delenv("OPENAI_API_KEY", raising=False)
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    req = {"engine": "math", "operation": "solve", "parameters": {"expression": "1+1"}}
    assert OpenAIClient().tool_call(req) == req
    assert ClaudeClient().tool_call(req) == req


# ---------------------------------------------------------------- live ---

live_openai = pytest.mark.skipif(
    not os.getenv("OPENAI_API_KEY"), reason="needs OPENAI_API_KEY"
)
live_anthropic = pytest.mark.skipif(
    not os.getenv("ANTHROPIC_API_KEY"), reason="needs ANTHROPIC_API_KEY"
)


@live_openai
def test_live_openai_generates():
    assert OpenAIClient().generate("Reply with exactly: ok").strip() != ""


@live_anthropic
def test_live_anthropic_generates():
    assert ClaudeClient().generate("Reply with exactly: ok").strip() != ""
