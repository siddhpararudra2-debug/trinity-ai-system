"""
Trinity error hierarchy.

Every engine failure must be classified so the caller (eventually an LLM)
can tell the difference between "your input was bad", "the engine tried
and failed", and "the system itself broke". This maps directly onto the
GENERATED / VALIDATED / VERIFIED distinction in PRD section 29.
"""
from __future__ import annotations


class TrinityError(Exception):
    """Base class for all Trinity errors."""

    code = "trinity_error"

    def __init__(self, message: str, *, details: dict | None = None):
        super().__init__(message)
        self.message = message
        self.details = details or {}

    def to_dict(self) -> dict:
        return {"code": self.code, "message": self.message, "details": self.details}


class RequestValidationError(TrinityError):
    """The incoming request was malformed or failed schema/business validation."""

    code = "request_validation_error"


class EngineNotFoundError(TrinityError):
    """No engine registered under the requested name."""

    code = "engine_not_found"


class EngineExecutionError(TrinityError):
    """The engine ran but could not produce a result (e.g. unsolvable equation)."""

    code = "engine_execution_error"


class GeometryValidationError(TrinityError):
    """CAD geometry was generated but failed validation (clearances, topology, etc.)."""

    code = "geometry_validation_error"


class JobNotFoundError(TrinityError):
    code = "job_not_found"


class ArtifactNotFoundError(TrinityError):
    code = "artifact_not_found"


class CapabilityUnavailableError(TrinityError):
    code = "capability_unavailable"
