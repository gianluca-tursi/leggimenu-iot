#!/usr/bin/env python3
"""Agent del nodo tavolo: sensori -> server, view-model -> e-ink.

Sul Mac (senza hardware):
    .venv/bin/python device/agent.py --tavolo T07 --server http://localhost:8000
Sul Raspberry Pi Zero 2 W:
    python3 device/agent.py --config /home/pi/leggimenu/config.json
"""
from __future__ import annotations

import argparse
import asyncio
import base64
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import websockets

import display as display_mod
import render
import sensors

VERSIONE = "nodo-tavolo 0.1"


def carica_config(args) -> dict:
    cfg: dict = {
        "tavolo": "T07",
        "server": "http://localhost:8000",
        "display": {"tipo": "png", "larghezza": 400, "altezza": 300,
                    "percorso": "/tmp/leggimenu-eink.png"},
        "sensori": {"intervallo_s": 0.5, "pir": {"attivo": False}, "ld2450": {"attivo": False}},
    }
    if args.config:
        cfg.update(json.loads(Path(args.config).read_text(encoding="utf-8")))
    if args.tavolo:
        cfg["tavolo"] = args.tavolo
    if args.server:
        cfg["server"] = args.server
    if args.display:
        cfg["display"]["tipo"] = args.display
    if args.panel:
        w, h = args.panel.lower().split("x")
        cfg["display"]["larghezza"], cfg["display"]["altezza"] = int(w), int(h)
    return cfg


class Nodo:
    def __init__(self, cfg: dict) -> None:
        self.cfg = cfg
        self.tavolo = cfg["tavolo"]
        d = cfg["display"]
        self.w, self.h = d.get("larghezza", 400), d.get("altezza", 300)
        self.schermo = display_mod.costruisci(d.get("tipo", "png"),
                                              modello=d.get("modello", "epd4in2_V2"),
                                              percorso=d.get("percorso", "/tmp/leggimenu-eink.png"))
        self.sensori = sensors.costruisci(cfg.get("sensori", {}))
        self.ultima_rev: int | None = None
        self.ultima_lettura: sensors.Lettura | None = None

    @property
    def ws_url(self) -> str:
        base = self.cfg["server"].replace("https://", "wss://").replace("http://", "ws://")
        return f"{base.rstrip('/')}/ws/device/{self.tavolo}"

    # --- disegno ----------------------------------------------------------
    async def aggiorna_schermo(self, ws, vista: dict) -> None:
        rev = vista.get("rev")
        if rev == self.ultima_rev:
            return
        prima_volta = self.ultima_rev is None
        self.ultima_rev = rev

        img = render.disegna(vista, self.w, self.h)
        # il refresh completo lampeggia: lo uso solo al primo disegno
        await asyncio.to_thread(self.schermo.mostra, img, not prima_volta)
        print(f"[display] schermata '{vista.get('schermata')}' rev={rev}")

        png = render.png_bytes(img)
        await ws.send(json.dumps({"tipo": "anteprima",
                                  "png_b64": base64.b64encode(png).decode()}))

    # --- cicli ------------------------------------------------------------
    async def ciclo_sensori(self, ws) -> None:
        intervallo = self.cfg.get("sensori", {}).get("intervallo_s", 0.5)
        da_quando = 0.0
        while True:
            lettura = await asyncio.to_thread(sensors.leggi_tutti, self.sensori)
            if lettura is not None:
                cambiata = (self.ultima_lettura is None or
                            lettura.movimento != self.ultima_lettura.movimento or
                            lettura.presenza != self.ultima_lettura.presenza or
                            lettura.target != self.ultima_lettura.target)
                if cambiata or da_quando >= 5.0:
                    await ws.send(json.dumps({
                        "tipo": "sensori",
                        "movimento": lettura.movimento,
                        "presenza": lettura.presenza,
                        "target": lettura.target,
                        "distanze_cm": lettura.distanze_cm,
                    }))
                    self.ultima_lettura = lettura
                    da_quando = 0.0
            da_quando += intervallo
            await asyncio.sleep(intervallo)

    async def ciclo_heartbeat(self, ws) -> None:
        while True:
            await asyncio.sleep(10)
            await ws.send(json.dumps({"tipo": "heartbeat"}))

    async def sessione(self) -> None:
        async with websockets.connect(self.ws_url, ping_interval=20) as ws:
            print(f"[nodo] connesso a {self.ws_url}")
            await ws.send(json.dumps({"tipo": "hello", "fw": VERSIONE,
                                      "display": {"w": self.w, "h": self.h}}))
            self.ultima_rev = None
            tasks = [asyncio.create_task(self.ciclo_sensori(ws)),
                     asyncio.create_task(self.ciclo_heartbeat(ws))]
            try:
                async for grezzo in ws:
                    msg = json.loads(grezzo)
                    if msg.get("tipo") == "display":
                        await self.aggiorna_schermo(ws, msg["vista"])
            finally:
                for t in tasks:
                    t.cancel()

    async def esegui(self) -> None:
        attesa = 1.0
        while True:
            try:
                await self.sessione()
                attesa = 1.0
            except Exception as e:
                print(f"[nodo] connessione persa ({e}); riprovo fra {attesa:.0f}s")
            await asyncio.sleep(attesa)
            attesa = min(attesa * 2, 30.0)


def main() -> None:
    p = argparse.ArgumentParser(description="Nodo tavolo leggimenu")
    p.add_argument("--config", help="file json di configurazione")
    p.add_argument("--tavolo", help="id tavolo, es. T07")
    p.add_argument("--server", help="url del backend, es. http://192.168.1.50:8000")
    p.add_argument("--display", choices=["png", "waveshare"], help="driver del pannello")
    p.add_argument("--panel", help="risoluzione pannello, es. 400x300")
    args = p.parse_args()

    nodo = Nodo(carica_config(args))
    try:
        asyncio.run(nodo.esegui())
    except KeyboardInterrupt:
        nodo.schermo.dormi()
        print("\n[nodo] arrivederci")


if __name__ == "__main__":
    main()
