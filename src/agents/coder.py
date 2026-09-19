"""Coder role — wraps firmware/pcb scaffolds via the job manager contract."""
from __future__ import annotations

from typing import Any


class CoderAgent:
    engine = "firmware"
    operation = "create"

    def to_toolcall(self, spec: str) -> dict[str, Any]:
        return {"engine": self.engine, "operation": self.operation, "parameters": {"spec": spec}}
