"""LLM adapters (Item 8: LLM link-up).

Provides a single adapter that marks every LLM call with "LLM-LINK:"
behind one adapter. Supports OpenAI-compatible endpoints with canned JSON
responses for development and testing (mock server mode).

The adapter wraps any OpenAI-compatible client and ensures:
- Every API call is routed through this adapter
- Responses are marked/can be identified as LLM-LINK output
- Mock mode can be enabled via LLM_MOCK=1 env var for development
- Zero code changes needed when swapping real LLM → mock later
"""

from __future__ import annotations

import json
import os
from typing import Any, Dict, List, Optional, Tuple


def llm_link_mark(text: str) -> str:
    """Mark LLM output with the LLM-LINK identifier.

    Every LLM-generated string that passes through this adapter
    should be prefixed/identified with this marker so the system
    can distinguish LLM output from other sources.

    Args:
        text: The raw LLM output text.

    Returns:
        The text prefixed with the LLM-LINK marker.
    """
    marker = "LLM-LINK:"
    if text.startswith(marker):
        return text
    return f"{marker} {text}"


def unlink_mark(text: str) -> str:
    """Remove the LLM-LINK marker from text.

    Args:
        text: Text possibly prefixed with "LLM-LINK:".

    Returns:
        The text without the marker.
    """
    marker = "LLM-LINK:"
    if text.startswith(marker):
        return text[len(marker):].strip()
    return text


class LLMAdapter:
    """Single adapter that routes all LLM calls and marks them with LLM-LINK.

    This adapter can wrap any OpenAI-compatible client (OpenAIClient,
    LocalClient, etc.) and ensure every call is marked. In mock mode
    (LLM_MOCK=1), it returns canned JSON responses instead of making
    real network calls.

    Attributes:
        client: The underlying LLM client.
        mock: If True, return canned responses without network calls.
        model_name: The model name to use in responses.
    """

    def __init__(
        self,
        client: Any = None,
        mock: bool | None = None,
        model_name: str | None = None,
    ):
        self.mock = bool(os.getenv("LLM_MOCK", str(mock).lower() == "true" if mock is not None else False))
        self.model_name = model_name or os.getenv("LLM_MODEL_NAME", "local-model")
        self._client = client or self._build_client()

    def _build_client(self) -> Any:
        """Build the underlying client from environment config."""
        from src.llm.local_client import LocalClient

        endpoint = os.getenv("TRINITY_MODEL_ENDPOINT", "")
        return LocalClient(
            endpoint=endpoint or None,
            model=os.getenv("TRINITY_MODEL_NAME", self.model_name) or None,
        )

    # ------------------------------------------------------------------
    # Chat completions shape (OpenAI-compatible)
    # ------------------------------------------------------------------

    def chat_completion(self, messages: List[dict[str, str]], **kwargs: Any) -> dict[str, Any]:
        """Send a chat completion request through the adapter.

        Args:
            messages: List of message dicts with 'role' and 'content'.
            **kwargs: Additional parameters (temperature, top_p, etc.).

        Returns:
            A dict with at least ``content`` and ``model`` keys,
            marked with LLM-LINK identifier.
        """
        if self.mock:
            return self._mock_response(messages)

        # Real client call
        raw = self._client.generate(messages=messages, **kwargs)  # type: ignore[arg-type]
        return {"content": llm_link_mark(raw), "model": self.model_name}

    def chat_completion_stream(self, messages: List[dict[str, str]], **kwargs: Any) -> Any:
        """Send a streaming chat completion request.

        Args:
            messages: List of message dicts.
            **kwargs: Additional parameters.

        Yields:
            Incremental text chunks marked with LLM-LINK.
        """
        if self.mock:
            yield from self._mock_stream(messages)
            return

        # Real client call
        for chunk in self._client.stream(messages=messages, **kwargs):  # type: ignore[arg-type]
            yield llm_link_mark(chunk)

    # ------------------------------------------------------------------
    # Canned/mock responses for development
    # ------------------------------------------------------------------

    def _mock_response(self, messages: List[dict[str, str]]) -> dict[str, Any]:
        """Return a canned JSON response for development/testing.

        The response shape matches OpenAI chat-completions output.
        """
        # Determine the intent from the user message
        user_msg = ""
        for m in messages:
            if m.get("role") == "user":
                user_msg = m.get("content", "")
                break

        # Simple keyword-based canned responses
        response_text = self._canned_response(user_msg)
        return {"content": llm_link_mark(response_text), "model": self.model_name}

    def _canned_response(self, user_msg: str) -> str:
        """Generate a canned response based on user message content."""
        msg_lower = user_msg.lower()

        if "plan" in msg_lower or "design" in msg_lower:
            return "I can help you design a Trinity project. I'll generate a CAD model, create a PCB netlist, and produce firmware source code. The design will include verification reports and downloadable files in STL, Gerber, and CSV formats."

        if "cad" in msg_lower:
            return "I can assist with CAD operations. I'm ready to generate enclosure designs, lid parts, or any custom CAD component you need. What type of part would you like me to create?"

        if "pcb" in msg_lower or "board" in msg_lower:
            return "I can help with PCB design. I'll generate a board outline, place components from your netlist, and export Gerber files for fabrication. What are your board dimensions and component requirements?"

        if "firmware" in msg_lower or "code" in msg_lower:
            return "I can generate firmware source code. For the ESP32-WROOM-32, I'll create C source with UART, SPI, and I2C peripheral initialization. Would you like me to generate a firmware project for your board?"

        if "verify" in msg_lower or "check" in msg_lower:
            return "I can help verify your design. I'll check components against datasheets, validate netlists, and report any mismatches between CAD, PCB, and firmware domains. What design would you like me to assess?"

        if "export" in msg_lower or "download" in msg_lower:
            return "I can help export your design in multiple formats. I'll generate STL/STEP files for CAD, Gerber files for PCB fabrication, BOM CSV for purchasing, and firmware source code. Which format do you need first?"

        if "bom" in msg_lower or "bill" in msg_lower or "parts" in msg_lower:
            return "I can help generate a Bill of Materials. I'll list all components with their reference designators, values, types, and net connections. The BOM will be available as a CSV file for import into your purchasing system."

        if "question" in msg_lower or "info" in msg_lower:
            return "Trinity AI is a full-stack electronic design automation platform supporting CAD schematic capture, PCB netlist generation, firmware source creation, and verification reporting. All outputs include verification status and downloadable artifact files."

        # Default helpful response
        return "I'm Trinity AI, your electronic design assistant. I can help you with CAD design, PCB layout, firmware generation, verification reporting, and export in standard formats. What would you like to work on today?"

    def _mock_stream(self, messages: List[dict[str, str]]) -> Any:
        """Yield canned streaming response chunks."""
        user_msg = ""
        for m in messages:
            if m.get("role") == "user":
                user_msg = m.get("content", "")
                break

        response_text = self._canned_response(user_msg)
        # Simple streaming: split by sentences
        sentences = [s.strip() for s in response_text.split(".") if s.strip()]
        for i, sentence in enumerate(sentences):
            yield llm_link_mark(sentence + ".")
            if i < len(sentences) - 1:
                # Small delay simulation
                import time
                time.sleep(0.05)


# Global adapter instance for easy import
_adapter: Optional[LLMAdapter] = None


def get_adapter() -> LLMAdapter:
    """Get the global LLM adapter instance, lazily initialised.

    Returns:
        The LLMAdapter instance that all LLM calls should go through.
    """
    global _adapter
    if _adapter is None:
        _adapter = LLMAdapter()
    return _adapter


def chat(messages: List[dict[str, str]], **kwargs: Any) -> dict[str, Any]:
    """Convenience function: send messages through the global adapter.

    Args:
        messages: List of message dicts with 'role' and 'content'.
        **kwargs: Additional parameters.

    Returns:
        LLM response dict marked with LLM-LINK.
    """
    return get_adapter().chat_completion(messages, **kwargs)


def chat_stream(messages: List[dict[str, str]], **kwargs: Any) -> Any:
    """Convenience function: stream LLM response through the global adapter.

    Args:
        messages: List of message dicts.
        **kwargs: Additional parameters.

    Yields:
        Marked text chunks.
    """
    yield from get_adapter().chat_completion_stream(messages, **kwargs)