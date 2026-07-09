from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from .connections import connections
from .routes.api import router as api_router

STATIC_DIR = Path(__file__).parent / "static" / "manager"


def create_app() -> FastAPI:
    app = FastAPI(title="EDB Gateway", version="1.0.0")
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
