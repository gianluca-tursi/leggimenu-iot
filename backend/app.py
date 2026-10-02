"""Backend leggimenu IoT: hub fra i nodi tavolo (e-ink + sensori) e la cassa.

Avvio:  .venv/bin/python -m uvicorn app:app --host 0.0.0.0 --port 8000 --app-dir backend
"""
from __future__ import annotations

import asyncio
import base64
import json
import os
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse, Response
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from models import LetturaSensori, StatoPiatto
from state import Sala

RADICE = Path(__file__).resolve().parent.parent
DASHBOARD = RADICE / "dashboard"
BASE_URL = os.environ.get("LEGGIMENU_BASE_URL", "http://localhost:8000")

sala = Sala(base_url=BASE_URL)
sala.aggiungi_tavolo("T01", "Tavolo 1", posti=2)
sala.aggiungi_tavolo("T07", "Tavolo 7", posti=4)
sala.aggiungi_tavolo("T12", "Tavolo 12", posti=6, sala="Dehors")

# ultima anteprima e-ink ricevuta da ogni nodo (PNG grezzo)
anteprime: dict[str, bytes] = {}


class Hub:
    """Tiene aperte le websocket di dashboard e nodi e smista i messaggi."""

    def __init__(self) -> None:
        self.dashboard: set[WebSocket] = set()
        self.device: dict[str, WebSocket] = {}
        self.coda: asyncio.Queue[dict[str, Any]] = asyncio.Queue()

    async def broadcast_dashboard(self, msg: dict[str, Any]) -> None:
        morti = []
        testo = json.dumps(msg, default=str)
        for ws in list(self.dashboard):
            try:
                await ws.send_text(testo)
            except Exception:
                morti.append(ws)
        for ws in morti:
            self.dashboard.discard(ws)

    async def push_device(self, tavolo_id: str) -> None:
        ws = self.device.get(tavolo_id)
        if ws is None:
            return
        try:
            await ws.send_text(json.dumps(
                {"tipo": "display", "vista": sala.vista_display(tavolo_id)}, default=str))
        except Exception:
            self.device.pop(tavolo_id, None)


hub = Hub()


def _on_change(tavolo_id: str, payload: dict[str, Any]) -> None:
    hub.coda.put_nowait({"tipo": "tavolo", "tavolo": payload,
                         "eventi": list(sala.eventi)[:20]})


sala.on_change(_on_change)


async def _pompa() -> None:
    """Drena la coda dei cambiamenti: aggiorna dashboard e nodo interessato."""
    while True:
        msg = await hub.coda.get()
        await hub.broadcast_dashboard(msg)
        tid = msg.get("tavolo", {}).get("id")
        if tid:
            await hub.push_device(tid)


async def _orologio() -> None:
    while True:
        await asyncio.sleep(2)
        sala.tick()


@asynccontextmanager
async def lifespan(app: FastAPI):
    tasks = [asyncio.create_task(_pompa()), asyncio.create_task(_orologio())]
    yield
    for t in tasks:
        t.cancel()


app = FastAPI(title="leggimenu IoT hub", lifespan=lifespan)


# --- API cassa ------------------------------------------------------------
class Coperti(BaseModel):
    coperti: int


class Ordine(BaseModel):
    piatto_id: str
    quantita: int = 1
    origine: str = "cassa"


class StatoRiga(BaseModel):
    stato: str


class Sensori(BaseModel):
    movimento: bool = False
    presenza: bool = False
    target: int = 0
    distanze_cm: list[float] = []


@app.get("/api/stato")
async def api_stato():
    return sala.snapshot()


@app.post("/api/tavoli/{tid}/apri")
async def api_apri(tid: str, body: Coperti | None = None):
    return _ok(sala.apri(tid, coperti=body.coperti if body else None))


@app.post("/api/tavoli/{tid}/coperti")
async def api_coperti(tid: str, body: Coperti):
    return _ok(sala.conferma_coperti(tid, body.coperti))


@app.post("/api/tavoli/{tid}/ordina")
async def api_ordina(tid: str, body: Ordine):
    try:
        return _ok(sala.ordina(tid, body.piatto_id, body.quantita, body.origine))
    except KeyError as e:
        raise HTTPException(404, str(e))


@app.post("/api/tavoli/{tid}/righe/{riga_id}")
async def api_riga(tid: str, riga_id: str, body: StatoRiga):
    try:
        return _ok(sala.stato_riga(tid, riga_id, StatoPiatto(body.stato)))
    except (KeyError, ValueError) as e:
        raise HTTPException(400, str(e))


@app.post("/api/tavoli/{tid}/vino")
async def api_vino(tid: str, accetta: bool = True):
    return _ok(sala.accetta_vino(tid, accetta))


@app.post("/api/tavoli/{tid}/conto")
async def api_conto(tid: str):
    return _ok(sala.chiedi_conto(tid))


@app.post("/api/tavoli/{tid}/chiudi")
async def api_chiudi(tid: str):
    return _ok(sala.chiudi(tid))


@app.post("/api/tavoli/{tid}/sensori")
async def api_sensori(tid: str, body: Sensori):
    """Ingresso HTTP per i sensori: comodo per test e per nodi senza websocket."""
    return _ok(sala.sensori(tid, LetturaSensori(**body.model_dump())))


@app.get("/api/tavoli/{tid}/display")
async def api_display(tid: str):
    try:
        return sala.vista_display(tid)
    except KeyError as e:
        raise HTTPException(404, str(e))


@app.get("/api/tavoli/{tid}/anteprima.png")
async def api_anteprima(tid: str):
    png = anteprime.get(tid)
    if not png:
        raise HTTPException(404, "nessuna anteprima dal nodo")
    return Response(png, media_type="image/png",
                    headers={"Cache-Control": "no-store"})


def _ok(tavolo) -> JSONResponse:
    return JSONResponse(tavolo.to_dict())


@app.exception_handler(KeyError)
async def _key_error(request, exc: KeyError):
    return JSONResponse({"errore": str(exc)}, status_code=404)


@app.get("/api/sessione/{sessione}")
async def api_sessione(sessione: str):
    """Risolve il QR: dalla sessione al tavolo, col menu per il cliente."""
    t = next((x for x in sala.tavoli.values()
              if x.sessione_id == sessione or x.id == sessione), None)
    if t is None:
        raise HTTPException(404, "sessione non valida o tavolo gia' chiuso")
    return {"tavolo": t.to_dict(), "menu": sala.menu, "vini": sala.vini}


# --- websocket dashboard ---------------------------------------------------
@app.websocket("/ws/dashboard")
async def ws_dashboard(ws: WebSocket):
    await ws.accept()
    hub.dashboard.add(ws)
    await ws.send_text(json.dumps({"tipo": "snapshot", **sala.snapshot()}, default=str))
    try:
        while True:
            await ws.receive_text()   # la dashboard scrive via REST, qui solo keepalive
    except WebSocketDisconnect:
        pass
    finally:
        hub.dashboard.discard(ws)


# --- websocket nodo tavolo -------------------------------------------------
@app.websocket("/ws/device/{tid}")
async def ws_device(ws: WebSocket, tid: str):
    if tid not in sala.tavoli:
        await ws.close(code=4404)
        return
    await ws.accept()
    hub.device[tid] = ws
    sala.heartbeat(tid, True)
    await ws.send_text(json.dumps({"tipo": "display", "vista": sala.vista_display(tid)}, default=str))
    try:
        while True:
            msg = json.loads(await ws.receive_text())
            tipo = msg.get("tipo")
            if tipo == "sensori":
                sala.sensori(tid, LetturaSensori(
                    movimento=bool(msg.get("movimento")),
                    presenza=bool(msg.get("presenza")),
                    target=int(msg.get("target", 0)),
                    distanze_cm=list(msg.get("distanze_cm", [])),
                ))
            elif tipo == "heartbeat":
                sala.heartbeat(tid, True)
            elif tipo == "anteprima":
                anteprime[tid] = base64.b64decode(msg["png_b64"])
                await hub.broadcast_dashboard({"tipo": "anteprima", "tavolo_id": tid})
            elif tipo == "hello":
                sala.log(tid, f"{sala.get(tid).nome}: nodo connesso ({msg.get('fw', 'n/d')})")
                sala.heartbeat(tid, True)
    except WebSocketDisconnect:
        pass
    finally:
        if hub.device.get(tid) is ws:
            hub.device.pop(tid, None)
        sala.heartbeat(tid, False)


# --- pagine ---------------------------------------------------------------
@app.get("/m/{sessione}")
async def pagina_menu(sessione: str):
    return FileResponse(DASHBOARD / "menu.html")


app.mount("/", StaticFiles(directory=DASHBOARD, html=True), name="dashboard")
