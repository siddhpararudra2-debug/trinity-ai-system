"""YAML config loader — keeps config/*.yaml as source of truth, enforced by tests."""
from __future__ import annotations

from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
CONFIG_DIR = REPO_ROOT / "config"


def load_yaml(name: str) -> dict[str, Any]:
    try:
        import yaml
    except ImportError as exc:
        raise RuntimeError("pyyaml required: pip install pyyaml") from exc
    path = CONFIG_DIR / name
    with path.open() as fh:
        return yaml.safe_load(fh) or {}


def load_model_config() -> dict[str, Any]:
    return load_yaml("model_config.yaml")


def load_prompt_templates() -> dict[str, Any]:
    return load_yaml("prompt_templates.yaml")
