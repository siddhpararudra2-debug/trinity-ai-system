from app.core.errors import CapabilityUnavailableError
class FreeCADAdapter:
    name = "freecad"
    def export_step(self, model, output_path: str) -> None:
        raise CapabilityUnavailableError("FreeCAD adapter is not configured", details={"code": "CAPABILITY_UNAVAILABLE"})
