from __future__ import annotations

import os
from contextlib import asynccontextmanager
from pathlib import Path
from dotenv import load_dotenv

# Load .env.dummy from the repository root
# main.py is at src/main.py, so parents[1] = project root
_PROJECT_ROOT = Path(__file__).resolve().parents[1]
load_dotenv(str(_PROJECT_ROOT / ".env.dummy"))

from fastapi import FastAPI, Request  # noqa: E402 - dotenv must load before application imports
from fastapi.exceptions import RequestValidationError as FastAPIValidationError  # noqa: E402 - dotenv must load before application imports
from fastapi.middleware.cors import CORSMiddleware  # noqa: E402 - dotenv must load before application imports
from fastapi.responses import JSONResponse  # noqa: E402 - dotenv must load before application imports
from starlette.exceptions import HTTPException as StarletteHTTPException  # noqa: E402 - dotenv must load before application imports

from src.api.routes import router  # noqa: E402 - dotenv must load before application imports
from src.core.config import ensure_storage_layout, resolve_cors_origins, settings  # noqa: E402 - dotenv must load before application imports
from src.core.data_schema import apply_data_schema  # noqa: E402 - dotenv must load before application imports
from src.core.errors import TrinityError  # noqa: E402 - dotenv must load before application imports
from src.core.logging_config import configure_logging, get_logger  # noqa: E402 - dotenv must load before application imports
from src.db.database import init_db  # noqa: E402 - dotenv must load before application imports
from src.engines.registry import bootstrap_engines, registry  # noqa: E402 - dotenv must load before application imports

configure_logging()
log = get_logger("main")


@asynccontextmanager
async def lifespan(app: FastAPI):
    ensure_storage_layout()
    # M17: refuse a storage root written by a newer Trinity before the
    # database is touched; migration 0 -> 1 then applies the v1 schema.
    apply_data_schema()
    # Still idempotent on every start: a marker at the current version
    # with a missing DB file is recreated rather than failing later.
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
        # M30 accounts: auth failures are the caller's fault, conflicts
        # are 409, and a locked account is 423 (Locked) with retry_after.
        "auth_error": 401,
        "account_conflict": 409,
        "account_locked": 423,
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

# M30 accounts: signup/verify/login/refresh/logout/reset/me under
# /api/accounts/*; /api/login itself stays in routes.py for path
# stability and delegates to the same implementation.
from src.api.accounts import router as accounts_router  # noqa: E402

app.include_router(accounts_router)

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
        """StaticFiles(html=True) serves directory/index.html and, when
        404.html exists (Next.js exports one), returns a 404 FileResponse
        for unknown paths — it never maps /chat to chat.html, and Next.js
        exports every page as a standalone .html file. So inspect the
        produced response: on a 404 for an extensionless path, retry with
        the .html suffix and take it only when it succeeds."""

        async def get_response(self, path, scope):
            response = await super().get_response(path, scope)
            if response.status_code == 404 and path and not path.endswith(".html"):
                retry = await super().get_response(path + ".html", scope)
                if retry.status_code == 200:
                    return retry
            return response

    app.mount("/", _StaticHtml(directory=_ui_dir, html=True), name="ui")
    log.info("static ui served", extra={"ctx": {"dir": _ui_dir}})
