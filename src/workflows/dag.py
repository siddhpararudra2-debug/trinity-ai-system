from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass
class WorkflowNode:
    id: str
    engine: str
    operation: str
    parameters: dict[str, Any]
    dependencies: list[str] = field(default_factory=list)
    status: str = "queued"


def ordered(nodes: list[WorkflowNode]) -> list[WorkflowNode]:
    if len({node.id for node in nodes}) != len(nodes):
        raise ValueError("Workflow has duplicate node IDs")
    remaining = {node.id: node for node in nodes}
    result: list[WorkflowNode] = []
    while remaining:
        ready = [
            node
            for node in remaining.values()
            if all(dep in {n.id for n in result} for dep in node.dependencies)
        ]
        if not ready:
            raise ValueError("Workflow has a missing dependency or cycle")
        for node in ready:
            result.append(node)
            del remaining[node.id]
    return result
