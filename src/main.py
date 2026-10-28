from __future__ import annotations

import os
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError as FastAPIValidationError
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from starlette.exceptions import HTTPException as StarletteHTTPException

from src.api.routes import router
from src.core.config import ensure_storage_layout, resolve_cors_origins, settings
from src.core.errors import TrinityError
from src.core.logging_config import configure_logging, get_logger
from src.db.database import init_db
from src.engines.registry import bootstrap_engines, registry

configure_logging()
log = get_logger("main")


@asynccontextmanager
async def lifespan(app: FastAPI):
    ensure_storage_layout()
    init_db()
    bootstrap_engines()
    log.info(
        "trinity started",
        extra={"ctx": {"engines": [e["name"] for e in registry.list()]}},
    )
    yield


app = FastAPI(title=settings.api_title, version=settings.api_version, lifespan=lifespan)

# V1 dev CORS: the local frontend needs cross-origin access with zero setup.
# Gate (src/core/config.py: resolve_cors_origins): development may use the
# wildcard; TRINITY_ENV=production refuses wildcard/empty origins at startup
# unless TRINITY_CORS_ORIGINS lists explicit domains.
app.add_middleware(
    CORSMiddleware,
    allow_origins=resolve_cors_origins(settings.environment, settings.cors_origins),
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
    log.info(
        "trinity error", extra={"ctx": {"path": request.url.path, **exc.to_dict()}}
    )
    return JSONResponse(
        status_code=status_code, content={"success": False, "errors": [exc.to_dict()]}
    )


@app.exception_handler(FastAPIValidationError)
def fastapi_validation_handler(request: Request, exc: FastAPIValidationError) -> JSONResponse:
    err = {"code": "request_validation_error", "message": str(exc), "details": {"errors": exc.errors()}}
    return JSONResponse(status_code=422, content={"success": False, "errors": [err]})


@app.exception_handler(StarletteHTTPException)
def http_handler(request: Request, exc: StarletteHTTPException) -> JSONResponse:
    err = {"code": "http_error", "message": str(exc.detail), "details": {"status_code": exc.status_code}}
    return JSONResponse(status_code=exc.status_code, content={"success": False, "errors": [err]})


app.include_router(router)

# Packaged mode: TRINITY_UI_DIR points at the Next.js static export
# (frontend `out/`), which this process then serves at "/". That makes
# the shipped application a single Python process -- no Node runtime,
# no second port, and the UI's same-origin /api base just works. The
# mount is registered after the router so every API route keeps
# priority, and it is skipped when unset or the directory is missing
# (development never serves a half-present UI dir).
_ui_dir = os.getenv("TRINITY_UI_DIR", "")
if _ui_dir and Path(_ui_dir).is_dir():
    from fastapi.staticfiles import StaticFiles

    class _StaticHtml(StaticFiles):
        """StaticFiles(html=True) serves directory/index.html and
        404.html, but NOT /chat from chat.html — and Next.js exports
        every page as a standalone .html file. Retry with the .html
        suffix before giving a 404 so exported routes resolve."""

        async def get_response(self, path, scope):
            try:
                return await super().get_response(path, scope)
            except StarletteHTTPException as exc:
                if exc.status_code != 404 or not path or path.endswith(".html"):
                    raise
                return await super().get_response(path + ".html", scope)

    app.mount("/", _StaticHtml(directory=_ui_dir, html=True), name="ui")
    log.info("static ui served", extra={"ctx": {"dir": _ui_dir}})
