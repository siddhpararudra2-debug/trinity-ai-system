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

    # Pre-fetch counts and last engines in bulk to avoid N+1 queries
    counts_result = await db.execute(
        select(Message.conversation_id, func.count(Message.id))
        .group_by(Message.conversation_id)
    )
    counts_map = dict(counts_result.all())

    # We can fetch the last engine using a window function, but SQLite's support
    # in SQLAlchemy might be tricky without full subqueries.
    # A simpler approach for SQLite since we don't have millions of rows right away:
    # Just grab all assistant messages, sort by created_at, and build a map.
    engines_result = await db.execute(
        select(Message.conversation_id, Message.engine)
        .where(Message.role == "assistant")
        .order_by(Message.created_at)
    )
    engines_map = {row[0]: row[1] for row in engines_result.all()}

    items = []
    for conv in conversations:
        items.append({
            "id": conv.id,
            "title": conv.title,
            "created_at": conv.created_at.isoformat(),
            "updated_at": (conv.updated_at or conv.created_at).isoformat(),
            "message_count": counts_map.get(conv.id, 0),
            "last_engine": engines_map.get(conv.id, None),
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
