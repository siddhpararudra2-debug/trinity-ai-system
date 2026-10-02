from typing import Protocol

from src.engines.cad.ir import PartIR


class CADAdapter(Protocol):
    name: str

    def generate(self, ir: PartIR): ...
    def export_step(self, model, output_path: str): ...
