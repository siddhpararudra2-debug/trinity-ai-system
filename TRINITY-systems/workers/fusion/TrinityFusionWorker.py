"""Trinity Fusion 360 add-in worker.

Install this file as a Fusion 360 Python script/add-in on a trusted Windows
machine. It intentionally requires an operator to enter a CAD job ID and uses
an allowlisted generated script hash from the Trinity API.
"""
from __future__ import annotations

import ast
import builtins
import hashlib
import json
import os
import pathlib
import tempfile
import traceback
import urllib.error
import urllib.request
import uuid

try:
    import adsk.core
    import adsk.fusion
except ImportError:  # Allows syntax checks on non-Fusion machines.
    adsk = None

ADDIN_VERSION = "1.0.0"
API_BASE = os.getenv("TRINITY_API_BASE", "http://127.0.0.1:8000").rstrip("/")
WORKER_ID = os.getenv("TRINITY_FUSION_WORKER_ID", "fusion-desktop-01")
WORKER_SECRET = os.getenv("TRINITY_FUSION_WORKER_SECRET", "")
API_KEY = os.getenv("TRINITY_API_KEY", "")
ALLOWED_SCRIPT_IMPORTS = {"adsk.core", "adsk.fusion", "traceback", "math"}
ALLOWED_EXPORTS = {"step", "stl", "f3d"}


def _url(path: str) -> str:
    return f"{API_BASE}{path}" if path.startswith("/") else f"{API_BASE}/{path}"


def _json_request(method: str, path: str, payload: dict, token: str | None = None) -> dict:
    if len(WORKER_SECRET) < 32:
        raise RuntimeError("TRINITY_FUSION_WORKER_SECRET must contain at least 32 characters")
    body = json.dumps(payload).encode("utf-8")
    headers = {"Content-Type": "application/json", "X-Trinity-Worker-Secret": WORKER_SECRET}
    if API_KEY:
        headers["X-Trinity-Api-Key"] = API_KEY
    if token:
        headers["X-Trinity-Worker-Token"] = token
    request = urllib.request.Request(_url(path), data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Trinity API {exc.code}: {detail}") from exc


def _download_script(script_info: dict, job_id: str) -> str:
    url = script_info["download_url"]
    if url.startswith("/"):
        url = _url(url)
    headers = {"X-Trinity-Worker-Secret": WORKER_SECRET}
    if API_KEY:
        headers["X-Trinity-Api-Key"] = API_KEY
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != script_info["sha256"]:
        raise RuntimeError(f"Fusion script hash mismatch for job {job_id}: expected {script_info['sha256']}, got {digest}")
    return data.decode("utf-8")


def _validate_script(script: str) -> None:
    tree = ast.parse(script)
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            names = [alias.name for alias in node.names]
        elif isinstance(node, ast.ImportFrom):
            names = [node.module or ""]
        else:
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in {"eval", "exec", "open", "compile", "__import__"}:
                raise RuntimeError(f"Forbidden generated-script call: {node.func.id}")
            continue
        for name in names:
            if name not in ALLOWED_SCRIPT_IMPORTS:
                raise RuntimeError(f"Generated script imports a module outside the allowlist: {name}")
    if not any(isinstance(node, ast.FunctionDef) and node.name == "run" for node in tree.body):
        raise RuntimeError("Generated Fusion script does not define run(context)")


def _safe_import(name, globals=None, locals=None, fromlist=(), level=0):
    if name not in ALLOWED_SCRIPT_IMPORTS:
        raise ImportError(f"Import blocked by Trinity Fusion worker: {name}")
    return builtins.__import__(name, globals, locals, fromlist, level)


def _execute_script(script: str) -> None:
    _validate_script(script)
    safe_builtins = dict(vars(builtins))
    safe_builtins["__import__"] = _safe_import
    for blocked in ("open", "input", "eval", "exec", "compile", "breakpoint"):
        safe_builtins.pop(blocked, None)
    namespace = {"__name__": "__trinity_generated__", "__builtins__": safe_builtins}
    exec(compile(script, "trinity_generated_fusion.py", "exec"), namespace, namespace)
    run = namespace.get("run")
    if not callable(run):
        raise RuntimeError("Generated Fusion script has no callable run(context)")
    run(None)


def _export(design, output_dir: pathlib.Path, formats: set[str]) -> list[pathlib.Path]:
    root = design.rootComponent
    if getattr(root.bRepBodies, "count", 0) < 1:
        raise RuntimeError("Generated Fusion design contains no solid bodies")
    export_manager = design.exportManager
    outputs: list[pathlib.Path] = []
    name = root.name or "trinity_model"
    if "step" in formats:
        path = output_dir / f"{name}.step"
        export_manager.execute(export_manager.createSTEPExportOptions(str(path)))
        outputs.append(path)
    if "stl" in formats:
        path = output_dir / f"{name}.stl"
        options = export_manager.createSTLExportOptions(root, str(path))
        options.sendToPrintUtility = False
        options.meshRefinement = adsk.fusion.MeshRefinementSettings.MeshRefinementHigh
        export_manager.execute(options)
        outputs.append(path)
    if "f3d" in formats:
        path = output_dir / f"{name}.f3d"
        export_manager.execute(export_manager.createFusionArchiveExportOptions(str(path)))
        outputs.append(path)
    missing = [str(path) for path in outputs if not path.is_file() or path.stat().st_size == 0]
    if missing:
        raise RuntimeError("Fusion export did not create non-empty files: " + ", ".join(missing))
    return outputs


def _multipart_upload(job_id: str, token: str, path: pathlib.Path, kind: str) -> str:
    boundary = "----TrinityFusion" + uuid.uuid4().hex
    data = path.read_bytes()
    parts = [
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"token\"\r\n\r\n{token}\r\n".encode(),
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"kind\"\r\n\r\n{kind}\r\n".encode(),
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{path.name}\"\r\nContent-Type: application/octet-stream\r\n\r\n".encode() + data + b"\r\n",
        f"--{boundary}--\r\n".encode(),
    ]
    headers = {"Content-Type": f"multipart/form-data; boundary={boundary}", "X-Trinity-Worker-Secret": WORKER_SECRET}
    if API_KEY:
        headers["X-Trinity-Api-Key"] = API_KEY
    request = urllib.request.Request(_url(f"/api/fusion/jobs/{job_id}/artifacts"), data=b"".join(parts), headers=headers, method="POST")
    with urllib.request.urlopen(request, timeout=120) as response:
        payload = json.loads(response.read().decode("utf-8"))
    return payload["artifact"]["id"]


def process_job(job_id: str) -> dict:
    claim = _json_request("POST", f"/api/fusion/jobs/{job_id}/claim", {"worker_id": WORKER_ID, "expires_in_seconds": 600})
    script = _download_script(claim["script"], job_id)
    _execute_script(script)
    spec = claim.get("spec") or {}
    requested = set(spec.get("output_formats") or ["step", "stl", "f3d"]) & ALLOWED_EXPORTS
    output_dir = pathlib.Path(tempfile.mkdtemp(prefix=f"trinity_{job_id}_"))
    try:
        design = adsk.core.Application.get().activeProduct
        exported = _export(design, output_dir, requested)
        artifact_ids = []
        for path in exported:
            kind = {".step": "fusion_step", ".stl": "fusion_stl", ".f3d": "fusion_archive"}[path.suffix.lower()]
            artifact_ids.append(_multipart_upload(job_id, claim["token"], path, kind))
        return _json_request("POST", f"/api/fusion/jobs/{job_id}/complete", {"token": claim["token"], "status": "ready", "artifact_ids": artifact_ids, "validation_status": "passed", "fusion_version": str(adsk.core.Application.get().version), "addin_version": ADDIN_VERSION, "checks": [{"name": "solid-body", "status": "passed", "message": "Fusion design contains at least one solid body."}, {"name": "exports", "status": "passed", "message": f"Exported {len(artifact_ids)} requested format(s)."}]})
    except Exception as exc:
        try:
            _json_request("POST", f"/api/fusion/jobs/{job_id}/complete", {"token": claim["token"], "status": "failed", "validation_status": "failed", "error": str(exc), "fusion_version": str(adsk.core.Application.get().version), "addin_version": ADDIN_VERSION})
        finally:
            raise
    finally:
        for path in output_dir.glob("*"):
            try:
                path.unlink()
            except OSError:
                pass
        try:
            output_dir.rmdir()
        except OSError:
            pass


def run(context):
    app = adsk.core.Application.get()
    ui = app.userInterface
    try:
        result, cancelled = ui.inputBox("Enter Trinity CAD job ID", "Trinity Fusion Worker", "cad_...")
        if cancelled or not result.strip():
            return
        response = process_job(result.strip())
        ui.messageBox("Trinity export completed:\n" + json.dumps(response, indent=2))
    except Exception:
        ui.messageBox("Trinity Fusion Worker failed:\n" + traceback.format_exc())


def stop(context):
    pass
