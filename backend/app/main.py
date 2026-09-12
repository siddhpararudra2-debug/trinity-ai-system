from __future__ import annotations

from contextlib import asynccontextmanager

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

from app.api.routes import router
from app.core.config import ensure_storage_layout, settings
from app.core.errors import TrinityError
from app.core.logging_config import configure_logging, get_logger
from app.db.database import init_db
from app.engines.registry import bootstrap_engines, registry

configure_logging()
log = get_logger("main")


@asynccontextmanager
async def lifespan(app: FastAPI):
    ensure_storage_layout()
    init_db()
    bootstrap_engines()
    log.info("trinity started", extra={"ctx": {"engines": [e["name"] for e in registry.list()]}})
    yield


app = FastAPI(title=settings.api_title, version=settings.api_version, lifespan=lifespan)

# V1 dev-friendly CORS: the Trinity frontend (Vite, typically :5173) needs
# to call this API from a different origin during local development.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.exception_handler(TrinityError)
def trinity_error_handler(request: Request, exc: TrinityError) -> JSONResponse:
    status_map = {
        "request_validation_error": 422,
        "engine_not_found": 404,
        "job_not_found": 404,
        "artifact_not_found": 404,
        "geometry_validation_error": 422,
        "engine_execution_error": 400,
        "capability_unavailable": 501,
    }
    status_code = status_map.get(exc.code, 500)
    log.info("trinity error", extra={"ctx": {"path": str(request.url), **exc.to_dict()}})
    return JSONResponse(status_code=status_code, content={"success": False, "errors": [exc.to_dict()]})


app.include_router(router)
