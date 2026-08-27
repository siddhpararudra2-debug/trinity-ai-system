from __future__ import annotations

from fastapi import APIRouter, Depends, WebSocket, WebSocketDisconnect

from app.auth import get_current_user
from app.collab import authorize_websocket, collaboration_manager
from app.models import User

router = APIRouter(tags=["collab"])


@router.websocket("/ws")
async def collaboration_ws(websocket: WebSocket, session_id: str = "default"):
    if not authorize_websocket(websocket):
        await websocket.close(code=1008, reason="Authentication required")
        return
    try:
        await collaboration_manager.handle(session_id, websocket)
    except WebSocketDisconnect:
        pass


@router.get("/collab/sessions/{session_id}")
async def collaboration_session(session_id: str, _: User = Depends(get_current_user)):
    normalized = collaboration_manager.normalize_session(session_id)
    room = collaboration_manager.rooms.get(normalized)
    return {"session_id": normalized, "members": len(room.clients) if room else 0, "status": "active" if room else "idle"}
