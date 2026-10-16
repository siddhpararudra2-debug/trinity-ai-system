"""Optional CadQuery/OpenCascade boundary.

This used to be an availability probe that always refused: the kernel could
be imported and still no STEP came out, because nothing bridged the mesh to
B-rep geometry. src.engines.cad.step now does that bridging, so this adapter
performs the real export when a kernel is installed and refuses with the
same CAD_KERNEL_UNAVAILABLE code when one is not. It never fabricates a STEP
file it did not write.
"""

from pathlib import Path

from src.core.errors import CapabilityUnavailableError
from src.engines.cad.step import kernel_info, write_step


class CadQueryAdapter:
    name = "cadquery"

    @property
    def available(self) -> bool:
        return bool(kernel_info()["available"])

    def export_step(self, model, output_path: str) -> dict:
        """Write `model` (a Mesh) to `output_path` as AP214 STEP."""
        if not self.available:
            raise CapabilityUnavailableError(
                str(kernel_info().get("reason", "CAD kernel is not installed")),
                details={"code": "CAD_KERNEL_UNAVAILABLE", "format": "step"},
            )
        return write_step(model, Path(output_path))