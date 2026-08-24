"""
Trinity AI Engineering OS — FastAPI Backend
Built on top of the trinity-ai GitHub repo foundation.
"""
from contextlib import asynccontextmanager
from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

from app.database import init_db
from app.routes.health import router as health_router
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
from app.security import require_api_key_for_request


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Initialize DB tables on startup, clean up on shutdown."""
    print("🔺 Trinity AI starting up...")
    await init_db()
    print("✅ Database initialized")
    yield
    print("🔻 Trinity AI shutting down...")


app = FastAPI(
    title="Trinity AI",
    description="Unified AI Engineering and Research Operating System",
    version="1.0.0",
    lifespan=lifespan,
    docs_url="/api/docs",
    redoc_url="/api/redoc",
    openapi_url="/api/openapi.json",
)

# CORS — allow all origins in development
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.middleware("http")
async def api_key_middleware(request: Request, call_next):
    try:
        require_api_key_for_request(request)
    except Exception as exc:
        status_code = getattr(exc, "status_code", 401)
        detail = getattr(exc, "detail", "A valid Trinity API key is required")
        return JSONResponse(status_code=status_code, content={"detail": detail})
    return await call_next(request)

# All routes are prefixed with /api because the reverse proxy routes
# /api/* traffic to this service without stripping the prefix.
API = "/api"
app.include_router(health_router, prefix=API)
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


@app.get("/")
async def root():
    return {
        "system": "Trinity AI Engineering OS",
        "version": "1.0.0",
        "docs": "/api/docs",
        "engines": [
            "math", "quantum", "maker_cad", "maker_pcb",
            "literature", "vision", "firmware", "collab", "orchestrator",
        ],
    }
