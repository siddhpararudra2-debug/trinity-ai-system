"""Optional CadQuery boundary. It never fabricates STEP when unavailable."""

from app.core.errors import CapabilityUnavailableError


class CadQueryAdapter:
    name = "cadquery"

    def export_step(self, model, output_path: str) -> None:
        try:
            import cadquery as cq  # noqa: F401 — availability probe
        except ImportError as exc:
            raise CapabilityUnavailableError(
                "CadQuery/OpenCascade is not installed",
                details={"code": "CAD_KERNEL_UNAVAILABLE"},
            ) from exc
        raise CapabilityUnavailableError(
            "CadQuery STEP conversion is not available for the native mesh model",
            details={"code": "CAD_KERNEL_UNAVAILABLE"},
        )
