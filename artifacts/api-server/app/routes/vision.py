from __future__ import annotations

from fastapi import APIRouter, File, HTTPException, UploadFile

from app.engines.vision_engine import VisionEngine

router = APIRouter(tags=["vision"])
_engine = VisionEngine()
_MAX_IMAGE_BYTES = 10 * 1024 * 1024
_ALLOWED_TYPES = {"image/png", "image/jpeg", "image/jpg", "image/bmp", "image/tiff", "image/webp"}


@router.post("/vision/ocr")
async def run_vision_ocr(file: UploadFile = File(...), description: str = ""):
    content_type = (file.content_type or "").lower()
    if content_type not in _ALLOWED_TYPES:
        raise HTTPException(status_code=415, detail=f"Unsupported image type: {content_type or 'unknown'}")
    data = await file.read(_MAX_IMAGE_BYTES + 1)
    if len(data) > _MAX_IMAGE_BYTES:
        raise HTTPException(status_code=413, detail="Image exceeds the 10 MB limit")
    if not data:
        raise HTTPException(status_code=400, detail="Uploaded image is empty")
    result = await _engine.process(description=description, image_data=data)
    result["filename"] = file.filename or "upload"
    result["content_type"] = content_type
    return result
