from __future__ import annotations

from fastapi import APIRouter
from pydantic import BaseModel, Field

from app.workflows import build_workflow_plan, plan_to_dict

router = APIRouter(tags=["workflows"])


class WorkflowPlanRequest(BaseModel):
    objective: str = Field(min_length=3, max_length=4000)


@router.post("/workflows/plan")
async def create_workflow_plan(request: WorkflowPlanRequest):
    return plan_to_dict(build_workflow_plan(request.objective))
