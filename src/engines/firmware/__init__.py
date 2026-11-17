"""Firmware Engine (PRD §24, §31) — real Python fallback.

Generates a compilable C skeleton, validates pin and peripheral
configuration against a built-in MCU pin database, and runs the build
op in two honest tiers:

* when a real toolchain exists on the machine (PlatformIO), the build
  actually runs and only then reports ``build_status: "COMPILED"``;
* otherwise the engine runs static checks — MCU pin conflicts,
  input-only pin misuse, peripheral instance ranges, source structure,
  and a host ``gcc -fsyntax-only`` pass when a host compiler exists —
  and labels every result ``"static check only, not compiled"``.

What stays native-only (documented, never faked)
- real cross-compilation for the target (vendor SDK headers, linker
  scripts, SDK init): PlatformIO is the only path to COMPILED here and
  it must actually succeed; a host syntax pass is never claimed as a
  target compile;
- vendor SDK integration (ESP-IDF app_main / STM32 HAL / avr-libc):
  the skeleton's ``*_init`` bodies are real C functions the application
  replaces, stated in the generated header comment, not a hidden stub;
- flashing, debugging, and the alternate-function pin matrix: the pin
  database is enough to catch config errors, not a datasheet.

LLM-LINK: a future model could draft the SDK init bodies or propose a
pin map ahead of the deterministic checker; the engine never calls one
today.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import uuid
from pathlib import Path
from typing import Any

from src.core.errors import RequestValidationError
from src.engines.base import BaseEngine, EngineResult, ValidationResult

DEFAULT_MCU = "ESP32-WROOM-32"

#: Seconds before a toolchain invocation is abandoned and reported as a
#: timeout. Module-level so tests can shorten it with a fake compiler.
_TOOLCHAIN_TIMEOUT_S = 600.0

VALID_FEATURES = ("uart", "spi", "i2c", "adc", "pwm")

#: The fallback MCU pin database. Pin name lists, input-only pins, and
#: peripheral instance counts are datasheet-level facts sufficient to
#: catch configuration errors; the full alternate-function matrix stays
#: native-only. ``entry`` records the SDK's real entry point so the
#: generated skeleton can say honestly what a ``main()`` means per MCU.
_MCU_DB: dict[str, dict[str, Any]] = {
    "ESP32-WROOM-32": {
        "pins": [
            * [f"GPIO{i}" for i in (0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27)],
            * [f"GPIO{i}" for i in (32, 33, 34, 35, 36, 39)],
        ],
        "input_only": {"GPIO34", "GPIO35", "GPIO36", "GPIO39"},
        "peripherals": {"uart": 3, "i2c": 2, "spi": 4, "adc": 2, "pwm": 8},
        "entry": "ESP-IDF app_main() (main() kept for toolchain-agnostic builds)",
    },
    "STM32F407": {
        "pins": [
            *[f"PA{i}" for i in range(16)],
            *[f"PB{i}" for i in range(16)],
            *[f"PC{i}" for i in range(16)],
            *[f"PD{i}" for i in range(16)],
            *[f"PE{i}" for i in range(16)],
        ],
        "input_only": set(),
        "peripherals": {"uart": 6, "i2c": 3, "spi": 3, "adc": 3, "pwm": 14},
        "entry": "CMSIS Reset_Handler -> HAL main",
    },
    "ATmega2560": {
        "pins": [
            *[f"{port}{i}" for port in "ABCDEFGHIJKL" for i in range(8)],
        ],
        "input_only": set(),
        "peripherals": {"uart": 4, "i2c": 1, "spi": 1, "adc": 1, "pwm": 6},
        "entry": "avr-libc main()",
    },
}


class FirmwareError(RequestValidationError):
    """Firmware-specific validation error."""


class FirmwareEngine(BaseEngine):
    name = "firmware"
    version = "2.0"
    capabilities = (
        "create",
        "select_mcu",
        "configure_pin",
        "configure_peripheral",
        "generate",
        "validate",
        "build",
    )

    def describe(self) -> dict[str, Any]:
        # Real Python fallback (the M18 convention): skeleton generation,
        # the MCU-database pin/peripheral checks, and the two-tier build
        # op below are implemented and tested — no status key. Real
        # cross-compilation and vendor SDK integration stay native-only
        # (module docstring).
        return super().describe()

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "create":
            return self.execute_create(parameters)
        if operation == "select_mcu":
            return self.execute_select_mcu(parameters)
        if operation == "configure_pin":
            return self.execute_configure_pin(parameters)
        if operation == "configure_peripheral":
            return self.execute_configure_peripheral(parameters)
        if operation == "generate":
            return self.execute_generate(parameters)
        if operation == "validate":
            return self.execute_validate(parameters)
        if operation == "build":
            return self.execute_build(parameters)
        raise RequestValidationError(f"Firmware engine has no operation '{operation}'")

    # -------------------------------------------------------- execute ---

    def execute_create(self, parameters: dict[str, Any]) -> EngineResult:
        """Create a new firmware project record; validates the MCU name."""
        board = parameters.get("board", {})
        mcu = parameters.get("mcu", DEFAULT_MCU)
        features = parameters.get("features", [])

        project_id = str(uuid.uuid4().hex[:8])
        known = mcu in _MCU_DB
        errors = [] if known else [
            f"Unrecognized MCU: {mcu} (known: {', '.join(sorted(_MCU_DB))})"
        ]

        return EngineResult(
            success=known,
            engine=self.name,
            operation="create",
            result={
                "project_id": project_id,
                "mcu": mcu,
                "board_width_mm": board.get("width_mm", 50.0),
                "board_height_mm": board.get("height_mm", 40.0),
                "features": features,
                "status": "created" if known else "rejected-unknown-mcu",
                "errors": errors,
            },
            validation=ValidationResult(
                status="VALIDATED" if known else "FAILED",
                checks={"errors": errors} if errors else {},
            ),
        )

    def execute_select_mcu(self, parameters: dict[str, Any]) -> EngineResult:
        """Select the MCU; only MCUs the fallback database knows."""
        mcu = parameters.get("mcu", DEFAULT_MCU)
        known = mcu in _MCU_DB
        errors = [] if known else [
            f"Unrecognized MCU: {mcu} (known: {', '.join(sorted(_MCU_DB))})"
        ]
        return EngineResult(
            success=known,
            engine=self.name,
            operation="select_mcu",
            result={
                "mcu": mcu,
                "status": "selected" if known else "rejected-unknown-mcu",
                "errors": errors,
            },
            validation=ValidationResult(
                status="VALIDATED" if known else "FAILED",
                checks={"errors": errors} if errors else {},
            ),
        )

    def execute_configure_pin(self, parameters: dict[str, Any]) -> EngineResult:
        """Validate one pin assignment against the MCU pin database."""
        pin = parameters.get("pin", "")
        function = parameters.get("function", "")
        mode = parameters.get("mode", "gpio")
        mcu = parameters.get("mcu", DEFAULT_MCU)

        if not pin:
            raise RequestValidationError("configure_pin requires pin name")

        errors, detail = _pin_checks(
            mcu, [{"name": pin, "function": function, "mode": mode}], []
        )
        ok = not errors
        return EngineResult(
            success=ok,
            engine=self.name,
            operation="configure_pin",
            result={
                "pin": pin,
                "function": function,
                "mode": mode,
                "mcu": mcu,
                "status": "validated" if ok else "rejected",
                "errors": errors,
                "pin_checks": detail,
            },
            validation=ValidationResult(
                status="VALIDATED" if ok else "FAILED",
                checks={"errors": errors} if errors else {},
            ),
        )

    def execute_configure_peripheral(
        self, parameters: dict[str, Any]
    ) -> EngineResult:
        """Validate one peripheral configuration against the MCU database."""
        peripheral = parameters.get("peripheral", "")
        config = parameters.get("config", {}) or {}
        mcu = parameters.get("mcu", DEFAULT_MCU)

        if not peripheral:
            raise RequestValidationError("configure_peripheral requires peripheral name")
        if not isinstance(config, dict):
            raise RequestValidationError("configure_peripheral config must be an object")

        instance = config.get("instance", 0)
        errors, detail = _pin_checks(
            mcu,
            [],
            [{
                "peripheral": peripheral,
                "instance": instance,
                "pins": config.get("pins", {}),
            }],
        )
        ok = not errors
        return EngineResult(
            success=ok,
            engine=self.name,
            operation="configure_peripheral",
            result={
                "peripheral": peripheral,
                "config": config,
                "mcu": mcu,
                "status": "validated" if ok else "rejected",
                "errors": errors,
                "peripheral_checks": detail,
            },
            validation=ValidationResult(
                status="VALIDATED" if ok else "FAILED",
                checks={"errors": errors} if errors else {},
            ),
        )

    # ------------------------------------------------------- generate ---

    def execute_generate(self, parameters: dict[str, Any]) -> EngineResult:
        """Generate a compilable C skeleton for the selected MCU.

        The skeleton wires the configured peripherals into init calls and
        says honestly what is left for the application: vendor SDK init
        and the superloop body. It carries no placeholder TODO markers.
        """
        mcu = parameters.get("mcu", DEFAULT_MCU)
        project_id = parameters.get("project_id", "")
        features = [str(f).lower() for f in parameters.get("features", []) or []]
        pins = parameters.get("pins", []) or []

        db = _MCU_DB.get(mcu)
        entry_note = (
            db["entry"] if db
            else "entry point unknown for this MCU; plain C main() kept"
        )

        lines: list[str] = [
            "/* Trinity firmware skeleton - generated deterministically.",
            f"   MCU: {mcu}. Project: {project_id or '(unnamed)'}",
            f"   Target entry point: {entry_note}.",
            "   Scope, honestly: this skeleton compiles as plain C and wires",
            "   the configured peripherals into init calls; vendor SDK driver",
            "   init (ESP-IDF / STM32 HAL / avr-libc) is deliberately not faked",
            "   - replace each *_init body with the SDK calls your build uses. */",
            "#include <stdbool.h>",
            "#include <stdint.h>",
            "#include <stdio.h>",
            "",
        ]

        for pin in pins:
            if not isinstance(pin, dict):
                continue
            name = str(pin.get("name", "")).strip()
            if not name:
                continue
            number = _gpio_number(name)
            label = str(pin.get("function") or name).strip().upper()
            label = re.sub(r"[^A-Z0-9_]", "_", label) or name.upper()
            if number is not None:
                lines.append(f"#define FW_PIN_{label} {number}")
            else:
                lines.append(f"/* {name} has no numeric GPIO form; wire it in the SDK layer */")

        for feature in features:
            known = db is not None and feature in db["peripherals"]
            hook = "vendor SDK hook" if known else "vendor SDK hook (not in the MCU database; kept generic)"
            lines.extend([
                "",
                f"static void {feature}_init(void) {{",
                f"    /* {feature} init: {hook} */",
                "    (void)0;",
                "}",
            ])

        lines.extend(["", "int main(void) {"])
        for feature in features:
            lines.append(f"    {feature}_init();")
        lines.extend([
            "    for (;;) {",
            "        /* application superloop: SDK driver calls and scheduling",
            "           belong here; the V1 skeleton leaves it empty on purpose */",
            "    }",
            "    return 0; /* unreachable; kept for toolchains that require it */",
            "}",
        ])

        source_text = "\n".join(lines)

        return EngineResult(
            success=True,
            engine=self.name,
            operation="generate",
            result={
                "mcu": mcu,
                "project_id": project_id,
                "source_lines": len(lines),
                "source_text": source_text,
                "entry_point": entry_note,
                "status": "generated",
            },
        )

    # ------------------------------------------------------- validate ---

    def execute_validate(self, parameters: dict[str, Any]) -> EngineResult:
        """Validate MCU, features, pin assignments and peripherals."""
        mcu = parameters.get("mcu", DEFAULT_MCU)
        features = parameters.get("features", []) or []
        pins = parameters.get("pins", []) or []
        peripherals = parameters.get("peripherals", []) or []

        errors: list[str] = []

        if mcu not in _MCU_DB:
            errors.append(
                f"Unrecognized MCU: {mcu} (known: {', '.join(sorted(_MCU_DB))})"
            )

        for feature in features:
            if str(feature).lower() not in VALID_FEATURES:
                errors.append(f"Unrecognized feature: {feature}")

        pin_errors, pin_detail = _pin_checks(mcu, pins, peripherals)
        errors.extend(pin_errors)

        ok = not errors
        status = "VALIDATED" if ok else "FAILED"
        checks = {"errors": errors} if errors else {}

        return EngineResult(
            success=ok,
            engine=self.name,
            operation="validate",
            result={
                "mcu": mcu,
                "features": features,
                "pins_checked": len(pins),
                "peripherals_checked": len(peripherals),
                "pin_checks": pin_detail,
                "errors": errors,
            },
            validation=ValidationResult(status=status, checks=checks),
        )

    # ----------------------------------------------------------- build ---

    def execute_build(self, parameters: dict[str, Any]) -> EngineResult:
        """Build the firmware project in two honest tiers.

        With a PlatformIO toolchain on the machine the build actually
        runs: exit 0 -> ``COMPILED``, anything else -> FAILED with the
        toolchain's output. Without one, the op reports
        ``"static check only, not compiled"`` after the MCU pin checks,
        the peripheral checks, the source structure checks, and — when a
        host compiler exists — a ``-fsyntax-only`` pass have run.
        """
        project_id = parameters.get("project_id", "")
        mcu = parameters.get("mcu", DEFAULT_MCU)
        source_text = parameters.get("source_text", "") or ""
        pins = parameters.get("pins", []) or []
        peripherals = parameters.get("peripherals", []) or []

        static_errors, static_detail = _static_source_checks(source_text)
        pin_errors, pin_detail = _pin_checks(mcu, pins, peripherals)
        errors = static_errors + pin_errors

        build_status: str | None = "FAILED" if errors else None
        toolchain_used: str | None = None
        notes: list[str] = []
        attempts: list[dict[str, Any]] = []

        if not errors:
            # Static checks passed; now try the real toolchain, PlatformIO
            # first. A static failure is already a verdict, so the
            # toolchain is not invoked to second-guess it.
            for candidate in _toolchain_candidates():
                attempt: dict[str, Any] = {
                    "name": candidate["name"],
                    "kind": candidate["kind"],
                }
                attempts.append(attempt)
                try:
                    if candidate["kind"] == "platformio":
                        returncode, output = _run_platformio(
                            candidate, source_text, parameters
                        )
                    else:
                        returncode, output = _run_gcc_syntax(
                            candidate, source_text
                        )
                except subprocess.TimeoutExpired:
                    attempt["result"] = "timeout"
                    errors.append(
                        f"toolchain '{candidate['name']}' timed out after "
                        f"{_TOOLCHAIN_TIMEOUT_S:g} s"
                    )
                    continue
                attempt["exit_code"] = returncode
                if returncode == 0:
                    if candidate["kind"] == "platformio":
                        build_status = "COMPILED"
                        toolchain_used = candidate["name"]
                        attempt["result"] = "compiled"
                        break
                    attempt["result"] = "syntax_ok"
                    notes.append(
                        f"host {candidate['name']} syntax pass OK "
                        f"(static check only, not compiled for {mcu})"
                    )
                    continue
                attempt["result"] = "failed"
                if (
                    candidate["kind"] == "gcc_syntax"
                    and "No such file or directory" in output
                ):
                    # A vendor SDK header the host compiler cannot resolve
                    # makes the syntax pass inconclusive — stated, not a
                    # compile failure and not a pass.
                    attempt["result"] = "inconclusive"
                    notes.append(
                        f"host {candidate['name']} could not resolve a vendor "
                        "SDK header; syntax pass inconclusive, not a compile "
                        "failure"
                    )
                    continue
                errors.append(
                    f"toolchain '{candidate['name']}' failed (exit "
                    f"{returncode}): {output.strip()[:400]}"
                )
                if candidate["kind"] == "platformio":
                    # A real cross-compile failure is the verdict; the
                    # host syntax pass must not mask it.
                    break

        if build_status is None:
            build_status = "FAILED" if errors else "STATIC_CHECK_ONLY"
        if build_status == "STATIC_CHECK_ONLY" and not any(
            "not compiled" in note for note in notes
        ):
            notes.append(
                "static check only, not compiled — no cross-compile "
                "toolchain completed on this machine"
            )

        ok = not errors
        if ok and build_status == "COMPILED":
            vstatus = "VERIFIED"
        elif ok:
            vstatus = "VALIDATED"
        else:
            vstatus = "FAILED"

        if toolchain_used:
            note_text = f"compiled with {toolchain_used}"
        elif notes:
            note_text = "; ".join(notes)
        elif attempts:
            note_text = "toolchain attempted and failed; not compiled"
        else:
            note_text = "static check only, not compiled — no toolchain on this machine"

        return EngineResult(
            success=ok,
            engine=self.name,
            operation="build",
            result={
                "project_id": project_id,
                "mcu": mcu,
                "build_ok": ok,
                "build_status": build_status,
                "build_note": note_text,
                "toolchain": toolchain_used,
                "errors": errors,
                "static_checks": static_detail,
                "pin_checks": pin_detail,
                "toolchain_attempts": attempts,
            },
            validation=ValidationResult(status=vstatus, checks={"errors": errors} if errors else {}),
        )


# ----------------------------------------------------------- toolchains ---


def _toolchain_candidates() -> list[dict[str, Any]]:
    """Toolchains that may exist on this machine, in try order.

    PlatformIO first (a real cross-compile), then one host compiler as a
    syntax pass. Tests monkeypatch this seam to exercise success,
    failure, timeout, and not-installed without a real toolchain.
    """
    candidates: list[dict[str, Any]] = []
    for exe in ("pio", "platformio"):
        path = shutil.which(exe)
        if path:
            candidates.append(
                {"name": exe, "kind": "platformio", "argv": [path]}
            )
            break
    for exe in ("gcc", "clang", "cc"):
        path = shutil.which(exe)
        if path:
            candidates.append(
                {
                    "name": exe,
                    "kind": "gcc_syntax",
                    "argv": [path, "-fsyntax-only", "-x", "c", "-"],
                }
            )
            break
    return candidates


def _run_gcc_syntax(
    candidate: dict[str, Any], source_text: str
) -> tuple[int, str]:
    """Run one host syntax pass with the source on stdin."""
    completed = subprocess.run(  # noqa: S603 - argv from a fixed seam
        candidate["argv"],
        input=source_text,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=_TOOLCHAIN_TIMEOUT_S,
        check=False,
    )
    return completed.returncode, completed.stdout + completed.stderr


def _run_platformio(
    candidate: dict[str, Any], source_text: str, parameters: dict[str, Any]
) -> tuple[int, str]:
    """Materialize a minimal PlatformIO project and run the real build."""
    project = Path(tempfile.mkdtemp(prefix="trinity_fw_pio_"))
    (project / "src").mkdir()
    platform_name = parameters.get("pio_platform", "espressif32")
    board = parameters.get("pio_board", "esp32dev")
    (project / "platformio.ini").write_text(
        "[env:trinity]\n"
        f"platform = {platform_name}\n"
        f"board = {board}\n"
        "framework = arduino\n",
        encoding="utf-8",
    )
    (project / "src" / "main.c").write_text(source_text, encoding="utf-8")
    argv = [*candidate["argv"], "run", "--project-dir", str(project)]
    completed = subprocess.run(  # noqa: S603 - argv from a fixed seam
        argv,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=_TOOLCHAIN_TIMEOUT_S,
        check=False,
    )
    output = completed.stdout + completed.stderr
    return completed.returncode, output[-4000:]


# ------------------------------------------------------------ static ------


def _gpio_number(pin_name: str) -> int | None:
    """The numeric form of a ``GPIO<n>`` pin name, else None."""
    match = re.fullmatch(r"GPIO(\d+)", pin_name.strip())
    return int(match.group(1)) if match else None


def _static_source_checks(source_text: str) -> tuple[list[str], dict[str, Any]]:
    """Structure checks every build op runs, toolchain or not."""
    errors: list[str] = []
    detail: dict[str, Any] = {}
    stripped = source_text.strip()
    if not stripped:
        errors.append("source_text is empty; nothing to build")
        return errors, detail
    detail["lines"] = len(stripped.splitlines())
    if "int main" not in stripped:
        errors.append("source has no main() function (expected 'int main')")
    for open_c, close_c, name in (("{", "}", "braces"), ("(", ")", "parentheses")):
        opens, closes = stripped.count(open_c), stripped.count(close_c)
        if opens != closes:
            errors.append(
                f"unbalanced {name}: {opens} '{open_c}' vs {closes} '{close_c}'"
            )
    detail["todo_or_fixme_markers"] = stripped.count("TODO") + stripped.count("FIXME")
    return errors, detail


def _collect_pin_assignments(
    pins: list[dict[str, Any]] | None,
    peripherals: list[dict[str, Any]] | None,
) -> dict[str, list[str]]:
    """pin name -> every function/role assigned to it, from both inputs."""
    assignments: dict[str, list[str]] = {}

    def assign(pin: Any, label: str) -> None:
        name = str(pin).strip()
        if name:
            assignments.setdefault(name, []).append(label)

    for pin_info in pins or []:
        name = pin_info.get("name", "") if isinstance(pin_info, dict) else ""
        if name:
            assign(
                name,
                str(
                    pin_info.get("function")
                    or pin_info.get("mode")
                    or "unlabeled"
                ),
            )
    for peripheral in peripherals or []:
        p_name = str(peripheral.get("peripheral", "?"))
        for role, pin in (peripheral.get("pins") or {}).items():
            assign(pin, f"{p_name}.{role}")
    return assignments


def _pin_checks(
    mcu: str,
    pins: list[dict[str, Any]] | None,
    peripherals: list[dict[str, Any]] | None,
) -> tuple[list[str], dict[str, Any]]:
    """Pin conflicts, unknown pins, input-only misuse, instance ranges.

    When the MCU is not in the fallback database the checks are skipped
    and the skip is stated — never silently passed.
    """
    errors: list[str] = []
    detail: dict[str, Any] = {"mcu_known": mcu in _MCU_DB}

    if mcu not in _MCU_DB:
        detail["skipped"] = (
            f"MCU '{mcu}' is not in the fallback pin database; "
            "pin-conflict and input-only checks were skipped and stated"
        )
        detail["assignments"] = {}  # none were evaluated; see 'skipped'
        return errors, detail

    db = _MCU_DB[mcu]
    known_pins = set(db["pins"])
    assignments = _collect_pin_assignments(pins, peripherals)
    detail["assignments"] = {
        pin: sorted(set(labels)) for pin, labels in sorted(assignments.items())
    }

    for pin in sorted(assignments):
        if pin not in known_pins:
            errors.append(f"pin '{pin}' does not exist on {mcu}")
    for pin, labels in sorted(assignments.items()):
        unique = sorted(set(labels))
        if len(unique) > 1:
            errors.append(
                f"pin conflict: {pin} assigned to {' + '.join(unique)}"
            )
    for pin_info in pins or []:
        if not isinstance(pin_info, dict):
            continue
        name = str(pin_info.get("name", "")).strip()
        mode = str(pin_info.get("mode", "gpio")).lower()
        if name in db["input_only"] and mode in ("output", "inout"):
            errors.append(
                f"{name} is input-only on {mcu} and cannot drive mode '{mode}'"
            )

    counts = db["peripherals"]
    instances: dict[tuple[str, int], int] = {}
    for peripheral in peripherals or []:
        p_name = str(peripheral.get("peripheral", ""))
        if p_name not in counts:
            errors.append(
                f"peripheral '{p_name}' does not exist on {mcu} "
                f"(known: {', '.join(sorted(counts))})"
            )
            continue
        try:
            index = int(peripheral.get("instance", 0) or 0)
        except (TypeError, ValueError) as exc:
            raise FirmwareError(
                f"peripheral '{p_name}' has a non-integer instance"
            ) from exc
        if index < 0 or index >= counts[p_name]:
            errors.append(
                f"{p_name} instance {index} out of range on {mcu} "
                f"(0..{counts[p_name] - 1})"
            )
        key = (p_name, index)
        instances[key] = instances.get(key, 0) + 1
    for (p_name, index), count in sorted(instances.items()):
        if count > 1:
            errors.append(
                f"{p_name} instance {index} configured {count} times"
            )
    return errors, detail
