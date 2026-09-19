from typing import Protocol

from src.engines.cad.ir import QuadcopterFrameIR


class CADAdapter(Protocol):
    name: str

    def generate(self, ir: QuadcopterFrameIR): ...
    def export_step(self, model, output_path: str): ...
