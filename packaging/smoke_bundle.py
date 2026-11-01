"""End-to-end smoke check for a packaged Trinity distribution.

Run against a distribution directory:
  windows: native/ + api/ + web/  (Trinity.exe daemon included)
  linux:   api/ + web/            (M16 - no native daemon on this bundle)

Starts the bundled trinity-api binary with the layout's environment, then
asserts, in order:
  1. /api/health answers (the process is genuinely up)
  2. / serves the static UI
  3. /chat resolves through the .html retry
  4. an engine execution answers -- through the bundled native bridge on
     Windows, through the Python math engine on Linux
  5. (linux) a native-backed engine refuses with the honest
     capability_unavailable reason inside the frozen bundle -- absence
     must be stated, never papered over
  6. the OpenCascade kernel works IN THE BUNDLE: the catalog reports
     step as available and a real STEP export produces OCCT stats
     (M11 - the bundle without a working kernel fails the smoke)
  7. the updater route answers with the availability shape (M13 -
     offline is fine: available:false with a reason)

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

REQUIRED_DIRS_BY_PLATFORM = {
    "windows": ("native", "api", "web"),
    "linux": ("api", "web"),
}


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
    parser.add_argument(
        "--platform", choices=sorted(REQUIRED_DIRS_BY_PLATFORM), default="windows",
        help="which bundle layout this is (default: windows, unchanged)")
    args = parser.parse_args()

    dist = Path(args.dist).resolve()
    required = REQUIRED_DIRS_BY_PLATFORM[args.platform]
    missing = [d for d in required if not (dist / d).is_dir()]
    if missing:
        print(f"FAIL: distribution missing: {missing}", file=sys.stderr)
        return 1
    api_name = "trinity-api.exe" if args.platform == "windows" else "trinity-api"
    api_bin = dist / "api" / api_name
    if not api_bin.is_file():
        print(f"FAIL: {api_bin} not found", file=sys.stderr)
        return 1

    env = os.environ.copy()
    env["TRINITY_API_PORT"] = str(args.port)
    env["TRINITY_UI_DIR"] = str(dist / "web")
    env["TRINITY_STORAGE_ROOT"] = str(dist.parent / "smoke-data" / dist.name)
    # User data lives NEXT TO nothing the installer owns: keep the
    # package read-only by putting storage in the smoke scratch space
    # (the real launcher uses %LOCALAPPDATA% on Windows and
    # ~/.local/share on Linux).
    if args.platform == "windows":
        env["TRINITY_NATIVE_EXEC"] = str(dist / "native" / "Trinity.exe")
        env["TRINITY_QT_BIN"] = str(dist / "native")
    # Linux: deliberately no TRINITY_NATIVE_EXEC -- the bridge must keep
    # the six native-backed engines on ScaffoldEngine and refuse them
    # honestly; that refusal is what step 5 below proves.

    proc = subprocess.Popen(
        [str(api_bin)],
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

        if args.platform == "windows":
            body = json.dumps({
                "engine": "research", "operation": "search",
                "parameters": {"query": "package smoke probe"},
            }).encode()
        else:
            # Linux: the probe answers through the Python math engine —
            # the native bridge is absent from this bundle by design.
            body = json.dumps({
                "engine": "math", "operation": "evaluate",
                "parameters": {"expression": "2+3"},
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

        # --- M16 (linux): absence must be stated, never papered over ----
        # Inside the frozen bundle, a native-backed engine answers with
        # the scaffold's honest capability_unavailable refusal.
        if args.platform == "linux":
            body = json.dumps({
                "engine": "pcb", "operation": "create",
                "parameters": {"spec": "smoke probe"},
            }).encode()
            req = urllib.request.Request(
                f"{base}/api/execute", data=body,
                headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=30) as resp:
                out = json.loads(resp.read())
            codes = [e.get("code") for e in (out.get("errors") or [])]
            if out.get("success") or "capability_unavailable" not in codes:
                print(f"FAIL: linux bundle must refuse native-backed "
                      f"engines honestly: {out}", file=sys.stderr)
                return 1
            findings.append("native-backed refusal honest")

        # --- M11: the bundled OpenCascade kernel must genuinely work ------
        # The catalog is pure registry read; `step` flips to available only
        # when OCP imports inside the frozen bundle.
        with urllib.request.urlopen(f"{base}/api/cad/catalog", timeout=30) as resp:
            catalog = json.loads(resp.read())
        step_avail = (catalog.get("outputs") or {}).get("step", {})
        if not step_avail.get("available"):
            print(f"FAIL: catalog does not report step available "
                  f"(kernel missing in bundle): {step_avail}", file=sys.stderr)
            return 1
        findings.append("kernel catalog ok")

        body = json.dumps({
            "engine": "cad", "operation": "generate",
            "parameters": {"outputs": ["step"]},
        }).encode()
        req = urllib.request.Request(
            f"{base}/api/execute", data=body,
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=180) as resp:
            out = json.loads(resp.read())
        step_stats = (out.get("result") or {}).get("step") or {}
        if resp.status != 200 or not out.get("success") or not step_stats.get("volume_mm3"):
            print(f"FAIL: bundled STEP export refused or empty: "
                  f"{out.get('errors') or step_stats}", file=sys.stderr)
            return 1
        findings.append(
            "step export ok (volume_mm3={:.1f}, kernel={})".format(
                step_stats["volume_mm3"], step_stats.get("kernel", "?")))

        # --- M13: the updater route must answer, and honestly -----------
        # Offline runner: 'available': False with a reason is fine - the
        # route must EXIST and respond with the availability shape.
        code, body = _get(f"{base}/api/update/check", timeout=30)
        if code != 200 or '"available"' not in body:
            print(f"FAIL: /api/update/check answered {code}: "
                  f"{body[:120]}", file=sys.stderr)
            return 1
        findings.append("update route ok")
    finally:
        proc.terminate()
        proc.wait(timeout=10)

    print("SMOKE OK:", "; ".join(findings))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
