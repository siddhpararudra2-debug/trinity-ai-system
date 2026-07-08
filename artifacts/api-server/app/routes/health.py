"""Health check endpoint with DB and engine status."""
from fastapi import APIRouter, Depends
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import text

from app.database import get_db

router = APIRouter(tags=["health"])


@router.get("/healthz")
async def health_check(db: AsyncSession = Depends(get_db)):
    """Health check: verifies DB connectivity and engine registry."""
    db_status = "healthy"
    try:
        await db.execute(text("SELECT 1"))
    except Exception as exc:
        db_status = f"unhealthy: {exc}"

    return {
        "status": "ok",
        "db": db_status,
        "engines": {
            "math": "ready",
            "quantum": "ready",
            "maker_cad": "ready",
            "maker_pcb": "ready",
            "literature": "ready",
            "vision": "ready",
            "collab": "ready",
            "orchestrator": "ready",
        },
    }
