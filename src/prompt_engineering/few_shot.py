"""Contextual examples injector (mirrors intelligence/router.py example)."""
from __future__ import annotations

EXAMPLES = [
    ("Create a 50 mm quadcopter frame", {"domain": "cad", "operation": "generate", "object": "quadcopter_frame"}),
    ("Solve x**2 - 4 = 0", {"domain": "math", "operation": "solve"}),
]


def inject_examples(prompt: str, n: int = 2) -> str:
    shots = "\n".join(f"Ex: {q} -> {a}" for q, a in EXAMPLES[:n])
    return f"{shots}\nNow: {prompt}"
