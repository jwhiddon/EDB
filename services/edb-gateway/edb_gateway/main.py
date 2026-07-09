from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from starlette.middleware.trustedhost import TrustedHostMiddleware

from . import config
from .connections import connections
from .routes.api import router as api_router

STATIC_DIR = Path(__file__).parent / "static" / "manager"


def create_app() -> FastAPI:
    # Refuse to run open to the network without authentication: a non-loopback bind with no
    # API key would expose full device control to anyone who can reach the port.
    if not config.host_is_loopback() and not config.API_KEY:
        raise RuntimeError(
            "EDB_GATEWAY_HOST is non-loopback but EDB_GATEWAY_API_KEY is not set; "
            "refusing to start without authentication"
        )

    app = FastAPI(title="EDB Gateway", version="2.0.0")

    # DNS-rebinding defense: only serve requests whose Host header is allowlisted.
    app.add_middleware(TrustedHostMiddleware, allowed_hosts=config.ALLOWED_HOSTS)
    # CORS: explicit origin allowlist only (never "*"), so a hostile web page cannot drive
    # the local gateway from the browser.
    app.add_middleware(
        CORSMiddleware,
        allow_origins=config.ALLOWED_ORIGINS,
        allow_credentials=False,
        allow_methods=["GET", "POST", "PUT", "DELETE"],
        allow_headers=["X-API-Key", "Content-Type"],
    )

    app.include_router(api_router)

    if STATIC_DIR.is_dir():
        app.mount("/manager/static", StaticFiles(directory=STATIC_DIR), name="manager-static")

        @app.get("/manager")
        async def manager_index():
            return FileResponse(STATIC_DIR / "index.html")

    @app.on_event("shutdown")
    async def shutdown():
        for cid in list(connections._connections.keys()):
            await connections.close(cid)

    return app


app = create_app()
