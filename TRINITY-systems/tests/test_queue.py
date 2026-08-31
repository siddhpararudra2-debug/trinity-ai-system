from __future__ import annotations

import asyncio
import unittest
from tempfile import TemporaryDirectory

from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine

from app.database import Base
from app.job_queue import cancel, claim, complete, enqueue


class DurableQueueTests(unittest.TestCase):
    def test_queue_claim_retry_and_cancel(self):
        async def scenario():
            with TemporaryDirectory() as directory:
                engine = create_async_engine(f"sqlite+aiosqlite:///{directory}/jobs.db")
                async with engine.begin() as connection:
                    await connection.run_sync(Base.metadata.create_all)
                sessions = async_sessionmaker(engine, expire_on_commit=False)
                async with sessions() as db:
                    job = await enqueue(db, "noop", {"value": 1}, owner_id=11)
                    self.assertEqual(job.status, "queued")
                    claimed = await claim(db, "test-worker", lease_seconds=10)
                    self.assertEqual(claimed.id, job.id)
                    retried = await complete(db, job.id, "test-worker", False, "temporary failure")
                    self.assertEqual(retried.status, "queued")
                    cancelled = await cancel(db, job.id, owner_id=11)
                    self.assertEqual(cancelled.status, "cancelled")
                await engine.dispose()

        asyncio.run(scenario())


if __name__ == "__main__":
    unittest.main()

