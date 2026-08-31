"""Direct engine endpoints — bypass the unified router."""
from fastapi import APIRouter, File, UploadFile
from pydantic import BaseModel

from app.engines.math_engine import MathEngine
from app.engines.quantum_engine import QuantumEngine
from app.engines.maker_cad import MakerCadEngine
from app.engines.maker_pcb import MakerPcbEngine
from app.engines.literature_engine import LiteratureEngine

router = APIRouter(prefix="/engines", tags=["engines"])

# Singletons
_math = MathEngine()
_quantum = QuantumEngine()
_cad = MakerCadEngine()
_pcb = MakerPcbEngine()
_literature = LiteratureEngine()

ENGINES_REGISTRY = [
    {
        "id": "math",
        "name": "Trinity Math Engine",
        "description": "Symbolic solving (SymPy), integration, differentiation, dimensional analysis",
        "status": "ready",
        "trigger_words": ["solve", "calculate", "integrate", "differentiate", "equation", "simplify", "expand", "factor"],
        "icon": "∑",
    },
    {
        "id": "quantum",
        "name": "Trinity Quantum Lab",
        "description": "Quantum circuit simulation, Bloch sphere visualization, state-vector analysis",
        "status": "ready",
        "trigger_words": ["quantum", "qubit", "circuit", "bloch", "entangle", "superposition", "hadamard", "bell state"],
        "icon": "⚛",
    },
    {
        "id": "maker_cad",
        "name": "Trinity Maker Engine (CAD)",
        "description": "Generates Fusion 360 Python API scripts for parametric mechanical parts",
        "status": "ready",
        "trigger_words": ["design part", "cad", "3d model", "fusion 360", "bracket", "housing", "gear", "mechanical"],
        "icon": "⚙",
    },
    {
        "id": "maker_pcb",
        "name": "Trinity Maker Engine (PCB)",
        "description": "Generates KiCad .kicad_sch and .kicad_pcb S-expression files",
        "status": "ready",
        "trigger_words": ["pcb", "schematic", "circuit board", "kicad", "esp32", "arduino", "route board"],
        "icon": "🔌",
    },
    {
        "id": "literature",
        "name": "Trinity Literature RAG",
        "description": "Vector search (arXiv), knowledge graph (NetworkX), paper summarization",
        "status": "ready",
        "trigger_words": ["paper", "research", "arxiv", "literature", "study", "publication", "find papers"],
        "icon": "📚",
    },
    {
        "id": "vision",
        "name": "Trinity Vision Engine",
        "description": "Handwritten OCR (pix2tex), publication plotting, LaTeX/PDF export",
        "status": "ready",
        "trigger_words": ["ocr", "handwritten", "scan", "image to latex", "extract text", "pdf export"],
        "icon": "👁",
    },
    {
        "id": "collab",
        "name": "Trinity Collab Desktop",
        "description": "Real-time WebSocket notebooks for multi-user research collaboration",
        "status": "ready",
        "trigger_words": ["collaborate", "share notebook", "real-time", "team research"],
        "icon": "🤝",
    },
    {
        "id": "orchestrator",
        "name": "Trinity Autonomous Orchestrator",
        "description": "ReAct loop that chains all engines for self-driving research pipelines",
        "status": "ready",
        "trigger_words": ["research pipeline", "automate", "chain engines", "investigate"],
        "icon": "🔬",
    },
]


@router.get("")
async def list_engines():
    return ENGINES_REGISTRY


# ---------------------------------------------------------------------------
# Math
# ---------------------------------------------------------------------------

class MathInput(BaseModel):
    expression: str
    operation: str = "auto"


@router.post("/math")
async def run_math(data: MathInput):
    return await _math.process(data.expression, data.operation)


# ---------------------------------------------------------------------------
# Quantum
# ---------------------------------------------------------------------------

class QuantumInput(BaseModel):
    circuit_description: str
    num_qubits: int = 2
    shots: int = 1024


@router.post("/quantum")
async def run_quantum(data: QuantumInput):
    return await _quantum.process(data.circuit_description, data.num_qubits, data.shots)


# ---------------------------------------------------------------------------
# Maker — CAD
# ---------------------------------------------------------------------------

class MakerCadInput(BaseModel):
    description: str
    parameters: dict = {}


@router.post("/maker/cad")
async def run_maker_cad(data: MakerCadInput):
    return await _cad.process(data.description, data.parameters)


# ---------------------------------------------------------------------------
# Maker — PCB
# ---------------------------------------------------------------------------

class MakerPcbInput(BaseModel):
    description: str
    components: list[str] = []


@router.post("/maker/pcb")
async def run_maker_pcb(data: MakerPcbInput):
    return await _pcb.process(data.description, data.components)


# ---------------------------------------------------------------------------
# Literature
# ---------------------------------------------------------------------------

class LiteratureInput(BaseModel):
    query: str
    max_results: int = 5


@router.post("/literature")
async def run_literature(data: LiteratureInput):
    return await _literature.process(data.query, data.max_results)


# ---------------------------------------------------------------------------
# Vision
# ---------------------------------------------------------------------------

@router.post("/vision/upload")
async def run_vision_upload(file: UploadFile = File(...)):
    contents = await file.read()
    return await _vision.process(image_data=contents)
