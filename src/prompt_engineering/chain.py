"""Sequential / DAG routing (wraps workflows/dag.py + intelligence/router.py)."""
from __future__ import annotations

from typing import Any

from src.intelligence.router import parse_requirement
from src.workflows.dag import WorkflowNode, ordered


def run_chain(text: str) -> dict[str, Any]:
    """Parse NL requirement -> single-node DAG -> ordered execution plan."""
    parsed = parse_requirement(text)
    if parsed["domain"] == "math":
        parameters: dict[str, Any] = {"expression": parsed["parameters"]["expression"]}
    else:
        parameters = {
            "type": parsed["object"],
            "parameters": parsed["parameters"],
            "outputs": ["stl", "glb", "json"],
        }
    node = WorkflowNode(
        id="n1",
        engine=parsed["domain"],
        operation=parsed["operation"],
        parameters=parameters,
    )
    return {"plan": [n.id for n in ordered([node])], "toolcall": {"engine": node.engine, "operation": node.operation, "parameters": node.parameters}}
