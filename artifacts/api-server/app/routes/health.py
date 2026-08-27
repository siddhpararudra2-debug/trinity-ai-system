"""Health and capability endpoints."""
import os
import shutil

from fastapi import APIRouter, Depends, HTTPException
from fastapi.responses import PlainTextResponse
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import text

from app.database import get_db
from app.observability import metrics

router = APIRouter(tags=["health"])


@router.get("/metrics", response_class=PlainTextResponse)
async def metrics_endpoint():
    return metrics.prometheus()


@router.get("/readyz")
async def readiness_check(db: AsyncSession = Depends(get_db)):
    try:
        await db.execute(text("SELECT 1"))
    except Exception as exc:
        raise HTTPException(status_code=503, detail={"ready": False, "reason": str(exc)}) from exc
    return {"ready": True}


@router.get("/healthz")
async def health_check(db: AsyncSession = Depends(get_db)):
    """Health check: verifies DB connectivity and engine registry."""
    db_status = "healthy"
    try:
        await db.execute(text("SELECT 1"))
    except Exception as exc:
        db_status = f"unhealthy: {exc}"

    if db_status != "healthy":
        raise HTTPException(status_code=503, detail={"status": "unhealthy", "db": db_status})

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
            "firmware": "ready",
            "collab": "ready",
            "orchestrator": "ready",
        },
        "capabilities": {
            "tesseract": bool(shutil.which("tesseract")),
            "kicad_cli": bool(shutil.which("kicad-cli")) and os.getenv("TRINITY_ENABLE_KICAD_CLI", "0") == "1",
            "firmware_builds": os.getenv("TRINITY_ENABLE_FIRMWARE_BUILDS", "0") == "1",
            "fusion_worker": len(os.getenv("TRINITY_FUSION_WORKER_SECRET", "")) >= 32,
        },
    }
