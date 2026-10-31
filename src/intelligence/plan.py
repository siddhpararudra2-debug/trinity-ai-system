"""Structured plan contract for /api/intelligence/stream-plan (M15).

The model path must emit an *executable* plan, not prose: the final SSE
frame carries validated `plan.nodes[]` in the exact WorkflowNodeModel shape
that POST /api/workflows/execute accepts, so a plan can be run unchanged.
Shape, dependency and DAG-semantics failures degrade to an honest
`plan_invalid` error event — never to a plan the UI has to interpret and
never to exec-ed prose. Dependency threading follows the workflow
executor's `{node:<id>}.<dotted.path>` convention.
"""

from __future__ import annotations

import json
from typing import Any

from src.agents.loop import _first_json_object
from src.workflows.dag import WorkflowNode, ordered as ordered_nodes

# A plan is a proposal over real engine calls; a bounded list keeps one
# degenerate reply from registering an unbounded workflow.
MAX_PLAN_NODES = 32

PLAN_JSON_EXAMPLE = (
    '{"summary": "<one sentence>", '
    '"nodes": ['
    '{"id": "n1", "engine": "<engine>", "operation": "<operation>", '
    '"parameters": {}, "dependencies": []}, '
    '{"id": "n2", "engine": "<engine>", "operation": "<operation>", '
    '"parameters": {"<param>": "{node:n1}.result.<path>"}, '
    '"dependencies": ["n1"]}'
    "]}"
)


def build_plan_system_prompt() -> str:
    """Describe the JSON contract over the engines that really exist.

    The roster comes from the live registry (the same truth GET /engines
    serves), so the prompt can never advertise an engine that is not
    actually present.
    """
    from src.engines.registry import registry

    roster = "\n".join(
        f"- {entry['name']}: {', '.join(entry['capabilities'])}"
        for entry in registry.list()
    )
    return (
        "You are the Trinity planning layer. Turn the request into a small "
        "workflow of real engineering-engine calls. Reply with EXACTLY one "
        "JSON object and nothing else:\n"
        f"  {PLAN_JSON_EXAMPLE}\n"
        "Rules: node ids are short and unique; dependencies list ids of "
        "other nodes in this plan; a dependency to thread a value forward "
        "may mention {node:<id>}.<dotted.path> inside a parameter value; "
        "parameters must be engine call parameters, never prose. Only use "
        "engines and operations from this roster:\n"
        f"{roster}\n"
        "Do not execute anything — planning only."
    )


def build_plan_prompt(text: str) -> str:
    """The user turn: the bare requirement, nothing else."""
    return f"Request: {text}"


class PlanError(ValueError):
    """A model plan could not be parsed into an executable DAG."""


def parse_plan(reply: str) -> dict[str, Any]:
    """Pull the plan object out of a (possibly wordy) model reply."""
    block = _first_json_object(reply)
    if block is None:
        raise PlanError("plan reply contained no JSON object")
    try:
        data = json.loads(block)
    except ValueError as exc:
        raise PlanError(f"plan JSON was malformed: {exc}") from exc
    if not isinstance(data, dict):
        raise PlanError("plan was not a JSON object")
    if not isinstance(data.get("nodes"), list):
        raise PlanError("plan JSON had no nodes array")
    summary = data.get("summary", "")
    return {"summary": str(summary), "nodes": data["nodes"]}


def validate_plan(plan: dict[str, Any]) -> list[WorkflowNode]:
    """Shape + DAG validation. Returns nodes in topological order.

    Every failure names the node and the rule, so the error frame can be
    shown verbatim without the UI guessing.
    """
    nodes_raw = plan.get("nodes")
    if not isinstance(nodes_raw, list) or not nodes_raw:
        raise PlanError("plan must contain a non-empty nodes array")
    if len(nodes_raw) > MAX_PLAN_NODES:
        raise PlanError(
            f"plan has {len(nodes_raw)} nodes; the maximum is {MAX_PLAN_NODES}"
        )

    nodes: list[WorkflowNode] = []
    seen: set[str] = set()
    for index, raw in enumerate(nodes_raw):
        if not isinstance(raw, dict):
            raise PlanError(f"node {index} is not a JSON object")
        missing = [f for f in ("id", "engine", "operation") if not raw.get(f)]
        if missing:
            raise PlanError(
                f"node {index}: missing or empty {', '.join(missing)}"
            )
        node_id = str(raw["id"]).strip()
        if not node_id or any(ch.isspace() for ch in node_id):
            raise PlanError(f"node {index}: id '{raw['id']}' is unusable")
        if node_id in seen:
            raise PlanError(f"duplicate node id '{node_id}'")
        seen.add(node_id)
        parameters = raw.get("parameters", {})
        if not isinstance(parameters, dict):
            raise PlanError(
                f"node {index} ('{node_id}'): parameters must be an object"
            )
        dependencies = raw.get("dependencies", [])
        if not isinstance(dependencies, list) or not all(
            isinstance(dep, str) for dep in dependencies
        ):
            raise PlanError(
                f"node {index} ('{node_id}'): dependencies must be a list of ids"
            )
        nodes.append(
            WorkflowNode(
                id=node_id,
                engine=str(raw["engine"]).strip(),
                operation=str(raw["operation"]).strip(),
                parameters=dict(parameters),
                dependencies=list(dependencies),
            )
        )

    declared = seen
    for node in nodes:
        for dep in node.dependencies:
            if dep not in declared:
                raise PlanError(
                    f"node '{node.id}' depends on '{dep}' which the plan does not define"
                )
    try:
        order = ordered_nodes(nodes)
    except ValueError as exc:
        raise PlanError(f"plan is not an executable DAG: {exc}") from exc
    return order


def plan_frame(source: str, summary: str, nodes: list[WorkflowNode]) -> dict[str, Any]:
    """The frames' plan section: exactly POST /api/workflows/execute's shape."""
    return {
        "plan": {
            "source": source,
            "summary": summary,
            "nodes": [
                {
                    "id": node.id,
                    "engine": node.engine,
                    "operation": node.operation,
                    "parameters": node.parameters,
                    "dependencies": node.dependencies,
                }
                for node in nodes
            ],
        }
    }
