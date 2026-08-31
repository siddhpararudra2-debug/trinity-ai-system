"""Lightweight WebSocket collaboration rooms.

The default is an in-memory room manager. When TRINITY_REDIS_URL is configured
and the optional redis package is installed, room events are mirrored over Redis
pub/sub so multiple API workers can broadcast to their local WebSocket clients.
"""
from __future__ import annotations

import asyncio
import hmac
import os
import json
import re
import time
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Any

from fastapi import WebSocket

from app.auth import decode_access_token
from app.security import configured_api_key


_SESSION_RE = re.compile(r"^[A-Za-z0-9_-]{1,96}$")
_MAX_MESSAGE = 12_000


@dataclass
class Room:
    clients: set[WebSocket] = field(default_factory=set)
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    created_at: float = field(default_factory=time.time)


def authorize_websocket(websocket: WebSocket) -> bool:
    """Validate an API key or bearer token before accepting a room connection."""
    configured = configured_api_key()
    api_key = websocket.headers.get("x-trinity-api-key", "")
    authorization = websocket.headers.get("authorization", "")
    bearer = authorization[7:].strip() if authorization.lower().startswith("bearer ") else websocket.query_params.get("access_token", "")
    if configured and hmac.compare_digest(api_key, configured):
        return True
    if configured and hmac.compare_digest(bearer, configured):
        return True
    if bearer:
        try:
            decode_access_token(bearer)
            return True
        except Exception:
            return False
    return False


class CollaborationManager:
    def __init__(self) -> None:
        self.rooms: dict[str, Room] = defaultdict(Room)
        self.redis_url = os.getenv("TRINITY_REDIS_URL", "").strip()
        self.redis = None
        self.redis_tasks: dict[str, asyncio.Task] = {}

    async def _ensure_redis_room(self, session_id: str) -> None:
        if not self.redis_url or session_id in self.redis_tasks:
            return
        try:
            from redis.asyncio import Redis
            self.redis = self.redis or Redis.from_url(self.redis_url, decode_responses=True)
            pubsub = self.redis.pubsub()
            await pubsub.subscribe(self._channel(session_id))
        except ImportError:
            return

        async def listen() -> None:
            try:
                async for item in pubsub.listen():
                    if item.get("type") != "message":
                        continue
                    try:
                        message = json.loads(item["data"])
                    except (TypeError, json.JSONDecodeError):
                        continue
                    await self.broadcast(session_id, message, from_redis=True)
            except asyncio.CancelledError:
                raise
            except Exception:
                return
            finally:
                try:
                    await pubsub.unsubscribe(self._channel(session_id))
                    await pubsub.close()
                except Exception:
                    pass

        self.redis_tasks[session_id] = asyncio.create_task(listen())

    def _channel(self, session_id: str) -> str:
        return f"trinity:collab:{session_id}"

    @staticmethod
    def normalize_session(session_id: str) -> str:
        if not _SESSION_RE.fullmatch(session_id):
            raise ValueError("session_id must contain only letters, numbers, underscores, or hyphens")
        return session_id

    async def connect(self, session_id: str, websocket: WebSocket) -> Room:
        session_id = self.normalize_session(session_id)
        await websocket.accept()
        room = self.rooms[session_id]
        await self._ensure_redis_room(session_id)
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
            task = self.redis_tasks.pop(session_id, None)
            if task:
                task.cancel()

    async def broadcast(self, session_id: str, message: dict[str, Any], exclude: WebSocket | None = None, from_redis: bool = False) -> None:
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
        if self.redis is not None and not from_redis:
            try:
                await self.redis.publish(self._channel(session_id), encoded)
            except Exception:
                pass

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
