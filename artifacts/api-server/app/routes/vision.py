from __future__ import annotations

from fastapi import APIRouter, Depends, File, Form, HTTPException, UploadFile
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user
from app.database import get_db
from app.engines.vision_engine import VisionEngine
from app.models import Conversation, Message, User

router = APIRouter(tags=["vision"])
_engine = VisionEngine()
_MAX_IMAGE_BYTES = 10 * 1024 * 1024
_ALLOWED_TYPES = {"image/png", "image/jpeg", "image/jpg", "image/bmp", "image/tiff", "image/webp"}


@router.post("/vision/ocr")
async def run_vision_ocr(
    file: UploadFile = File(...),
    description: str = Form(default=""),
    conversation_id: int = Form(...),
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    conversation_result = await db.execute(
        select(Conversation).where(
            Conversation.id == conversation_id,
            Conversation.owner_id == user.id,
        )
    )
    if conversation_result.scalar_one_or_none() is None:
        raise HTTPException(status_code=404, detail="Conversation not found")

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
    result["conversation_id"] = conversation_id

    db.add(Message(
        conversation_id=conversation_id,
        role="user",
        content=description.strip() or f"Uploaded image: {file.filename or 'upload'}",
        engine="vision",
    ))
    assistant = Message(
        conversation_id=conversation_id,
        role="assistant",
        content=result.get("description") or result.get("message") or "Vision OCR result",
        engine="vision",
    )
    assistant.data = result
    db.add(assistant)
    await db.commit()
    await db.refresh(assistant)
    result["message_id"] = assistant.id
    return result
