"""Lightweight WebSocket collaboration rooms.

This is a single-process implementation. Production deployments should replace
this manager with a Redis-backed pub/sub layer when multiple API workers run.
"""
from __future__ import annotations

import asyncio
import json
import re
import time
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Any

from fastapi import WebSocket


_SESSION_RE = re.compile(r"^[A-Za-z0-9_-]{1,96}$")
_MAX_MESSAGE = 12_000


@dataclass
class Room:
    clients: set[WebSocket] = field(default_factory=set)
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    created_at: float = field(default_factory=time.time)


class CollaborationManager:
    def __init__(self) -> None:
        self.rooms: dict[str, Room] = defaultdict(Room)

    @staticmethod
    def normalize_session(session_id: str) -> str:
        if not _SESSION_RE.fullmatch(session_id):
            raise ValueError("session_id must contain only letters, numbers, underscores, or hyphens")
        return session_id

    async def connect(self, session_id: str, websocket: WebSocket) -> Room:
        session_id = self.normalize_session(session_id)
        await websocket.accept()
        room = self.rooms[session_id]
        async with room.lock:
            room.clients.add(websocket)
        await self.broadcast(session_id, {"type": "system", "event": "joined", "members": len(room.clients)}, exclude=websocket)
        return room

    async def disconnect(self, session_id: str, websocket: WebSocket) -> None:
        room = self.rooms.get(session_id)
        if not room:
            return
        async with room.lock:
            room.clients.discard(websocket)
            remaining = len(room.clients)
        if remaining:
            await self.broadcast(session_id, {"type": "system", "event": "left", "members": remaining})
        else:
            self.rooms.pop(session_id, None)

    async def broadcast(self, session_id: str, message: dict[str, Any], exclude: WebSocket | None = None) -> None:
        room = self.rooms.get(session_id)
        if not room:
            return
        encoded = json.dumps(message)
        stale: list[WebSocket] = []
        async with room.lock:
            clients = list(room.clients)
        for client in clients:
            if client is exclude:
                continue
            try:
                await client.send_text(encoded)
            except Exception:
                stale.append(client)
        if stale:
            async with room.lock:
                for client in stale:
                    room.clients.discard(client)

    async def handle(self, session_id: str, websocket: WebSocket) -> None:
        try:
            room = await self.connect(session_id, websocket)
            await websocket.send_json({"type": "system", "event": "connected", "session_id": session_id, "members": len(room.clients)})
            while True:
                raw = await websocket.receive_text()
                if len(raw) > _MAX_MESSAGE:
                    await websocket.send_json({"type": "error", "message": "Message exceeds the 12,000 character limit"})
                    continue
                try:
                    payload = json.loads(raw)
                except json.JSONDecodeError:
                    payload = {"type": "text", "content": raw}
                if not isinstance(payload, dict):
                    await websocket.send_json({"type": "error", "message": "WebSocket messages must be JSON objects or text"})
                    continue
                payload["type"] = str(payload.get("type", "update"))[:32]
                payload["session_id"] = session_id
                payload["server_time"] = time.time()
                await self.broadcast(session_id, payload, exclude=websocket)
        except Exception:
            # Disconnects, malformed client frames, and network closures are
            # cleaned up in finally without leaking room state.
            pass
        finally:
            await self.disconnect(session_id, websocket)


collaboration_manager = CollaborationManager()
