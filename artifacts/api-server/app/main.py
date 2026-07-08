"""
Trinity AI Engineering OS — FastAPI Backend
Built on top of the trinity-ai GitHub repo foundation.
"""
from contextlib import asynccontextmanager
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.database import init_db
from app.routes.health import router as health_router
from app.routes.chat import router as chat_router
from app.routes.conversations import router as conversations_router
from app.routes.engines import router as engines_router


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

# All routes are prefixed with /api because the reverse proxy routes
# /api/* traffic to this service without stripping the prefix.
API = "/api"
app.include_router(health_router, prefix=API)
app.include_router(chat_router, prefix=API)
app.include_router(conversations_router, prefix=API)
app.include_router(engines_router, prefix=API)


@app.get("/")
async def root():
    return {
        "system": "Trinity AI Engineering OS",
        "version": "1.0.0",
        "docs": "/api/docs",
        "engines": [
            "math", "quantum", "maker_cad", "maker_pcb",
            "literature", "vision", "collab", "orchestrator",
        ],
    }
