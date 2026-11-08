"""M18.3: Python vision engine — image inspection + document parsing.

Both dependencies are optional ([vision] extra). Tests that need them
skip when they are absent; the refusal path is pinned unconditionally so
a bare install still fails honestly.
"""

from __future__ import annotations

import pytest

from src.core.errors import (
    ArtifactNotFoundError,
    CapabilityUnavailableError,
    RequestValidationError,
)
from src.engines import registry as registry_module
from src.engines.base import ArtifactRef
from src.engines.pcb import PcbEngine
from src.engines.vision import engine as engine_module
from src.engines.vision.engine import VisionEngine

needs_pillow = pytest.mark.skipif(
    engine_module.Image is None, reason="Pillow is not installed"
)
needs_pypdf = pytest.mark.skipif(
    engine_module.PdfReader is None, reason="pypdf is not installed"
)


@pytest.fixture()
def engine() -> VisionEngine:
    return VisionEngine()


@pytest.fixture()
def png(tmp_path):
    if engine_module.Image is None:
        pytest.skip("Pillow is not installed")
    from PIL import Image

    path = tmp_path / "sample.png"
    Image.new("RGB", (8, 6), (255, 0, 0)).save(path)
    return path


def test_describe_advertises_only_implemented_ops(engine):
    described = engine.describe()
    assert described["name"] == "vision"
    assert set(described["capabilities"]) == {"image_inspect", "document_parse"}
    assert "status" not in described


@needs_pillow
def test_image_inspect_reports_native_metadata(engine, png):
    result = engine.execute("image_inspect", {"path": str(png)})
    data = result.result
    assert result.success is True
    assert data["operation"] == "image_inspect"
    assert data["input"]["path"] == str(png)
    metadata = data["input"]["metadata"]
    assert metadata == {
        "width": 8,
        "height": 6,
        "channels": 3,
        "format": "png",
        "size_bytes": png.stat().st_size,
    }
    assert data["output"]["metadata"]["width"] == 0
    assert data["artifact_paths"] == []
    assert result.validation is not None
    assert result.validation.status == "VALIDATED"


@needs_pillow
def test_image_inspect_counts_channels_like_opencv(engine, tmp_path):
    from PIL import Image

    rgba = tmp_path / "rgba.png"
    Image.new("RGBA", (2, 2)).save(rgba)
    gray = tmp_path / "gray.bmp"
    Image.new("L", (2, 2)).save(gray)

    assert engine.execute("image_inspect", {"path": str(rgba)}).result["input"][
        "metadata"
    ]["channels"] == 4
    assert engine.execute("image_inspect", {"path": str(gray)}).result["input"][
        "metadata"
    ]["channels"] == 1


@needs_pillow
def test_image_inspect_resolves_an_artifact_id(engine, png, monkeypatch):
    from src.artifacts.manager import artifact_manager

    ref = ArtifactRef(
        artifact_id="art-1",
        type="Image",
        path=str(png),
        size_bytes=png.stat().st_size,
        checksum="0" * 64,
    )
    monkeypatch.setattr(artifact_manager, "get", lambda _artifact_id: ref)

    data = engine.execute("image_inspect", {"artifact_id": "art-1"}).result
    assert data["input"]["artifact_id"] == "art-1"
    assert data["input"]["path"] == str(png)
    assert data["input"]["metadata"]["width"] == 8


def test_image_inspect_refuses_an_unknown_artifact_id(engine, monkeypatch):
    from src.artifacts.manager import artifact_manager

    def missing(_artifact_id: str):
        raise ArtifactNotFoundError("no such artifact")

    monkeypatch.setattr(artifact_manager, "get", missing)
    with pytest.raises(RequestValidationError, match="No artifact 'ghost'"):
        engine.execute("image_inspect", {"artifact_id": "ghost"})


def test_image_inspect_refuses_bad_paths(engine, tmp_path):
    with pytest.raises(RequestValidationError, match="Missing or invalid"):
        engine.execute("image_inspect", {})
    with pytest.raises(RequestValidationError, match="does not exist"):
        engine.execute("image_inspect", {"path": str(tmp_path / "nope.png")})

    gif = tmp_path / "picture.gif"
    gif.write_bytes(b"GIF89a")
    with pytest.raises(RequestValidationError, match="Unsupported image format"):
        engine.execute("image_inspect", {"path": str(gif)})

    broken = tmp_path / "broken.png"
    broken.write_bytes(b"not an image")
    with pytest.raises(RequestValidationError, match="Failed to decode"):
        engine.execute("image_inspect", {"path": str(broken)})


def test_image_inspect_refuses_without_pillow(engine, monkeypatch, tmp_path):
    monkeypatch.setattr(engine_module, "Image", None)
    with pytest.raises(CapabilityUnavailableError) as excinfo:
        engine.execute("image_inspect", {"path": str(tmp_path / "anything.png")})
    assert excinfo.value.code == "capability_unavailable"
    assert excinfo.value.details["missing"] == "pillow"


def test_document_parse_reads_utf8_text(engine, tmp_path):
    document = tmp_path / "notes.md"
    document.write_text("hello\nworld\n", encoding="utf-8")
    result = engine.execute("document_parse", {"path": str(document)})
    data = result.result
    assert data["input"]["format"] == "text"
    assert data["document"] == {
        "format": "text",
        "page_count": 0,
        "text": "hello\nworld\n",
        "char_count": 12,
        "source_char_count": 12,
        "truncated": False,
    }
    assert data["message"] == ""
    assert result.validation is not None
    assert result.validation.status == "VALIDATED"


def test_document_parse_truncates_honestly(engine, tmp_path):
    document = tmp_path / "notes.txt"
    document.write_text("hello world", encoding="utf-8")
    data = engine.execute(
        "document_parse", {"path": str(document), "max_chars": 5}
    ).result
    assert data["document"]["text"] == "hello"
    assert data["document"]["char_count"] == 5
    assert data["document"]["source_char_count"] == 11
    assert data["document"]["truncated"] is True


@pytest.mark.parametrize("value", [0, 200001, "many", True])
def test_document_parse_refuses_bad_max_chars(engine, tmp_path, value):
    document = tmp_path / "notes.txt"
    document.write_text("hello", encoding="utf-8")
    with pytest.raises(RequestValidationError, match="max_chars"):
        engine.execute(
            "document_parse", {"path": str(document), "max_chars": value}
        )


def test_document_parse_refuses_unsupported_and_non_utf8(engine, tmp_path):
    docx = tmp_path / "report.docx"
    docx.write_bytes(b"PK\x03\x04")
    with pytest.raises(RequestValidationError, match="Unsupported document format"):
        engine.execute("document_parse", {"path": str(docx)})

    binary = tmp_path / "bad.txt"
    binary.write_bytes(b"\xff\xfe\x00")
    with pytest.raises(RequestValidationError, match="not valid UTF-8"):
        engine.execute("document_parse", {"path": str(binary)})


@needs_pypdf
def test_document_parse_reports_pdf_pages_and_no_fake_text(engine, tmp_path):
    from pypdf import PdfWriter

    document = tmp_path / "blank.pdf"
    writer = PdfWriter()
    writer.add_blank_page(width=200, height=200)
    with open(document, "wb") as handle:
        writer.write(handle)

    data = engine.execute("document_parse", {"path": str(document)}).result
    assert data["input"]["format"] == "pdf"
    assert data["document"]["page_count"] == 1
    assert data["document"]["text"] == ""
    assert data["document"]["char_count"] == 0
    assert data["document"]["truncated"] is False
    assert "OCR" in data["message"]


@needs_pypdf
def test_document_parse_refuses_encrypted_pdfs(engine, tmp_path):
    from pypdf import PdfWriter

    document = tmp_path / "secret.pdf"
    writer = PdfWriter()
    writer.add_blank_page(width=200, height=200)
    writer.encrypt("secret")
    with open(document, "wb") as handle:
        writer.write(handle)

    with pytest.raises(RequestValidationError, match="Encrypted PDFs"):
        engine.execute("document_parse", {"path": str(document)})


def test_document_parse_refuses_without_pypdf(engine, monkeypatch, tmp_path):
    document = tmp_path / "report.pdf"
    document.write_bytes(b"%PDF-1.4\n")
    monkeypatch.setattr(engine_module, "PdfReader", None)
    with pytest.raises(CapabilityUnavailableError) as excinfo:
        engine.execute("document_parse", {"path": str(document)})
    assert excinfo.value.code == "capability_unavailable"
    assert excinfo.value.details["missing"] == "pypdf"


def test_unknown_operation_refuses_with_capability_code(engine):
    with pytest.raises(CapabilityUnavailableError) as excinfo:
        engine.execute("geometry_extract", {})
    assert excinfo.value.code == "capability_unavailable"


def test_supported_formats_match_the_upload_endpoint(engine):
    from src.api.routes import UPLOAD_IMAGE_EXTENSIONS

    assert set(engine_module.SUPPORTED_IMAGE_FORMATS) == set(UPLOAD_IMAGE_EXTENSIONS)


def test_registry_uses_python_fallbacks_without_native(monkeypatch):
    monkeypatch.delenv("TRINITY_NATIVE_EXEC", raising=False)
    monkeypatch.setattr(registry_module.registry, "_engines", {})
    registry_module.bootstrap_engines()

    assert isinstance(registry_module.registry.get("vision"), VisionEngine)
    assert isinstance(registry_module.registry.get("pcb"), PcbEngine)
