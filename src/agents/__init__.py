"""Specialized role definitions — thin adapters over deterministic engines."""
from src.agents.coder import CoderAgent
from src.agents.researcher import ResearcherAgent

__all__ = ["ResearcherAgent", "CoderAgent"]
