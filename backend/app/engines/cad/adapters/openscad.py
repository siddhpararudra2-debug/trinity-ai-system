from app.core.errors import CapabilityUnavailableError


class OpenSCADAdapter:
    name = "openscad"

    def export_step(self, model, output_path: str) -> None:
        raise CapabilityUnavailableError(
            "OpenSCAD adapter is not configured",
            details={"code": "CAPABILITY_UNAVAILABLE"},
        )
