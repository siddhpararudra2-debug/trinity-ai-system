import json
from pathlib import Path

import pytest

from src.agents.coder import CoderAgent
from src.agents.researcher import ResearcherAgent
from src.core.model_config import load_model_config, load_prompt_templates
from src.intelligence.router import parse_requirement
from src.llm.claude_client import ClaudeClient
from src.llm.openai_client import OpenAIClient
from src.prompt_engineering.chain import run_chain
from src.prompt_engineering.few_shot import inject_examples
from src.utils.rate_limiter import RateLimiter
from src.utils.token_counter import count_tokens
from src.utils.vector_store import VectorStore


def test_config_loads_and_matches_router():
    cfg = load_model_config()
    assert "math" in cfg["engines"] and "cad" in cfg["engines"]
    parsed = parse_requirement("Create a 50 mm quadcopter frame")
    assert parsed["domain"] == "cad"
    prompts = load_prompt_templates()
    assert "system" in prompts


def test_golden_files_match_router():
    repo = Path(__file__).resolve().parents[1]
    cad = json.loads((repo / "data/evaluation/golden_cad.json").read_text())
    case = cad["cases"][0]
    parsed = parse_requirement(case["input"])
    assert parsed["domain"] == case["expected"]["engine"]
    assert parsed["operation"] == case["expected"]["operation"]


def test_chain_and_few_shot():
    out = run_chain("Create a 50 mm quadcopter frame")
    assert out["plan"] == ["n1"]
    assert out["toolcall"]["engine"] == "cad"
    with pytest.raises(Exception):
        run_chain("do something unparseable xyz")
    assert "Create a 50" in inject_examples("hi")


def test_agents_emit_toolcalls():
    assert ResearcherAgent().to_toolcall("q")["engine"] == "research"
    assert CoderAgent().to_toolcall("s") == {
        "engine": "firmware",
        "operation": "create",
        "parameters": {"spec": "s"},
    }


def test_llm_clients_stamp_toolcalls():
    req = {"engine": "math", "operation": "solve", "parameters": {"expression": "1+1"}}
    assert OpenAIClient().tool_call(req) == req
    assert ClaudeClient().tool_call(req) == req


def test_utils():
    assert count_tokens("hello world") > 0
    RateLimiter(calls_per_minute=600).acquire()
    vs = VectorStore()
    assert vs.persist_dir.name == "embeddings"
    with pytest.raises(NotImplementedError):
        vs.query([0.0])
