"""Vision engine (M18.3): Python fallback for image/document inspection.

The native engine stays preferred wherever ``Trinity.exe`` exists
(Windows). On the Linux bundle and native-less CI this engine answers
two ops honestly:

* ``image_inspect`` mirrors the native ``load_image`` result envelope
  (``input.metadata`` width/height/channels/format/size_bytes) using
  Pillow. It accepts a concrete ``path`` or an uploaded ``artifact_id``
  (resolved through the artifact manager, like the native bridge does).
* ``document_parse`` extracts text from PDFs (pypdf) and plain-text
  formats. It reports page counts and truncation truthfully; a scanned
  PDF with no text layer returns empty text and says OCR is native-only.

OCR, contour extraction, and feature matching are not implemented here
and are not advertised by this engine. Both dependencies live in the
``[vision]`` extra; without them the engine refuses with an install
hint instead of pretending.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from src.core.errors import CapabilityUnavailableError, RequestValidationError
from src.engines.base import BaseEngine, EngineResult, ValidationResult

try:  # Pillow is an optional [vision] dependency.
    from PIL import Image
except ImportError:  # pragma: no cover - exercised by the refusal test
    Image = None  # type: ignore[assignment]

try:  # pypdf is an optional [vision] dependency.
    from pypdf import PdfReader
except ImportError:  # pragma: no cover - exercised by the refusal test
    PdfReader = None  # type: ignore[assignment]

# Keep in sync with src.api.routes.UPLOAD_IMAGE_EXTENSIONS and the native
# VisionEngine::supportedFormats() list; a test pins all three.
SUPPORTED_IMAGE_FORMATS = ("png", "jpg", "jpeg", "bmp")
TEXT_SUFFIXES = frozenset(
    {
        ".txt",
        ".md",
        ".markdown",
        ".rst",
        ".csv",
        ".json",
        ".log",
        ".yaml",
        ".yml",
        ".xml",
        ".html",
        ".htm",
    }
)
DEFAULT_MAX_CHARS = 20_000
MAX_MAX_CHARS = 200_000


def _empty_metadata() -> dict[str, Any]:
    return {"width": 0, "height": 0, "channels": 0, "format": "", "size_bytes": 0}


def _empty_image_input() -> dict[str, Any]:
    return {"path": "", "artifact_id": "", "bytes_ref": "", "metadata": _empty_metadata()}


def _resolve_path(
    parameters: dict[str, Any], operation: str
) -> tuple[Path, str]:
    """Resolve ``path`` or an uploaded ``artifact_id`` (bridge parity)."""
    path = parameters.get("path")
    artifact_id = parameters.get("artifact_id")
    if isinstance(path, str) and path:
        resolved = Path(path)
    elif isinstance(artifact_id, str) and artifact_id:
        from src.artifacts.manager import artifact_manager  # noqa: PLC0415
        from src.core.errors import ArtifactNotFoundError  # noqa: PLC0415

        try:
            ref = artifact_manager.get(artifact_id)
        except ArtifactNotFoundError as exc:
            raise RequestValidationError(
                f"No artifact '{artifact_id}' for vision/{operation}",
                details={"artifact_id": artifact_id, "engine": "vision"},
            ) from exc
        resolved = Path(ref.path)
        if not resolved.is_file():
            raise RequestValidationError(
                f"Artifact '{artifact_id}' has no file on disk",
                details={"artifact_id": artifact_id, "engine": "vision"},
            )
    else:
        raise RequestValidationError(
            "Missing or invalid 'path' parameter for vision operation",
            details={"operation": operation},
        )
    return resolved, artifact_id if isinstance(artifact_id, str) else ""


def _require_pillow(operation: str) -> None:
    if Image is None:
        raise CapabilityUnavailableError(
            "Image inspection requires Pillow, which is not installed; "
            "install the 'vision' extra (pip install '.[vision]')",
            details={
                "engine": "vision",
                "operation": operation,
                "missing": "pillow",
                "env": "pip install '.[vision]'",
            },
        )


def _channels(image: Any) -> int:
    # OpenCV's IMREAD_UNCHANGED decodes palette PNGs to BGR, so a
    # palette image reports 3 channels, not Pillow's single "P" band.
    if image.mode == "P":
        return 3
    return len(image.getbands())


def _image_metadata(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise RequestValidationError(
            "Image file does not exist", details={"path": str(path)}
        )
    suffix = path.suffix.lower().lstrip(".")
    if suffix not in SUPPORTED_IMAGE_FORMATS:
        raise RequestValidationError(
            "Unsupported image format",
            details={"path": str(path), "supported": list(SUPPORTED_IMAGE_FORMATS)},
        )
    try:
        with Image.open(path) as image:
            image.load()
            width, height = image.size
            channels = _channels(image)
    except RequestValidationError:
        raise
    except Exception as exc:  # Pillow raises several unrelated types
        raise RequestValidationError(
            "Failed to decode image data",
            details={"path": str(path), "reason": str(exc)},
        ) from exc
    return {
        "width": width,
        "height": height,
        "channels": channels,
        "format": suffix,
        "size_bytes": path.stat().st_size,
    }


def _bounded_max_chars(parameters: dict[str, Any]) -> int:
    value = parameters.get("max_chars", DEFAULT_MAX_CHARS)
    if isinstance(value, bool) or not isinstance(value, int):
        raise RequestValidationError(
            "max_chars must be an integer",
            details={"param": "max_chars"},
        )
    if not (1 <= value <= MAX_MAX_CHARS):
        raise RequestValidationError(
            f"max_chars must be in [1, {MAX_MAX_CHARS}]",
            details={"param": "max_chars"},
        )
    return value


class VisionEngine(BaseEngine):
    """Python implementation of the advertised image/document ops."""

    name = "vision"
    version = "0.1"
    capabilities = ("image_inspect", "document_parse")

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "image_inspect":
            return self._image_inspect(parameters)
        if operation == "document_parse":
            return self._document_parse(parameters)
        raise CapabilityUnavailableError(
            f"The vision engine has no Python implementation for "
            f"'{operation}' yet",
            details={
                "engine": self.name,
                "operation": operation,
                "status": "python",
            },
        )

    # -- image_inspect ---------------------------------------------------

    def _image_inspect(self, parameters: dict[str, Any]) -> EngineResult:
        # Request validation precedes the capability check: a malformed
        # request is the caller's error (422-class) even when Pillow is
        # absent; the capability error only answers a well-formed request
        # the environment genuinely cannot serve. document_parse already
        # follows this order; keep the two operations symmetric.
        path, artifact_id = _resolve_path(parameters, "image_inspect")
        _require_pillow("image_inspect")
        metadata = _image_metadata(path)
        data = {
            "success": True,
            "operation": "image_inspect",
            "input": {
                "path": str(path),
                "artifact_id": artifact_id,
                "bytes_ref": "",
                "metadata": metadata,
            },
            "output": _empty_image_input(),
            "statistics": {},
            "artifact_paths": [],
            "message": "",
        }
        return EngineResult(
            success=True,
            engine=self.name,
            operation="image_inspect",
            result=data,
            validation=ValidationResult(
                status="VALIDATED",
                checks={
                    "engine": "vision",
                    "operation": "image_inspect",
                    "message": "Vision operation completed successfully",
                },
            ),
        )

    # -- document_parse --------------------------------------------------

    def _document_parse(self, parameters: dict[str, Any]) -> EngineResult:
        path, artifact_id = _resolve_path(parameters, "document_parse")
        if not path.is_file():
            raise RequestValidationError(
                "Document file does not exist", details={"path": str(path)}
            )
        max_chars = _bounded_max_chars(parameters)
        suffix = path.suffix.lower()
        if suffix == ".pdf":
            text, page_count = self._pdf_text(path)
            document_format = "pdf"
            message = (
                ""
                if text
                else "No extractable text; scanned PDFs need OCR, which is "
                "native-only"
            )
        elif suffix in TEXT_SUFFIXES:
            try:
                text = path.read_text(encoding="utf-8")
            except UnicodeDecodeError as exc:
                raise RequestValidationError(
                    "Text document is not valid UTF-8",
                    details={"path": str(path)},
                ) from exc
            page_count = 0
            document_format = "text"
            message = ""
        else:
            raise RequestValidationError(
                "Unsupported document format",
                details={
                    "path": str(path),
                    "supported": ["pdf", *sorted(TEXT_SUFFIXES)],
                },
            )

        source_char_count = len(text)
        truncated = source_char_count > max_chars
        if truncated:
            text = text[:max_chars]
        data = {
            "success": True,
            "operation": "document_parse",
            "input": {
                "path": str(path),
                "artifact_id": artifact_id,
                "format": document_format,
                "size_bytes": path.stat().st_size,
            },
            "document": {
                "format": document_format,
                "page_count": page_count,
                "text": text,
                "char_count": len(text),
                "source_char_count": source_char_count,
                "truncated": truncated,
            },
            "message": message,
        }
        return EngineResult(
            success=True,
            engine=self.name,
            operation="document_parse",
            result=data,
            validation=ValidationResult(
                status="VALIDATED",
                checks={
                    "engine": "vision",
                    "operation": "document_parse",
                    "message": "Document parsed successfully",
                },
            ),
        )

    @staticmethod
    def _pdf_text(path: Path) -> tuple[str, int]:
        if PdfReader is None:
            raise CapabilityUnavailableError(
                "PDF parsing requires pypdf, which is not installed; "
                "install the 'vision' extra (pip install '.[vision]')",
                details={
                    "engine": "vision",
                    "operation": "document_parse",
                    "missing": "pypdf",
                    "env": "pip install '.[vision]'",
                },
            )
        try:
            reader = PdfReader(str(path))
            if reader.is_encrypted:
                raise RequestValidationError(
                    "Encrypted PDFs are not supported",
                    details={"path": str(path)},
                )
            page_count = len(reader.pages)
            text = "\n\n".join(
                page.extract_text() or "" for page in reader.pages
            ).strip()
        except RequestValidationError:
            raise
        except Exception as exc:  # pypdf raises several unrelated types
            raise RequestValidationError(
                "Failed to parse PDF",
                details={"path": str(path), "reason": str(exc)},
            ) from exc
        return text, page_count
