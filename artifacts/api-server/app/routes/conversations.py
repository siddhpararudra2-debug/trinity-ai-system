"""CRUD endpoints for Trinity conversations and messages."""
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select, func
from pydantic import BaseModel

from app.database import get_db
from app.models import Conversation, Message

router = APIRouter(tags=["conversations"])


class ConversationCreate(BaseModel):
    title: str = "New Conversation"


# ---------------------------------------------------------------------------
# Conversations
# ---------------------------------------------------------------------------

@router.get("/conversations")
async def list_conversations(db: AsyncSession = Depends(get_db)):
    result = await db.execute(
        select(Conversation).order_by(Conversation.updated_at.desc())
    )
    conversations = result.scalars().all()

    items = []
    for conv in conversations:
        count_result = await db.execute(
            select(func.count()).where(Message.conversation_id == conv.id)
        )
        count = count_result.scalar() or 0

        last_engine_result = await db.execute(
            select(Message.engine)
            .where(Message.conversation_id == conv.id, Message.role == "assistant")
            .order_by(Message.created_at.desc())
            .limit(1)
        )
        last_engine = last_engine_result.scalar()

        items.append({
            "id": conv.id,
            "title": conv.title,
            "created_at": conv.created_at.isoformat(),
            "updated_at": (conv.updated_at or conv.created_at).isoformat(),
            "message_count": count,
            "last_engine": last_engine,
        })

    return items


@router.post("/conversations", status_code=201)
async def create_conversation(
    data: ConversationCreate, db: AsyncSession = Depends(get_db)
):
    conv = Conversation(title=data.title or "New Conversation")
    db.add(conv)
    await db.commit()
    await db.refresh(conv)
    return {
        "id": conv.id,
        "title": conv.title,
        "created_at": conv.created_at.isoformat(),
        "updated_at": conv.created_at.isoformat(),
        "message_count": 0,
        "last_engine": None,
    }


@router.delete("/conversations/{id}", status_code=204)
async def delete_conversation(id: int, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(Conversation).where(Conversation.id == id))
    conv = result.scalar_one_or_none()
    if not conv:
        raise HTTPException(status_code=404, detail="Conversation not found")
    await db.delete(conv)
    await db.commit()


# ---------------------------------------------------------------------------
# Messages
# ---------------------------------------------------------------------------

@router.get("/conversations/{id}/messages")
async def get_messages(id: int, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(Conversation).where(Conversation.id == id))
    if not result.scalar_one_or_none():
        raise HTTPException(status_code=404, detail="Conversation not found")

    msgs = await db.execute(
        select(Message)
        .where(Message.conversation_id == id)
        .order_by(Message.created_at)
    )
    return [
        {
            "id": m.id,
            "conversation_id": m.conversation_id,
            "role": m.role,
            "content": m.content,
            "engine": m.engine,
            "data": m.data,
            "created_at": m.created_at.isoformat(),
        }
        for m in msgs.scalars().all()
    ]
