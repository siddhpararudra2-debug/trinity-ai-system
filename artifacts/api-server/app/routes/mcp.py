"""MCP-compatible tool surface for external AI assistants and IDEs."""
from __future__ import annotations

from typing import Any

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from app.engines.general_ai import GeneralAIEngine
from app.engines.literature_engine import LiteratureEngine
from app.engines.math_engine import MathEngine
from app.engines.quantum_engine import QuantumEngine
from app.pipelines.paper_to_code import PaperToCodePipeline
from app.unified_router.orchestrator import TrinityOrchestrator

router = APIRouter(tags=["mcp"])

_TOOLS: list[dict[str, Any]] = [
    {"name": "trinity_math", "description": "Symbolic math via SymPy", "inputSchema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}},
    {"name": "trinity_quantum", "description": "Quantum circuit simulation", "inputSchema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}},
    {"name": "trinity_literature", "description": "arXiv literature search", "inputSchema": {"type": "object", "properties": {"query": {"type": "string"}, "max_results": {"type": "integer"}}, "required": ["query"]}},
    {"name": "trinity_chat", "description": "Route a natural-language request through the Trinity orchestrator", "inputSchema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}},
    {"name": "trinity_general", "description": "General AI assistance fallback", "inputSchema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}},
    {"name": "trinity_paper_to_code", "description": "Extract equations from paper text into SymPy blocks", "inputSchema": {"type": "object", "properties": {"paper_text": {"type": "string"}}, "required": ["paper_text"]}},
]

_orchestrator = TrinityOrchestrator()
_math = MathEngine()
_quantum = QuantumEngine()
_literature = LiteratureEngine()
_general = GeneralAIEngine()
_paper = PaperToCodePipeline()


class McpToolCall(BaseModel):
    arguments: dict[str, Any] = Field(default_factory=dict)


@router.get("/mcp/tools")
async def list_mcp_tools():
    return {"tools": _TOOLS, "protocol": "mcp-http-bridge", "version": "1.0.0"}


@router.post("/mcp/tools/{tool_name}")
async def invoke_mcp_tool(tool_name: str, request: McpToolCall):
    args = request.arguments
    if tool_name == "trinity_math":
        result = await _math.process(str(args.get("query", "")), "auto")
    elif tool_name == "trinity_quantum":
        result = await _quantum.process(str(args.get("query", "")), 2, 1024)
    elif tool_name == "trinity_literature":
        result = await _literature.process(str(args.get("query", "")), int(args.get("max_results") or 5))
    elif tool_name == "trinity_chat":
        result = await _orchestrator.route(str(args.get("query", "")))
    elif tool_name == "trinity_general":
        result = await _general.process(str(args.get("query", "")))
    elif tool_name == "trinity_paper_to_code":
        result = await _paper.run(str(args.get("paper_text", "")))
    else:
        raise HTTPException(status_code=404, detail=f"Unknown MCP tool: {tool_name}")
    return {"tool": tool_name, "content": [{"type": "text", "text": str(result)}], "structured": result}
