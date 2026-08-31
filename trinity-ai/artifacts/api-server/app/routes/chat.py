"""Unified Trinity chat endpoint — routes to the appropriate engine."""
from fastapi import APIRouter, Depends
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select
from pydantic import BaseModel

from app.database import get_db
from app.models import Conversation, Message
from app.unified_router.orchestrator import TrinityOrchestrator

router = APIRouter(tags=["chat"])
_orchestrator: TrinityOrchestrator | None = None


def get_orchestrator() -> TrinityOrchestrator:
    global _orchestrator
    if _orchestrator is None:
        _orchestrator = TrinityOrchestrator()
    return _orchestrator


class ChatInput(BaseModel):
    content: str
    conversation_id: int | None = None


@router.post("/chat")
async def send_chat(data: ChatInput, db: AsyncSession = Depends(get_db)):
    """
    Main Trinity entry point.

    1. Gets or creates a conversation.
    2. Saves the user message.
    3. Routes the query through the TrinityOrchestrator.
    4. Saves the assistant response and returns it.
    """
    orchestrator = get_orchestrator()

    # --- Conversation -------------------------------------------------------
    if data.conversation_id is not None and data.conversation_id > 0:
        result = await db.execute(
            select(Conversation).where(Conversation.id == data.conversation_id)
        )
        conv = result.scalar_one_or_none() or Conversation(
            title=data.content[:60]
        )
        if conv.id is None:
            db.add(conv)
            await db.flush()
    else:
        title = data.content[:60] + ("..." if len(data.content) > 60 else "")
        conv = Conversation(title=title)
        db.add(conv)
        await db.flush()

    # --- User message -------------------------------------------------------
    user_msg = Message(
        conversation_id=conv.id, role="user", content=data.content
    )
    db.add(user_msg)
    await db.flush()

    # --- Conversation history (for context) ---------------------------------
    history_result = await db.execute(
        select(Message)
        .where(Message.conversation_id == conv.id)
        .order_by(Message.created_at.desc())
        .limit(20)
    )
    history = history_result.scalars().all()[::-1]

    # --- Engine routing -------------------------------------------------------
    response = await orchestrator.route(data.content, history)

    # --- Assistant message --------------------------------------------------
    assistant_msg = Message(
        conversation_id=conv.id,
        role="assistant",
        content=response["content"],
        engine=response.get("engine", "orchestrator"),
    )
    assistant_msg.data = response.get("data")
    db.add(assistant_msg)

    await db.commit()
    await db.refresh(assistant_msg)

    return {
        "id": assistant_msg.id,
        "conversation_id": conv.id,
        "content": assistant_msg.content,
        "engine": assistant_msg.engine,
        "data": assistant_msg.data,
        "created_at": assistant_msg.created_at.isoformat(),
    }
