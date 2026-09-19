"""Tracking token usage per call (heuristic; replace with tiktoken if needed)."""
from __future__ import annotations


def count_tokens(text: str) -> int:
    return max(1, len(text.split()) * 4 // 3)
