"""KiCad 9 manufacturing worker.

The worker runs only fixed kicad-cli subcommands against a job-scoped project
folder. It never accepts a shell command from a user or job payload.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import uuid
import urllib.request
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


@dataclass
class CommandResult:
    name: str
    status: str
    returncode: int | None
    message: str
    stdout: str = ""
    stderr: str = ""


class KiCadWorker:
    def __init__(self, executable: str | None = None, timeout_seconds: int = 180) -> None:
        self.executable = executable or shutil.which("kicad-cli")
        self.timeout_seconds = timeout_seconds

    def run(self, project_dir: Path, schematic: str, pcb: str, outputs: set[str]) -> dict:
        project_dir = project_dir.expanduser().resolve()
        project_dir.mkdir(parents=True, exist_ok=True)
        schem_path = self._safe_input(project_dir, schematic, ".kicad_sch")
        pcb_path = self._safe_input(project_dir, pcb, ".kicad_pcb")
        checks: list[CommandResult] = []
        generated: list[str] = []
        if not self.executable:
            checks.append(CommandResult("kicad-cli", "skipped", None, "kicad-cli is not installed on this worker."))
            return self._result(project_dir, checks, generated)
        checks.append(self._version())
        if schem_path is None or pcb_path is None:
            checks.append(CommandResult("inputs", "failed", None, "Schematic and PCB files must exist inside the job directory."))
            return self._result(project_dir, checks, generated)

        report_dir = project_dir / "validation"
        report_dir.mkdir(exist_ok=True)
        checks.append(self._command("erc", ["sch", "erc", "--exit-code-violations", "--format", "json", "--output", str(report_dir / "erc.json"), str(schem_path)], project_dir))
        checks.append(self._command("drc", ["pcb", "drc", "--exit-code-violations", "--format", "json", "--output", str(report_dir / "drc.json"), str(pcb_path)], project_dir))
        generated.extend(self._files_under(report_dir))

        if "gerbers" in outputs:
            gerbers = project_dir / "manufacturing" / "gerbers"
            gerbers.mkdir(parents=True, exist_ok=True)
            checks.append(self._command("gerbers", ["pcb", "export", "gerbers", "--output", str(gerbers), str(pcb_path)], project_dir))
            generated.extend(self._files_under(gerbers))
        if "drill" in outputs:
            drill = project_dir / "manufacturing" / "drill"
            drill.mkdir(parents=True, exist_ok=True)
            checks.append(self._command("drill", ["pcb", "export", "drill", "--output", str(drill), str(pcb_path)], project_dir))
            generated.extend(self._files_under(drill))
        if "bom" in outputs:
            bom = project_dir / "manufacturing" / "bom.csv"
            bom.parent.mkdir(parents=True, exist_ok=True)
            checks.append(self._command("bom", ["sch", "export", "bom", "--output", str(bom), str(schem_path)], project_dir))
            generated.extend(self._files_under(bom.parent))
        if "3d_preview" in outputs:
            preview = project_dir / "manufacturing" / "preview.glb"
            preview.parent.mkdir(parents=True, exist_ok=True)
            checks.append(self._command("3d_preview", ["pcb", "export", "glb", "--output", str(preview), str(pcb_path)], project_dir))
            generated.extend(self._files_under(preview.parent))

        return self._result(project_dir, checks, sorted(set(generated)))

    def _version(self) -> CommandResult:
        try:
            result = subprocess.run([self.executable, "version"], capture_output=True, text=True, timeout=15, check=False)
            return CommandResult("version", "passed" if result.returncode == 0 else "failed", result.returncode, result.stdout.strip() or "KiCad version unavailable.", result.stdout[-4000:], result.stderr[-4000:])
        except (OSError, subprocess.TimeoutExpired) as exc:
            return CommandResult("version", "failed", None, f"Could not query KiCad version: {exc}")

    def _command(self, name: str, args: list[str], cwd: Path) -> CommandResult:
        try:
            result = subprocess.run([self.executable, *args], cwd=cwd, capture_output=True, text=True, timeout=self.timeout_seconds, check=False)
            # KiCad uses exit code 5 for rule violations when --exit-code-violations is set.
            status = "passed" if result.returncode == 0 else "failed"
            message = f"{name} completed." if result.returncode == 0 else f"{name} returned exit code {result.returncode}."
            return CommandResult(name, status, result.returncode, message, result.stdout[-8000:], result.stderr[-8000:])
        except subprocess.TimeoutExpired as exc:
            return CommandResult(name, "failed", None, f"{name} timed out after {self.timeout_seconds} seconds: {exc}")
        except OSError as exc:
            return CommandResult(name, "failed", None, f"{name} could not start: {exc}")

    @staticmethod
    def _safe_input(root: Path, value: str, suffix: str) -> Path | None:
        candidate = (root / value).resolve()
        if candidate.parent != root or candidate.suffix != suffix or not candidate.is_file():
            return None
        return candidate

    @staticmethod
    def _files_under(path: Path) -> list[str]:
        if path.is_file():
            return [str(path.relative_to(path.parents[1]))]
        if not path.exists():
            return []
        return [str(item.relative_to(path.parents[1])) for item in path.rglob("*") if item.is_file()]

    @staticmethod
    def _result(project_dir: Path, checks: list[CommandResult], generated: list[str]) -> dict:
        failed = any(check.status == "failed" for check in checks)
        skipped = any(check.status == "skipped" for check in checks)
        result = {
            "status": "failed" if failed else "warnings" if skipped else "passed",
            "checks": [asdict(check) for check in checks],
            "generated_files": sorted(set(generated)),
        }
        (project_dir / "kicad-worker-result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return result


def _multipart_upload(server: str, job_id: str, secret: str, path: Path, kind: str) -> str:
    boundary = "----TrinityKiCad" + uuid.uuid4().hex
    data = path.read_bytes()
    body = b"".join([
        f"--{boundary}\\r\\nContent-Disposition: form-data; name=\\\"kind\\\"\\r\\n\\r\\n{kind}\\r\\n".encode(),
        f"--{boundary}\\r\\nContent-Disposition: form-data; name=\\\"file\\\"; filename=\\\"{path.name}\\\"\\r\\nContent-Type: application/octet-stream\\r\\n\\r\\n".encode() + data + b"\\r\\n",
        f"--{boundary}--\\r\\n".encode(),
    ])
    headers = {"Content-Type": f"multipart/form-data; boundary={boundary}", "X-Trinity-KiCad-Secret": secret}
    api_key = os.getenv("TRINITY_API_KEY", "")
    if api_key:
        headers["X-Trinity-Api-Key"] = api_key
    request = urllib.request.Request(f"{server.rstrip('/')}/api/kicad/jobs/{job_id}/artifacts", data=body, headers=headers, method="POST")
    with urllib.request.urlopen(request, timeout=120) as response:
        return json.loads(response.read().decode("utf-8"))["artifact"]["id"]


def _sync_to_server(server: str, job_id: str, secret: str, project_dir: Path, result: dict) -> dict:
    kinds = {"erc.json": "kicad_erc_report", "drc.json": "kicad_drc_report", "bom.csv": "bom", "preview.glb": "kicad_3d_preview", "kicad-worker-result.json": "kicad_worker_result"}
    artifact_ids = []
    for relative in result.get("generated_files", []):
        path = project_dir / relative
        if not path.is_file():
            continue
        kind = "gerber" if "gerbers" in path.parts else "drill" if "drill" in path.parts else kinds.get(path.name, "kicad_worker_result")
        artifact_ids.append(_multipart_upload(server, job_id, secret, path, kind))
    complete = {
        "status": "ready" if result["status"] == "passed" else "needs_review" if result["status"] == "warnings" else "failed",
        "validation_status": result["status"],
        "artifact_ids": artifact_ids,
        "checks": result.get("checks", []),
    }
    headers = {"Content-Type": "application/json", "X-Trinity-KiCad-Secret": secret}
    api_key = os.getenv("TRINITY_API_KEY", "")
    if api_key:
        headers["X-Trinity-Api-Key"] = api_key
    request = urllib.request.Request(f"{server.rstrip('/')}/api/kicad/jobs/{job_id}/complete", data=json.dumps(complete).encode("utf-8"), headers=headers, method="POST")
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read().decode("utf-8"))


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run safe KiCad ERC/DRC and manufacturing exports")
    parser.add_argument("project_dir", type=Path)
    parser.add_argument("--schematic", default="trinity_board.kicad_sch")
    parser.add_argument("--pcb", default="trinity_board.kicad_pcb")
    parser.add_argument("--outputs", default="gerbers,drill,bom,3d_preview")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--server", help="Trinity API base URL; enables artifact upload")
    parser.add_argument("--job-id", help="Existing PCB design job ID for server synchronization")
    parser.add_argument("--secret", default=os.getenv("TRINITY_KICAD_WORKER_SECRET", ""), help="Worker secret; prefer TRINITY_KICAD_WORKER_SECRET")
    args = parser.parse_args(argv)
    outputs = {item.strip() for item in args.outputs.split(",") if item.strip()}
    result = KiCadWorker(timeout_seconds=args.timeout).run(args.project_dir, args.schematic, args.pcb, outputs)
    if args.server or args.job_id:
        if not args.server or not args.job_id or len(args.secret) < 32:
            parser.error("--server, --job-id, and a 32+ character worker secret are required together")
        result["server_sync"] = _sync_to_server(args.server, args.job_id, args.secret, args.project_dir.resolve(), result)
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "passed" else 2 if result["status"] == "failed" else 3


if __name__ == "__main__":
    sys.exit(main())
