"""Updater tests (M13): version math, manifest parsing, staged apply,
and the launcher swap. Network is only ever a localhost test double."""

from __future__ import annotations

import hashlib
import http.server
import json
import shutil
import threading
import zipfile
from pathlib import Path

import pytest

from src.core import updater


@pytest.fixture()
def isolated(tmp_path, monkeypatch):
    """Redirect every global the updater touches into tmp_path."""
    from types import SimpleNamespace

    monkeypatch.setattr(updater, "REPO_ROOT", tmp_path)
    monkeypatch.setattr(
        "src.core.updater.settings",
        SimpleNamespace(storage_root=tmp_path / "storage"),
    )
    return tmp_path


def test_version_newer_semver_shapes():
    assert updater.version_newer("0.10.0", "0.9.0") is True
    assert updater.version_newer("0.9.1", "0.9.0") is True
    assert updater.version_newer("0.9.0", "0.9.0") is False
    assert updater.version_newer("0.9.0", "0.10.0") is False
    assert updater.version_newer("v1.0.0", "0.9.0") is True
    # Version comparison refuses to lie on unparseable input.
    assert updater.version_newer("banana", "0.9.0") is False


def test_installed_version_order(isolated, monkeypatch):
    # 1. env override wins
    monkeypatch.setenv("TRINITY_APP_VERSION", "9.9.9")
    assert updater.installed_version() == "9.9.9"
    # 2. stamp file
    monkeypatch.delenv("TRINITY_APP_VERSION")
    cover = isolated / "config"
    cover.mkdir()
    (cover / "app_version.txt").write_text("0.7.2\n", encoding="utf-8")
    assert updater.installed_version() == "0.7.2"
    # 3. pyproject fallback
    shutil.rmtree(cover)
    (isolated / "pyproject.toml").write_text('version = "0.4.1"\n', encoding="utf-8")
    assert updater.installed_version() == "0.4.1"
    # 4. honest unknown
    (isolated / "pyproject.toml").unlink()
    assert updater.installed_version() == "0.0.0+unknown"


def test_check_up_to_date_and_incomplete(isolated, monkeypatch):
    monkeypatch.delenv("TRINITY_APP_VERSION", raising=False)
    monkeypatch.setattr(updater, "_latest_release", lambda: {
        "tag_name": "v0.9.0",
        "published_at": "2026-10-29T00:00:00Z",
        "assets": [{"name": "latest.yml", "browser_download_url": "http://x/latest.yml"}],
    })
    # current == latest tag -> 'up to date' (installed_version reads repo
    # pyproject; point it at a matching version).
    (isolated / "pyproject.toml").write_text('version = "0.9.0"\n', encoding="utf-8")
    monkeypatch.setattr(updater, "_get_text", lambda url, timeout=15.0: (
        "version: 0.9.0\n"
        "url: https://example.com/TrinityAI-0.9.0-win64.zip\n"
        "sha256: " + "a" * 64 + "\n"
        "size: 12345\n"))
    out = updater.check_updates()
    assert out == {"available": False, "reason": "up to date"}

    # Newer tag but a crippled manifest -> honest incompleteness.
    (isolated / "pyproject.toml").write_text('version = "0.4.0"\n', encoding="utf-8")
    monkeypatch.setattr(updater, "_get_text", lambda url, timeout=15.0: (
        "version: 0.9.0\nurl: \nsha256: short\n"))
    out = updater.check_updates()
    assert out["available"] is False and "incomplete" in out["reason"]


def test_check_returns_update_info(isolated, monkeypatch):
    monkeypatch.delenv("TRINITY_APP_VERSION", raising=False)
    (isolated / "pyproject.toml").write_text('version = "0.5.0"\n', encoding="utf-8")
    monkeypatch.setattr(updater, "_latest_release", lambda: {
        "tag_name": "v0.10.2",
        "published_at": "2026-10-30T00:00:00Z",
        "assets": [{"name": "latest.yml", "browser_download_url": "http://x/latest.yml"}],
    })
    monkeypatch.setattr(updater, "_get_text", lambda url, timeout=15.0: (
        "version: 0.10.2\n"
        "url: https://example.com/TrinityAI-0.10.2-win64.zip\n"
        "sha256: " + "b" * 64 + "\n"
        "size: 999\n"))
    info = updater.check_updates()
    assert isinstance(info, updater.UpdateInfo)
    assert info.version == "0.10.2"
    assert info.sha256 == "b" * 64
    assert info.size_bytes == 999


def test_network_refusal_is_honest(isolated, monkeypatch):
    def boom():
        raise RuntimeError("offline")
    monkeypatch.setattr(updater, "_latest_release", boom)
    out = updater.check_updates()
    assert out["available"] is False
    assert "offline" in out["reason"]


def _serve_zip(scratch: Path, zip_bytes: bytes):
    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Length", str(len(zip_bytes)))
            self.end_headers()
            self.wfile.write(zip_bytes)

        def log_message(self, *args):
            pass

    server = http.server.HTTPServer(("127.0.0.1", 0), H)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/update.zip"


def _fake_bundle(tmp_path: Path) -> bytes:
    root = tmp_path / "TrinityAI-9.9.9-win64"
    for sub in ("native", "api", "web"):
        (root / sub).mkdir(parents=True)
        (root / sub / "keep.txt").write_text(sub, encoding="utf-8")
    buf = tmp_path / "bundle.zip"
    with zipfile.ZipFile(buf, "w") as zf:
        zf.write(root, arcname=str(root))
        for f in root.rglob("*"):
            if f.is_file():
                zf.write(f, arcname=str(f.relative_to(tmp_path)))
    return buf.read_bytes()


def test_apply_update_stages_and_marks(isolated, tmp_path):
    zip_bytes = _fake_bundle(tmp_path)
    server, url = _serve_zip(tmp_path, zip_bytes)
    try:
        info = updater.UpdateInfo(
            version="9.9.9", published_at="", url=url,
            sha256=hashlib.sha256(zip_bytes).hexdigest(), size_bytes=len(zip_bytes),
        )
        out = updater.apply_update(info)
    finally:
        server.shutdown()
    assert out == {"staged": True, "version": "9.9.9", "restart_required": True}
    staging = updater.staging_dir()
    marker = json.loads((staging / "swap.json").read_text(encoding="utf-8"))
    assert marker["version"] == "9.9.9"
    assert (Path(marker["source"]) / "api" / "keep.txt").is_file()
    # The downloaded zip is consumed, not left behind.
    assert not (staging / "update.zip").exists()


def test_apply_update_rejects_hash_mismatch(isolated, tmp_path):
    zip_bytes = _fake_bundle(tmp_path)
    server, url = _serve_zip(tmp_path, zip_bytes)
    try:
        info = updater.UpdateInfo(
            version="9.9.9", published_at="", url=url,
            sha256="0" * 64, size_bytes=len(zip_bytes),
        )
        with pytest.raises(ValueError, match="sha256 mismatch"):
            updater.apply_update(info)
    finally:
        server.shutdown()
    # Nothing half-installed: no marker, no tree.
    assert not (updater.staging_dir() / "swap.json").exists()


def test_apply_update_rejects_bad_layout(isolated, tmp_path):
    # A zip whose tree lacks native/api/web is refused before staging.
    root = tmp_path / "TrinityAI-x-win64"
    (root / "junk").mkdir(parents=True)
    (root / "junk" / "a.txt").write_text("x")
    buf = tmp_path / "bundle.zip"
    with zipfile.ZipFile(buf, "w") as zf:
        zf.write(root, arcname=str(root))
        zf.write(root / "junk" / "a.txt", arcname="TrinityAI-x-win64/junk/a.txt")
    zip_bytes = buf.read_bytes()
    server, url = _serve_zip(tmp_path, zip_bytes)
    try:
        info = updater.UpdateInfo(
            version="9.9.9", published_at="", url=url,
            sha256=hashlib.sha256(zip_bytes).hexdigest(), size_bytes=len(zip_bytes),
        )
        with pytest.raises(ValueError, match="not a Trinity bundle"):
            updater.apply_update(info)
    finally:
        server.shutdown()
    assert not (updater.staging_dir() / "swap.json").exists()


def test_consume_pending_swap(isolated, tmp_path, monkeypatch):
    # Prepare: install root = the updater's notion of REPO_ROOT (patched);
    # staged tree under the storage root.
    install = isolated / "install"
    install.mkdir()
    for sub in ("native", "api", "web"):
        (install / sub).mkdir()
        (install / sub / "old.txt").write_text("old")
    monkey_src = tmp_path / "staged" / "TrinityAI-9.9.9-win64"
    for sub in ("native", "api", "web"):
        (monkey_src / sub).mkdir(parents=True)
        (monkey_src / sub / "new.txt").write_text("new")
    (updater.staging_dir() / "tree").mkdir(parents=True)
    (updater.staging_dir() / "swap.json").write_text(
        json.dumps({"version": "9.9.9", "source": str(monkey_src)}), encoding="utf-8")
    monkeypatch.setattr(updater, "install_root", lambda: install)

    out = updater.consume_pending_swap()
    assert out["swapped"] == ["native", "api", "web"]
    assert out["version"] == "9.9.9"
    for sub in ("native", "api", "web"):
        assert (install / sub / "new.txt").is_file()
        assert not (install / sub / "old.txt").exists()
        backup = install / f"{sub}.prev"
        assert (backup / "old.txt").is_file()
    # Marker and staging tree are gone; the storage root persists.
    assert not (updater.staging_dir() / "swap.json").exists()
    assert (isolated / "storage").is_dir()


def test_consume_pending_swap_noop_when_no_marker(isolated):
    assert updater.consume_pending_swap() is None
