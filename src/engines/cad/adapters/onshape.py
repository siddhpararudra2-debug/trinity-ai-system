from src.core.errors import CapabilityUnavailableError


class OnshapeAdapter:
    name = "onshape"

    def export_step(self, model, output_path: str) -> None:
        raise CapabilityUnavailableError(
            "Onshape credentials are not configured",
            details={"code": "ONSHAPE_NOT_CONFIGURED"},
        )
