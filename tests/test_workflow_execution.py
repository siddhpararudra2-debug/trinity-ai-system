from __future__ import annotations

import asyncio
import unittest
from tempfile import TemporaryDirectory

from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine

from app.database import Base
from app.models import User
from app.routes.workflows import WorkflowExecuteRequest, approve_workflow, execute_workflow, get_workflow_run


class WorkflowExecutionTests(unittest.TestCase):
    def test_approval_gates_queue_creation(self):
        async def scenario():
            with TemporaryDirectory() as directory:
                engine = create_async_engine(f"sqlite+aiosqlite:///{directory}/workflow.db")
                async with engine.begin() as connection:
                    await connection.run_sync(Base.metadata.create_all)
                sessions = async_sessionmaker(engine, expire_on_commit=False)
                async with sessions() as db:
                    user = User(id=1, email="workflow@example.com", role="user")
                    response = await execute_workflow(WorkflowExecuteRequest(objective="make a PCB and firmware"), db, user)
                    self.assertEqual(response["status"], "awaiting_approval")
                    self.assertIsNone(response["queue_job_id"])
                    approved = await approve_workflow(response["run_id"], db, user)
                    self.assertEqual(approved["status"], "queued")
                    self.assertIsNotNone(approved["queue_job_id"])
                    current = await get_workflow_run(response["run_id"], db, user)
                    self.assertEqual(current["queue"]["status"], "queued")
                await engine.dispose()

        asyncio.run(scenario())


if __name__ == "__main__":
    unittest.main()

