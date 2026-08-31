from __future__ import annotations

import asyncio
import io
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, patch

from fastapi import HTTPException
from fastapi import UploadFile
from starlette.datastructures import Headers
from sqlalchemy import select
from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine
from starlette.requests import Request

from app.auth import validate_auth_configuration
from app.database import Base
from app.job_queue import claim, enqueue
from app.routes.health import health_check, readiness_check
from app.routes import vision as vision_route
from app.models import Conversation, Message, User
from app.security import require_api_key_for_request
from app.collab import authorize_websocket


class SecurityRegressionTests(unittest.TestCase):
    @staticmethod
    def request(method: str, path: str, headers: list[tuple[bytes, bytes]] | None = None) -> Request:
        return Request({
            "type": "http",
            "method": method,
            "path": path,
            "headers": headers or [],
            "query_string": b"",
            "scheme": "http",
            "server": ("testserver", 80),
            "client": ("testclient", 1),
        })

    def test_api_key_allows_cors_preflight_without_credentials(self):
        with patch.dict(os.environ, {"TRINITY_API_KEY": "secret"}):
            require_api_key_for_request(self.request("OPTIONS", "/api/chat"))

    def test_auth_required_rejects_missing_or_short_signing_secret(self):
        with patch.dict(os.environ, {"TRINITY_AUTH_REQUIRED": "1"}, clear=True):
            with self.assertRaises(RuntimeError):
                validate_auth_configuration()
        with patch.dict(os.environ, {"TRINITY_AUTH_REQUIRED": "1", "TRINITY_AUTH_SECRET": "x" * 32}):
            validate_auth_configuration()

    def test_websocket_requires_credential(self):
        class FakeWebSocket:
            def __init__(self, headers: dict[str, str], query: dict[str, str] | None = None):
                self.headers = headers
                self.query_params = query or {}

        with patch.dict(os.environ, {}, clear=True):
            self.assertFalse(authorize_websocket(FakeWebSocket({})))
        with patch.dict(os.environ, {"TRINITY_API_KEY": "secret"}):
            self.assertTrue(authorize_websocket(FakeWebSocket({"x-trinity-api-key": "secret"})))

    def test_failed_health_probes_return_503(self):
        class BrokenDb:
            async def execute(self, _query):
                raise RuntimeError("database unavailable")

        async def scenario():
            with self.assertRaises(HTTPException) as ready_error:
                await readiness_check(BrokenDb())
            self.assertEqual(ready_error.exception.status_code, 503)
            with self.assertRaises(HTTPException) as health_error:
                await health_check(BrokenDb())
            self.assertEqual(health_error.exception.status_code, 503)

        asyncio.run(scenario())

    def test_vision_upload_persists_to_owned_conversation(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as directory:
                engine = create_async_engine(f"sqlite+aiosqlite:///{directory}/vision.db")
                async with engine.begin() as connection:
                    await connection.run_sync(Base.metadata.create_all)
                sessions = async_sessionmaker(engine, expire_on_commit=False)
                async with sessions() as session:
                    conversation = Conversation(title="Vision audit", owner_id=1)
                    session.add(conversation)
                    await session.commit()
                    await session.refresh(conversation)
                    upload = UploadFile(
                        filename="equation.png",
                        file=io.BytesIO(b"image-bytes"),
                        headers=Headers({"content-type": "image/png"}),
                    )
                    with patch.object(
                        vision_route._engine,
                        "process",
                        new=AsyncMock(return_value={
                            "description": "Text extracted from image",
                            "recognized_text": "x = 1",
                            "latex": "x = 1",
                            "status": "success",
                            "engine": "vision",
                        }),
                    ):
                        result = await vision_route.run_vision_ocr(
                            file=upload,
                            description="Read this equation",
                            conversation_id=conversation.id,
                            db=session,
                            user=User(id=1, email="audit@example.com", role="user"),
                        )
                    messages = (await session.execute(
                        select(Message).where(Message.conversation_id == conversation.id)
                    )).scalars().all()
                    self.assertEqual(result["conversation_id"], conversation.id)
                    self.assertEqual(len(messages), 2)
                    self.assertEqual(messages[-1].data["recognized_text"], "x = 1")
                await engine.dispose()

        asyncio.run(scenario())

    def test_concurrent_workers_have_one_queue_winner(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as directory:
                db_path = Path(directory) / "race.db"
                engine = create_async_engine(
                    f"sqlite+aiosqlite:///{db_path}",
                    connect_args={"timeout": 30},
                )
                async with engine.begin() as connection:
                    await connection.run_sync(Base.metadata.create_all)
                sessions = async_sessionmaker(engine, expire_on_commit=False)
                async with sessions() as session:
                    job = await enqueue(session, "noop", {"race": True})
                barrier = asyncio.Barrier(2)

                async def worker(worker_id: str):
                    async with sessions() as session:
                        await barrier.wait()
                        claimed = await claim(session, worker_id)
                        return claimed.id if claimed else None

                claims = await asyncio.gather(worker("worker-a"), worker("worker-b"))
                self.assertEqual(claims.count(job.id), 1)
                self.assertEqual(claims.count(None), 1)
                await engine.dispose()

        asyncio.run(scenario())


if __name__ == "__main__":
    unittest.main()
