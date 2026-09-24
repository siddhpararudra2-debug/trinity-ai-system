import pytest

from src.core.config import resolve_cors_origins
from src.core.errors import RequestValidationError
from src.intelligence.router import parse_requirement


def test_cors_wildcard_allowed_in_development():
    assert resolve_cors_origins("development", ["*"]) == ["*"]


def test_cors_explicit_origins_allowed_in_production():
    origins = resolve_cors_origins("production", ["https://app.example.com"])
    assert origins == ["https://app.example.com"]


def test_cors_wildcard_refused_in_production():
    with pytest.raises(ValueError, match="refused"):
        resolve_cors_origins("production", ["*"])


def test_cors_empty_refused_in_production():
    with pytest.raises(ValueError, match="refused"):
        resolve_cors_origins("production", [])


def test_router_rejects_non_positive_frame_size():
    with pytest.raises(RequestValidationError):
        parse_requirement("Create a 0 mm quadcopter frame")


def test_router_error_declares_scope():
    with pytest.raises(RequestValidationError) as exc_info:
        parse_requirement("do something unparseable xyz")
    details = exc_info.value.details or {}
    assert "parser_scope" in details
