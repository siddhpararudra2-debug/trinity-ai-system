"""Image intake: POST /api/uploads and the artifact_id the vision engine takes.

The point of these is that an upload behaves like any other artifact —
checksummed, tracked, retrievable — and that the things which should be
refused are refused at the edge with a usable message instead of becoming
a confusing failure deep inside the native process.
"""

from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from src.core.config import settings
from src.main import app
from tests.pnggen import png_bytes as _png_bytes


@pytest.fixture
def client():
    with TestClient(app) as c:
        yield c


def test_upload_returns_a_checksummed_artifact(client):
    response = client.post(
        "/api/uploads", files={"file": ("board.png", _png_bytes(), "image/png")}
    )
    assert response.status_code == 200, response.text
    ref = response.json()

    assert ref["type"] == "upload"
    assert ref["size_bytes"] == len(_png_bytes())
    assert len(ref["checksum"]) == 64
    assert ref["artifact_id"]

    # The stored file must actually be readable back through the artifact route.
    download = client.get(f"/api/artifacts/{ref['artifact_id']}")
    assert download.status_code == 200
    assert download.content.startswith(b"\x89PNG")


def test_upload_rejects_a_non_image_extension(client):
    response = client.post(
        "/api/uploads", files={"file": ("payload.exe", b"MZ\x90", "application/octet-stream")}
    )
    assert response.status_code == 422
    error = response.json()["errors"][0]
    assert error["code"] == "request_validation_error"
    assert "png" in error["details"]["supported"]


def test_upload_rejects_an_empty_file(client):
    response = client.post("/api/uploads", files={"file": ("empty.png", b"", "image/png")})
    assert response.status_code == 422
    assert "empty" in response.json()["errors"][0]["message"].lower()


def test_upload_rejects_a_file_over_the_limit(client):
    oversized = b"\x89PNG\r\n\x1a\n" + b"\x00" * (settings.max_upload_bytes + 1024)
    response = client.post(
        "/api/uploads", files={"file": ("huge.png", oversized, "image/png")}
    )
    assert response.status_code == 422
    assert "limit" in response.json()["errors"][0]["message"].lower()


def test_upload_does_not_trust_the_client_filename(client):
    """A traversal attempt must not escape artifact storage.

    Stronger than merely surviving: the client filename is discarded
    entirely and the stored name is generated server-side, so there is no
    path for a crafted name to influence at all.
    """
    response = client.post(
        "/api/uploads",
        files={"file": ("../../escape.png", _png_bytes(), "image/png")},
    )
    assert response.status_code == 200, response.text
    stored = response.json()["path"]
    assert ".." not in stored
    assert "escape" not in stored
    assert stored.endswith("upload.png")
    assert str(settings.artifacts_dir) in stored


def test_upload_route_is_documented(client):
    spec = client.get("/openapi.json").json()
    assert "post" in spec["paths"].get("/api/uploads", {})