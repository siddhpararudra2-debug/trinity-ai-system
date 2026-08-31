"""Universal firmware engine and specialized submodule routing."""
from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any

from app.engines.base import BaseEngine
from app.firmware.generator import FirmwareGenerator, GeneratedFirmwareProject
from app.firmware.knowledge import extract_hardware_identifier, lookup_hardware
from app.firmware.llm import FirmwareLLMClient
from app.firmware.models import FirmwareFile, FirmwareRequest, FirmwareSpec, FirmwareTarget
from app.firmware.prompts import BASE_SYSTEM_PROMPT, template_for


@dataclass(frozen=True)
class FirmwareSubmodule:
    id: str
    patterns: tuple[str, ...]


SUBMODULES = (
    FirmwareSubmodule("arduino", ("arduino", "setup(", "loop()")),
    FirmwareSubmodule("esp_idf", ("esp-idf", "idf.py", "esp32")),
    FirmwareSubmodule("zephyr", ("zephyr", "device tree", "prj.conf")),
    FirmwareSubmodule("freertos", ("freertos", "free rtos", "xqueu", "xsemaphore")),
    FirmwareSubmodule("micropython", ("micropython", "circuitpython", "machine.pin")),
    FirmwareSubmodule("rust_embedded", ("embedded rust", "embedded-hal", "no_std", "cargo.toml")),
    FirmwareSubmodule("fpga", ("verilog", "systemverilog", "vhdl", "fpga", "testbench")),
    FirmwareSubmodule("assembly", ("assembly", "arm thumb", "risc-v", "riscv")),
    FirmwareSubmodule("bootloader", ("bootloader", "secure boot", "firmware update", "ota")),
    FirmwareSubmodule("linux_driver", ("linux driver", "kernel module", "device driver")),
    FirmwareSubmodule("build_system", ("cmakelists", "makefile", "platformio", "linker script", "device tree")),
    FirmwareSubmodule("bare_metal", ("bare-metal", "bare metal", "register", "gpio", "uart", "spi", "i2c", "interrupt", "dma")),
)


class FirmwareEngine(BaseEngine):
    engine_id = "firmware"

    def __init__(self, generator: FirmwareGenerator | None = None, llm: FirmwareLLMClient | None = None) -> None:
        self.generator = generator or FirmwareGenerator()
        self.llm = llm or FirmwareLLMClient()
        self.context: dict[str, list[FirmwareFile]] = {}

    def detect_submodule(self, request: FirmwareRequest, target: FirmwareTarget | None = None) -> str:
        text = f"{request.description} {request.framework or ''} {request.language or ''} {target.framework if target else ''} {target.language if target else ''}".lower()
        scores = {item.id: sum(text.count(pattern.lower()) for pattern in item.patterns) for item in SUBMODULES}
        return max(scores, key=scores.get) if scores and max(scores.values()) else "bare_metal"

    def build_prompt(self, request: FirmwareRequest, target: FirmwareTarget | None, previous: list[FirmwareFile] | None = None) -> tuple[str, str]:
        submodule = self.detect_submodule(request, target)
        hardware_id = extract_hardware_identifier(request.description) or (target.mcu if target else request.target_id)
        knowledge = lookup_hardware(hardware_id)
        system = f"{BASE_SYSTEM_PROMPT}\n\nParadigm: {submodule}.\n{template_for(submodule)}"
        if knowledge:
            system += f"\nHardware facts (verify against the datasheet): {knowledge}"
        context = ""
        context_files = list(previous or [])
        if request.previous_code:
            context_files.append(FirmwareFile(path="previous_generated_code.txt", content=request.previous_code, language="text", kind="context", line_count=request.previous_code.count("\n")))
        if context_files:
            context = "\nPrevious generated files for iterative refinement:\n" + "\n".join(f"FILE: {item.path}\n{item.content}" for item in context_files[-12:])
        user = (
            f"Target: {target.name if target else request.target_id or 'unresolved'}\n"
            f"Framework: {request.framework or target.framework if target else request.framework or 'unspecified'}\n"
            f"Language: {request.language or target.language if target else request.language or 'unspecified'}\n"
            f"Project: {request.project_name}\nRequest: {request.description}\n"
            f"Requested files: {', '.join(request.requested_files) or 'choose the minimum complete project'}\n"
            f"Return JSON {{\"files\": {{\"path\": \"content\"}}, \"dependencies\": [], \"warnings\": []}} only."
            + context
        )
        return system, user

    async def generate(self, request: FirmwareRequest, target: FirmwareTarget, spec: FirmwareSpec, conversation_key: str | None = None) -> tuple[GeneratedFirmwareProject, dict[str, Any]]:
        previous = self.context.get(conversation_key or "")
        system, user = self.build_prompt(request, target, previous)
        llm_result = await self.llm.generate(system, user)
        if llm_result and isinstance(llm_result.get("files"), dict):
            files = {str(path): str(content) for path, content in llm_result["files"].items() if self._safe_path(str(path))}
            if not files:
                raise ValueError("LLM response did not contain safe firmware file paths")
            project = GeneratedFirmwareProject(files=files, assumptions=["LLM-generated output requires independent review and toolchain validation."])
            metadata = {"submodule": self.detect_submodule(request, target), "dependencies": llm_result.get("dependencies", []), "hardware_id": extract_hardware_identifier(request.description)}
        else:
            project = self.generator.generate(target, spec)
            metadata = {"submodule": self.detect_submodule(request, target), "dependencies": self.infer_dependencies(project.files), "hardware_id": extract_hardware_identifier(request.description)}
        generated_files = [FirmwareFile(path=path, content=content, language=self.language_for(path), kind=self.kind_for(path), line_count=content.count("\n")) for path, content in project.files.items()]
        if conversation_key:
            self.context[conversation_key] = generated_files[-12:]
        return project, {**metadata, "files": generated_files, "rendering_hint": "firmware-code", "confidence_score": 0.92 if target else 0.45}

    @staticmethod
    def _safe_path(path: str) -> bool:
        return bool(path and not path.startswith("/") and ".." not in path.split("/") and re.fullmatch(r"[A-Za-z0-9_./+-]+", path))

    @staticmethod
    def language_for(path: str) -> str:
        suffix = path.rsplit(".", 1)[-1].lower() if "." in path else "text"
        return {"c": "c", "h": "c", "cc": "cpp", "cpp": "cpp", "hpp": "cpp", "rs": "rust", "py": "python", "v": "verilog", "sv": "systemverilog", "vhd": "vhdl", "vhdl": "vhdl", "s": "assembly", "S": "assembly", "ld": "linker", "toml": "toml", "cmake": "cmake", "mk": "makefile"}.get(suffix, "text")

    @staticmethod
    def kind_for(path: str) -> str:
        return "documentation" if path.lower().endswith((".md", ".txt")) else "build" if path.lower().endswith((".cmake", ".mk", "makefile", ".toml", ".ld")) else "test" if "test" in path.lower() else "source"

    @staticmethod
    def infer_dependencies(files: dict[str, str]) -> list[str]:
        text = "\n".join(files.values()).lower()
        candidates = {
            "Arduino.h": "arduino", "freertos": "freertos", "zephyr": "zephyr", "embedded-hal": "embedded-hal", "cortex_m": "cortex-m", "machine": "micropython", "px4": "px4", "hal": "vendor-hal",
        }
        return sorted(value for key, value in candidates.items() if key.lower() in text)

    async def process(self, *args: Any, **kwargs: Any) -> dict[str, Any]:
        request = kwargs.get("request") or (args[0] if args else None)
        target = kwargs.get("target")
        if not isinstance(request, FirmwareRequest) or not isinstance(target, FirmwareTarget):
            raise TypeError("FirmwareEngine.process requires a FirmwareRequest and FirmwareTarget")
        spec = FirmwareSpec(target_id=target.id, description=request.description, project_name=request.project_name, language=request.language or target.language, framework=request.framework or target.framework, features=request.features, pins=request.pins, peripherals=request.peripherals, include_tests=request.include_tests, safety_mode=request.safety_mode, previous_code=request.previous_code, requested_files=request.requested_files)
        project, metadata = await self.generate(request, target, spec, kwargs.get("conversation_key"))
        return self.format_output({"files": metadata["files"], "assumptions": project.assumptions, **{key: value for key, value in metadata.items() if key != "files"}})
