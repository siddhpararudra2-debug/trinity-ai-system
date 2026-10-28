"""End-to-end smoke check for a packaged Trinity distribution.

Run against a distribution directory (native/ + api/ + web/):
starts trinity-api.exe with the bundled layout's environment, then
asserts, in order:
  1. /api/health answers (the process is genuinely up)
  2. / serves the static UI
  3. /chat resolves through the .html retry
  4. an engine execution answers through the bundled native bridge

Exit code 0 only when every step really answered -- the same rule the
desktop startup probe follows.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

REQUIRED_DIRS = ("native", "api", "web")


def _get(url: str, timeout: float = 5.0) -> tuple[int, str]:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dist", help="path to the assembled distribution directory")
    parser.add_argument("--port", type=int, default=8241)
    args = parser.parse_args()

    dist = Path(args.dist).resolve()
    missing = [d for d in REQUIRED_DIRS if not (dist / d).is_dir()]
    if missing:
        print(f"FAIL: distribution missing: {missing}", file=sys.stderr)
        return 1
    api_exe = dist / "api" / "trinity-api.exe"
    if not api_exe.is_file():
        print(f"FAIL: {api_exe} not found", file=sys.stderr)
        return 1

    env = os.environ.copy()
    env["TRINITY_API_PORT"] = str(args.port)
    env["TRINITY_UI_DIR"] = str(dist / "web")
    env["TRINITY_NATIVE_EXEC"] = str(dist / "native" / "Trinity.exe")
    env["TRINITY_QT_BIN"] = str(dist / "native")
    # User data lives NEXT TO nothing the installer owns: keep the
    # package read-only by putting storage in the smoke scratch space
    # (the real launcher uses %LOCALAPPDATA%).
    env["TRINITY_STORAGE_ROOT"] = str(dist.parent / "smoke-data" / dist.name)

    proc = subprocess.Popen(
        [str(api_exe)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
        env=env,
    )
    findings: list[str] = []
    try:
        base = f"http://127.0.0.1:{args.port}"
        healthy = False
        for _ in range(90):
            time.sleep(0.5)
            try:
                body = json.loads(urllib.request.urlopen(
                    f"{base}/api/health", timeout=2).read())
                healthy = body.get("status") == "ok"
                break
            except Exception:  # noqa: BLE001 - polling until responsive
                continue
        if not healthy:
            print("FAIL: /api/health never answered", file=sys.stderr)
            return 1
        findings.append("health ok")

        code, page = _get(f"{base}/")
        if code != 200 or "Trinity" not in page:
            print(f"FAIL: / served {code} ({len(page)} bytes)", file=sys.stderr)
            return 1
        findings.append("ui served")

        code, _ = _get(f"{base}/chat")
        if code != 200:
            print(f"FAIL: /chat resolved to {code}", file=sys.stderr)
            return 1
        findings.append("chat route ok")

        body = json.dumps({
            "engine": "research", "operation": "search",
            "parameters": {"query": "package smoke probe"},
        }).encode()
        req = urllib.request.Request(
            f"{base}/api/execute", data=body,
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=60) as resp:
            out = json.loads(resp.read())
        if resp.status != 200 or not out.get("success"):
            print(f"FAIL: engine probe refused: {out.get('errors')}", file=sys.stderr)
            return 1
        findings.append("engine probe ok")
    finally:
        proc.terminate()
        proc.wait(timeout=10)

    print("SMOKE OK:", "; ".join(findings))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
