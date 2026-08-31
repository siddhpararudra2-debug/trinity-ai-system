"""
Trinity Vision Engine — Handwritten OCR and LaTeX extraction.
Full capability requires pix2tex (pip install pix2tex).
Currently provides the interface and structure; model weights load on demand.
"""
import asyncio
import io
import shutil
import subprocess
from typing import Any



class VisionEngine:

    async def process(self, description: str = "", image_data: bytes | None = None) -> dict[str, Any]:
        if image_data:
            return await asyncio.get_event_loop().run_in_executor(
                None, self._ocr, image_data
            )
        return {
            "description": "Trinity Vision Engine ready",
            "recognized_text": "",
            "latex": "",
            "status": "awaiting_image",
            "message": "Upload an image to extract text or LaTeX formulas. Supports handwritten equations, printed text, and scientific notation.",
            "supported_formats": ["PNG", "JPG", "JPEG", "BMP", "TIFF", "WEBP"],
            "engine": "vision",
        }

    def _ocr(self, image_data: bytes) -> dict[str, Any]:
        try:
            from PIL import Image
            img = Image.open(io.BytesIO(image_data))
            img.verify()
            img = Image.open(io.BytesIO(image_data))
        except Exception as exc:
            return {
                "description": "Invalid image",
                "recognized_text": "",
                "latex": "",
                "status": "error",
                "message": f"Image could not be decoded: {exc}",
                "engine": "vision",
            }
        try:
            from pix2tex.cli import LatexOCR
            model = LatexOCR()
            latex_str = model(img)
            return {
                "description": "LaTeX extracted from image",
                "recognized_text": latex_str,
                "latex": latex_str,
                "status": "success",
                "engine": "vision",
            }
        except ImportError:
            tesseract = shutil.which("tesseract")
            if tesseract:
                try:
                    result = subprocess.run([tesseract, "stdin", "stdout", "--psm", "6"], input=image_data, capture_output=True, timeout=30, check=False)
                    text = result.stdout.decode("utf-8", errors="replace").strip()
                    if result.returncode == 0:
                        return {
                            "description": "Text extracted from image",
                            "recognized_text": text,
                            "latex": "",
                            "status": "success",
                            "engine": "vision",
                            "ocr_backend": "tesseract",
                        }
                except (OSError, subprocess.TimeoutExpired):
                    pass
            return {
                "description": "OCR backend not installed",
                "recognized_text": "",
                "latex": "",
                "status": "error",
                "message": "Install pix2tex for handwritten LaTeX or Tesseract for printed-text OCR.",
                "engine": "vision",
            }
        except Exception as exc:
            return {
                "description": "OCR processing failed",
                "recognized_text": "",
                "latex": "",
                "status": "error",
                "message": str(exc),
                "engine": "vision",
            }
