"""
Trinity AI Engineering OS — FastAPI Backend
Built on top of the trinity-ai GitHub repo foundation.
"""
from contextlib import asynccontextmanager
import logging
import os
import time
import uuid
from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from slowapi import Limiter, _rate_limit_exceeded_handler
from slowapi.util import get_remote_address
from slowapi.errors import RateLimitExceeded

from app.database import init_db
from app.routes.health import router as health_router
from app.routes.auth import router as auth_router
from app.routes.chat import router as chat_router
from app.routes.conversations import router as conversations_router
from app.routes.engines import router as engines_router
from app.routes.designs import router as designs_router
from app.routes.firmware import router as firmware_router
from app.routes.vision import router as vision_router
from app.routes.collab import router as collab_router
from app.routes.fusion import router as fusion_router
from app.routes.kicad import router as kicad_router
from app.routes.workflows import router as workflows_router
from app.routes.jobs import router as jobs_router
from app.observability import logger, metrics
from app.security import require_api_key_for_request
from app.auth import validate_auth_configuration
from app.config import get_settings

settings = get_settings()

limiter = Limiter(key_func=get_remote_address)


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Initialize DB tables on startup, clean up on shutdown."""
    print("🔺 Trinity AI starting up...")
    validate_auth_configuration()
    await init_db()
    print("✅ Database initialized")
    yield
    print("🔻 Trinity AI shutting down...")


_disable_docs = settings.disable_docs or os.getenv("TRINITY_DISABLE_DOCS", "0") == "1"

app = FastAPI(
    title="Trinity AI",
    description="Unified AI Engineering and Research Operating System",
    version="1.0.0",
    lifespan=lifespan,
    docs_url=None if _disable_docs else "/api/docs",
    redoc_url=None if _disable_docs else "/api/redoc",
    openapi_url=None if _disable_docs else "/api/openapi.json",
)

app.state.limiter = limiter
app.add_exception_handler(RateLimitExceeded, _rate_limit_exceeded_handler)

_raw_cors_origins = settings.cors_origins
if _raw_cors_origins == "*":
    _cors_origins = ["*"]
else:
    _cors_origins = [origin.strip() for origin in _raw_cors_origins.split(",") if origin.strip()]
if not _cors_origins:
    _cors_origins = ["*"]
app.add_middleware(
    CORSMiddleware,
    allow_origins=_cors_origins,
    allow_credentials="*" not in _cors_origins,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.middleware("http")
async def request_observability_middleware(request: Request, call_next):
    request_id = request.headers.get("x-request-id") or uuid.uuid4().hex
    request.state.request_id = request_id
    started = time.perf_counter()
    response = await call_next(request)
    elapsed = time.perf_counter() - started
    metrics.observe(request.method, request.url.path, response.status_code, elapsed)
    response.headers["X-Request-ID"] = request_id
    logger.info("http_request method=%s path=%s status=%s duration_ms=%.2f request_id=%s", request.method, request.url.path, response.status_code, elapsed * 1000, request_id)
    return response


@app.middleware("http")
async def api_key_middleware(request: Request, call_next):
    try:
        require_api_key_for_request(request)
    except Exception as exc:
        status_code = getattr(exc, "status_code", 401)
        detail = getattr(exc, "detail", "A valid Trinity API key is required")
        return JSONResponse(status_code=status_code, content={"detail": detail})
    return await call_next(request)

API = "/api"
app.include_router(health_router, prefix=API)
app.include_router(auth_router, prefix=API)
app.include_router(chat_router, prefix=API)
app.include_router(conversations_router, prefix=API)
app.include_router(engines_router, prefix=API)
app.include_router(designs_router, prefix=API)
app.include_router(firmware_router, prefix=API)
app.include_router(vision_router, prefix=API)
app.include_router(collab_router, prefix=API)
app.include_router(fusion_router, prefix=API)
app.include_router(kicad_router, prefix=API)
app.include_router(workflows_router, prefix=API)
app.include_router(jobs_router, prefix=API)


@app.get("/")
async def root():
    return {
        "system": "Trinity AI Engineering OS",
        "version": "1.0.0",
        "docs": "/api/docs" if not _disable_docs else "disabled",
        "engines": [
            "math", "quantum", "maker_cad", "maker_pcb",
            "literature", "vision", "firmware", "collab", "orchestrator",
        ],
    }
