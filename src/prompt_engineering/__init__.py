"""Middleware for string assembly — DAG routing + few-shot injection."""
from src.prompt_engineering.chain import run_chain
from src.prompt_engineering.few_shot import inject_examples

__all__ = ["run_chain", "inject_examples"]
