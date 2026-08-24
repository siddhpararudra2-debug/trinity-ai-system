from __future__ import annotations

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from app.collab import collaboration_manager

router = APIRouter(tags=["collab"])


@router.websocket("/ws")
async def collaboration_ws(websocket: WebSocket, session_id: str = "default"):
    try:
        await collaboration_manager.handle(session_id, websocket)
    except WebSocketDisconnect:
        pass


@router.get("/collab/sessions/{session_id}")
async def collaboration_session(session_id: str):
    normalized = collaboration_manager.normalize_session(session_id)
    room = collaboration_manager.rooms.get(normalized)
    return {"session_id": normalized, "members": len(room.clients) if room else 0, "status": "active" if room else "idle"}
