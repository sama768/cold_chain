"""FastAPI application entry point.

Run locally:
    python -m uvicorn app.main:app --reload --host 0.0.0.0 --port 8000   (from backend/)
    python -m app.main   (from backend/, with ENABLE_HTTPS and TLS paths configured)

The HTTP API remains on port 8000. HTTPS is a separate listener on port 8443 that reuses the same app.
API docs: http://localhost:8000/docs
"""
import logging
from pathlib import Path

import psycopg
from fastapi import Depends, FastAPI, Request
from fastapi.responses import JSONResponse
from fastapi.middleware.cors import CORSMiddleware

from . import config
from .db import get_repo
from .routers import telemetry

app = FastAPI(title="Cold-Chain Warehouse Backend", version="0.1.0")

# The React dashboard runs on a different port during development.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:3000", "http://localhost:5173"],
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(telemetry.router)

log = logging.getLogger("app")


def _validate_tls_file(path: str, label: str) -> None:
    tls_path = Path(path)
    if not tls_path.is_file():
        raise RuntimeError(f"{label} file does not exist or is not a regular file: {tls_path}")
    try:
        with tls_path.open("rb"):
            pass
    except OSError as exc:
        raise RuntimeError(f"{label} file is not readable: {tls_path}") from exc


@app.exception_handler(psycopg.OperationalError)
def database_unavailable(request: Request, exc: psycopg.OperationalError):
    log.error("Database unavailable: %s", str(exc).splitlines()[0])
    return JSONResponse(
        status_code=503,
        content={"detail": "Database unavailable. Check that PostgreSQL is running and DATABASE_URL is correct."},
    )


@app.get("/health", tags=["health"])
def health(repo=Depends(get_repo)):
    try:
        repo.ping()
        database = "ok"
    except Exception:
        database = "unreachable"
    return {"status": "ok", "database": database}


def run_https() -> None:
    """Start a separate HTTPS listener on the configured TLS port.

    This preserves the existing HTTP API on port 8000. The HTTPS endpoint reuses the same
    FastAPI app and routes; it simply binds a second uvicorn instance with TLS enabled.
    """
    if not config.ENABLE_HTTPS:
        raise RuntimeError("HTTPS is disabled. Set ENABLE_HTTPS=true and configure SSL_CERTFILE/SSL_KEYFILE.")
    if not config.SSL_CERTFILE or not config.SSL_KEYFILE:
        raise RuntimeError("SSL_CERTFILE and SSL_KEYFILE must be configured before enabling HTTPS.")
    _validate_tls_file(config.SSL_CERTFILE, "SSL_CERTFILE")
    _validate_tls_file(config.SSL_KEYFILE, "SSL_KEYFILE")

    import uvicorn
    uvicorn.run(
        app,
        host=config.HTTPS_HOST,
        port=config.HTTPS_PORT,
        ssl_certfile=config.SSL_CERTFILE,
        ssl_keyfile=config.SSL_KEYFILE,
    )


if __name__ == "__main__":
    run_https()
elif config.ENABLE_HTTPS:
    log.warning(
        "ENABLE_HTTPS=true, but this HTTP Uvicorn process does not start the HTTPS listener; "
        "start it separately with 'python -m app.main' from backend/."
    )
