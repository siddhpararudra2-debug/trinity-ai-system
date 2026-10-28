"""Packaged-mode static UI serving (TRINITY_UI_DIR).

The bundle serves the Next.js static export from the API process so the
shipped application is a single Python process. The mount happens at
import time from the environment, so each case needs a fresh module —
importlib.reload(src.main) with the env set (or unset) first.
"""

import importlib
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

import src.main


@pytest.fixture
def ui_dir(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    """An out/ directory with an index.html and a chat.html next-page."""
    out = tmp_path / "out"
    (out / "_next").mkdir(parents=True)
    (out / "index.html").write_text("<html><body>INDEX</body></html>")
    (out / "chat.html").write_text("<html><body>CHAT</body></html>")
    # Next.js exports a real 404.html; with it present, StaticFiles
    # returns a 404 FileResponse instead of raising for unknown paths.
    (out / "404.html").write_text("<html><body>NOPE</body></html>")
    return out


def _reload() -> None:
    importlib.reload(src.main)


@pytest.fixture(autouse=True)
def restore_env(monkeypatch: pytest.MonkeyPatch):
    # importlib.reload leaves the module object switched; tests that
    # import `from src.main import app` (the rest of the suite) read a
    # cached app built WITHOUT a UI mount, so restoring unset env and
    # reloading at teardown keeps neighbors untouched.
    monkeypatch.delenv("TRINITY_UI_DIR", raising=False)
    yield
    _reload()


def test_ui_dir_serves_static_export(ui_dir: Path, monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setenv("TRINITY_UI_DIR", str(ui_dir))
    _reload()
    with TestClient(src.main.app) as client:
        root = client.get("/")
        assert root.status_code == 200
        assert "INDEX" in root.text
        # Next.js navigate routes: /chat is served from chat.html.
        chat = client.get("/chat")
        assert chat.status_code == 200
        assert "CHAT" in chat.text
        # API routes keep priority over the mount.
        health = client.get("/api/health")
        assert health.status_code == 200


def test_unset_ui_dir_skips_mount(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.delenv("TRINITY_UI_DIR", raising=False)
    _reload()
    with TestClient(src.main.app) as client:
        # No mount: "/" is unknown, API still answers.
        assert client.get("/").status_code == 404
        assert client.get("/api/health").status_code == 200


def test_missing_ui_dir_skips_mount(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setenv("TRINITY_UI_DIR", str(tmp_path / "does-not-exist"))
    _reload()
    with TestClient(src.main.app) as client:
        assert client.get("/").status_code == 404
        assert client.get("/api/health").status_code == 200


def test_unknown_extensionless_path_falls_through_to_404(
    ui_dir: Path, monkeypatch: pytest.MonkeyPatch
):
    """Only a real exported page resolves via the .html retry; a genuinely
    unknown path still answers 404 with the Next.js not-found page."""
    monkeypatch.setenv("TRINITY_UI_DIR", str(ui_dir))
    _reload()
    with TestClient(src.main.app) as client:
        assert client.get("/chat").status_code == 200
        missing = client.get("/no-such-page")
        assert missing.status_code == 404
        assert "NOPE" in missing.text
