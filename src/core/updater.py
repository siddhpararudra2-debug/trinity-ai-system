"""Self-update for the shipped bundle (M13).

Trust chain: the running API fetches the release's `latest.yml`
manifest, and the manifest's sha256 must match the zip it downloads.
Nothing downloads silently: check_updates and apply_update are separate
calls and apply only *prepares* - Windows cannot replace a running
exe, so the swap happens at the next launch (the launcher applies a
prepared marker before starting the API).

User data is out of scope by construction: TRINITY_STORAGE_ROOT is
owned by the user profile; a program-dir swap never touches it.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import sys
import zipfile
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any
from urllib.request import urlopen, Request

from src.core.config import REPO_ROOT, settings

# Public release endpoints only (no auth on purpose: the updater is a
# desktop client talking to the user's own GitHub traffic). Overridable
# for tests and local release servers.
_REPO_ENV = "TRINITY_UPDATE_REPO"
DEFAULT_REPO = "siddhpararudra2-debug/trinity-ai-system"
UA = {"User-Agent": "TrinityAI-updater"}


@dataclass(frozen=True)
class UpdateInfo:
    """One candidate update, manifest-verified shape."""

    version: str
    published_at: str
    url: str
    sha256: str
    size_bytes: int | None


def installed_version() -> str:
    """The version the RUNNING installation was built from.

    Order: launcher env override, then the packaged stamp
    (config/app_version.txt next to the frozen config), then pyproject
    for repo development runs. Never guesses beyond those.
    """
    env = os.getenv("TRINITY_APP_VERSION", "")
    if env:
        return env
    stamp = REPO_ROOT / "config" / "app_version.txt"
    if stamp.is_file():
        return stamp.read_text(encoding="utf-8").strip() or "0.0.0+unknown"
    pyproject = REPO_ROOT / "pyproject.toml"
    if pyproject.is_file():
        for line in pyproject.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.strip().startswith("version"):
                return line.split('"')[1] if '"' in line else "0.0.0+unknown"
    return "0.0.0+unknown"


def version_newer(candidate: str, current: str) -> bool:
    """Semver-style compare for vN.N.N tags only (no pre-release logic)."""
    def key(v: str) -> tuple[int, ...]:
        m = re.findall(r"\d+", v)
        if not m:
            raise ValueError(f"no numeric version in {v!r}")
        return tuple(int(n) for n in m[:3]) + (0,) * (3 - len(m[:3]))

    try:
        return key(candidate) > key(current)
    except ValueError:
        return False


def _get_text(url: str, timeout: float = 15.0) -> str:
    req = Request(url, headers=UA)
    with urlopen(req, timeout=timeout) as resp:
        return resp.read().decode("utf-8", errors="replace")


def _latest_release() -> dict[str, Any]:
    repo = os.getenv(_REPO_ENV, DEFAULT_REPO)
    return json.loads(_get_text(f"https://api.github.com/repos/{repo}/releases/latest"))


def check_updates() -> UpdateInfo | dict[str, Any]:
    """Manifest-backed update info for the latest published release.

    Returns an UpdateInfo when the release carries a usable latest.yml
    AND its version is newer than the running one; otherwise a dict
    with 'available': False plus an honest 'reason'. Reads two small
    documents; downloads nothing.
    """
    try:
        release = _latest_release()
    except Exception as exc:  # noqa: BLE001 - network truth is part of the answer
        return {"available": False, "reason": f"release check failed: {exc}"}
    tag = release.get("tag_name", "")
    asset_url = next(
        (a.get("browser_download_url", "") for a in release.get("assets", [])
         if a.get("name") == "latest.yml"),
        "",
    )
    if not (tag and asset_url):
        return {"available": False, "reason": "release publishes no latest.yml"}
    try:
        manifest = _get_text(asset_url)
    except Exception as exc:  # noqa: BLE001
        return {"available": False, "reason": f"manifest fetch failed: {exc}"}
    fields = dict(line.split(":", 1) for line in manifest.splitlines() if ":" in line)
    url = fields.get("url", "").strip()
    sha256 = fields.get("sha256", "").strip()
    size = fields.get("size", "").strip()
    if not (url and len(sha256) == 64):
        return {"available": False, "reason": "latest.yml incomplete"}
    version = tag.lstrip("v")
    if not version_newer(version, installed_version()):
        return {"available": False, "reason": "up to date"}
    return UpdateInfo(
        version=version,
        published_at=release.get("published_at", ""),
        url=url,
        sha256=sha256,
        size_bytes=int(size) if size.isdigit() else None,
    )


def staging_dir() -> Path:
    root = Path(settings.storage_root)
    return root / "update-pending"


def apply_update(info: UpdateInfo) -> dict[str, Any]:
    """Download, hash-verify, extract, and PREPARE the update.

    Leaves a swap marker plus a fully-staged tree under the storage
    root; the next launcher boot performs the swap. The running
    process and its files are never modified.
    """
    target = staging_dir()
    target.mkdir(parents=True, exist_ok=True)
    zip_path = target / "update.zip"
    digest = hashlib.sha256()
    req = Request(info.url, headers=UA)
    with urlopen(req, timeout=600) as resp, zip_path.open("wb") as fh:
        while chunk := resp.read(1 << 20):
            digest.update(chunk)
            fh.write(chunk)
    if digest.hexdigest().lower() != info.sha256.lower():
        zip_path.unlink(missing_ok=True)
        raise ValueError(
            "update rejected: sha256 mismatch (expected "
            f"{info.sha256[:12]}..., got {digest.hexdigest()[:12]}...)"
        )
    scratch = target / "tree"
    if scratch.exists():
        shutil.rmtree(scratch, ignore_errors=True)
    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(scratch)
    zip_path.unlink(missing_ok=True)
    inner = next((p for p in scratch.iterdir() if p.is_dir()), None)
    layout_ok = inner is not None and all((inner / d).is_dir() for d in ("native", "api", "web"))
    if not layout_ok:
        shutil.rmtree(scratch, ignore_errors=True)
        raise ValueError("update rejected: extracted tree is not a Trinity bundle")
    marker = target / "swap.json"
    marker.write_text(
        json.dumps(
            {"version": info.version, "source": str(inner),
             "requested_at": datetime.now(UTC).isoformat(timespec="seconds")},
            indent=2,
        ),
        encoding="utf-8",
    )
    return {"staged": True, "version": info.version, "restart_required": True}


def install_root() -> Path:
    """The program directory the launcher lives in (api's grandparent)."""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).parent.parent
    return REPO_ROOT


def consume_pending_swap() -> dict[str, Any] | None:
    """Called by the LAUNCHER at boot: perform the staged swap, if any.

    Replaces native / api / web in the install root with the staged
    tree and clears the marker. TRINITY_STORAGE_ROOT lives outside the
    install root, so user data is untouched by the file move itself.
    """
    marker = staging_dir() / "swap.json"
    if not marker.is_file():
        return None
    try:
        plan = json.loads(marker.read_text(encoding="utf-8"))
        source = Path(plan["source"])
    except Exception as exc:  # noqa: BLE001 - a broken marker must not brick boot
        marker.unlink(missing_ok=True)
        return {"swapped": [], "error": f"swap marker unreadable: {exc}"}
    here = install_root()
    swapped: list[str] = []
    for sub in ("native", "api", "web"):
        src = source / sub
        dst = here / sub
        if not src.is_dir():
            continue
        backup = here / f"{sub}.prev"
        shutil.rmtree(backup, ignore_errors=True)
        if dst.exists():
            dst.rename(backup)
        shutil.copytree(src, dst, dirs_exist_ok=True)
        swapped.append(sub)
    marker.unlink(missing_ok=True)
    shutil.rmtree(source.parent, ignore_errors=True)
    return {"swapped": swapped, "version": plan.get("version")}
