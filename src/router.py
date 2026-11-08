"""Router + workflow planner (PRD §24, §31).

Simple execution: one engine call.
Complex execution: multi-step DAG plan via workflow nodes.

This module lets an LLM tool-caller ask "what can Trinity do?" and
get a structured answer, and also execute plans consisting of ordered
engine operations.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Optional

from src.engines.registry import EngineRegistry, EngineNotFoundError
from src.workflows.dag import WorkflowNode, ordered


class Router:
    """Router + workflow planner: Simple = one tool call, Complex = multi-step plan."""

    def __init__(self, registry: EngineRegistry) -> None:
        self.registry = registry

    def execute_simple(
        self, engine_name: str, operation: str, parameters: dict[str, Any]
    ) -> dict[str, Any]:
        """Execute a single engine operation (simple mode).

        Returns a dict with ``status``, ``result``, and ``error`` keys.
        """
        try:
            engine = self.registry.get(engine_name)
            result = engine.execute(operation, parameters)
            artifacts = [
                {"artifact_id": a.artifact_id, "type": a.type, "path": a.path}
                for a in result.artifacts
            ]
            # EngineResult.result should be a dict; ensure we have a dict
            er = result.result if isinstance(result.result, dict) else {}
            return {
                "status": "completed",
                "result": {
                    "engine": engine_name,
                    "operation": operation,
                    "success": result.success,
                    "engine_result": er,  # full EngineResult.result dict (may be empty)
                    "artifacts": artifacts,
                    "pending_artifacts": result.pending_artifacts,
                },
                "error": None,
            }
        except EngineNotFoundError as e:
            return {"status": "error", "result": None, "error": str(e)}
        except Exception as exc:  # pragma: no cover
            return {"status": "error", "result": None, "error": f"{type(exc).__name__}: {exc}"}

    def execute_complex(
        self, nodes: list[dict[str, Any]]
    ) -> dict[str, Any]:
        """Execute a DAG of engine operations (complex mode).

        Each node is a dict with keys: ``id``, ``engine``, ``operation``,
        ``parameters``, and optionally ``dependencies`` (list of node IDs).

        The nodes are topologically sorted (via ``workflows.dag.ordered``)
        and executed in order.  If any node fails, remaining nodes are
        still executed but marked ``failed``.
        """
        # Convert dict nodes to PlanNode-like objects
        plan_nodes: list[Any] = []
        for node_dict in nodes:
            deps = node_dict.get("dependencies", [])
            plan_nodes.append(
                type(
                    "PlanNode",
                    (object,),
                    {
                        "id": node_dict["id"],
                        "engine": node_dict["engine"],
                        "operation": node_dict["operation"],
                        "parameters": node_dict["parameters"],
                        "dependencies": deps,
                        "status": "queued",
                    },
                )()
            )

        # Topological sort using the existing DAG helper
        try:
            sorted_nodes = ordered(plan_nodes)  # type: ignore[arg-type]
        except ValueError as e:
            return {"status": "error", "result": None, "error": str(e)}

        # Execute in topological order
        remaining: dict[str, Any] = {node.id: node for node in sorted_nodes}
        executed: list[Any] = []

        while remaining:
            ready = [
                node
                for node in remaining.values()
                if all(dep in {n.id for n in executed} for dep in node.dependencies)
            ]
            if not ready:
                # Remaining nodes have missing dependencies or a cycle
                return {
                    "status": "error",
                    "result": None,
                    "error": "Missing dependency or cycle in workflow",
                }

            for node in ready:
                exec_result = self.execute_simple(node.engine, node.operation, node.parameters)
                node.status = "completed" if exec_result["status"] == "completed" else "failed"
                executed.append(node)
                del remaining[node.id]

        # Build results dict
        results: dict[str, Any] = {}
        for node in executed:
            results[node.id] = {
                "status": node.status,
                "engine": node.engine,
                "operation": node.operation,
            }

        # If any node failed, surface the first error
        first_error: Optional[str] = None
        for node in executed:
            if node.status == "failed" and first_error is None:
                # Find the error from the last execute_simple call
                first_error = "node execution failed"  # simplified

        return {
            "status": "completed" if all(n.status == "completed" for n in executed) else "partial",
            "results": results,
            "error": first_error,
        }

    def execute_plan(self, plan: dict[str, Any]) -> dict[str, Any]:
        """Execute a plan given as a dict with ``nodes`` and optional ``entry_point``.

        ``nodes`` is a list of node dicts as described in ``execute_complex``.
        """
        nodes = plan.get("nodes", [])
        if not nodes:
            return {"status": "error", "result": None, "error": "Empty plan"}

        return self.execute_complex(nodes)

    # ------------------------------------------------------------------
    # Helper: build a CAD workflow for common use-cases
    # ------------------------------------------------------------------

    @staticmethod
    def cad_enclosure_lid_plan(
        lid_type: str = "removable",
        thickness_mm: float = 3.0,
        clearance_mm: float = 0.5,
        include_drawing: bool = True,
    ) -> dict[str, Any]:
        """Build a DAG plan for designing an enclosure with a lid.

        The plan flows:
          1. Generate the base enclosure (CAD generate)
          2. Generate the lid part (CAD generate, lid_type)
          3. Validate both parts
          4. (Optional) Export drawings

        Returns a node-plan dict ready for ``execute_complex``.
        """
        nodes: list[dict[str, Any]] = []

        # Node 1: Base enclosure generation
        nodes.append(
            {
                "id": "enclosure_base",
                "engine": "cad",
                "operation": "generate",
                "parameters": {
                    "type": "enclosure",
                    "parameters": {
                        "length_mm": 200.0,
                        "width_mm": 150.0,
                        "height_mm": 100.0,
                        "wall_thickness_mm": 3.0,
                        "lid": lid_type,
                    },
                    "outputs": ["stl", "json"],
                },
            }
        )

        # Node 2: Lid generation
        nodes.append(
            {
                "id": "lid_generation",
                "engine": "cad",
                "operation": "generate",
                "parameters": {
                    "type": "lid",
                    "parameters": {
                        "lid_type": lid_type,
                        "thickness_mm": thickness_mm,
                        "fit_clearance_mm": clearance_mm,
                    },
                    "outputs": ["stl", "json"],
                },
                "dependencies": ["enclosure_base"],
            }
        )

        # Node 3: Validate enclosure
        nodes.append(
            {
                "id": "enclosure_validate",
                "engine": "cad",
                "operation": "validate",
                "parameters": {
                    "type": "enclosure",
                    "parameters": {
                        "length_mm": 200.0,
                        "width_mm": 150.0,
                        "height_mm": 100.0,
                        "wall_thickness_mm": 3.0,
                    },
                },
                "dependencies": ["enclosure_base"],
            }
        )

        # Node 4: Validate lid
        nodes.append(
            {
                "id": "lid_validate",
                "engine": "cad",
                "operation": "validate",
                "parameters": {
                    "type": "lid",
                    "parameters": {
                        "lid_type": lid_type,
                        "thickness_mm": thickness_mm,
                        "fit_clearance_mm": clearance_mm,
                    },
                },
                "dependencies": ["lid_generation"],
            }
        )

        # Node 5: Optional drawings
        if include_drawing:
            nodes.append(
                {
                    "id": "enclosure_drawing",
                    "engine": "cad",
                    "operation": "drawings",
                    "parameters": {
                        "type": "enclosure",
                        "parameters": {
                            "length_mm": 200.0,
                            "width_mm": 150.0,
                            "height_mm": 100.0,
                            "wall_thickness_mm": 3.0,
                        },
                        "views": ["top", "front", "side"],
                    },
                    "dependencies": ["enclosure_validate"],
                }
            )

            nodes.append(
                {
                    "id": "lid_drawing",
                    "engine": "cad",
                    "operation": "drawings",
                    "parameters": {
                        "type": "lid",
                        "parameters": {
                            "lid_type": lid_type,
                            "thickness_mm": thickness_mm,
                            "fit_clearance_mm": clearance_mm,
                        },
                        "views": ["top", "front"],
                    },
                    "dependencies": ["lid_validate"],
                }
            )

        return {
            "status": "ready",
            "plan": {
                "nodes": nodes,
            },
        }