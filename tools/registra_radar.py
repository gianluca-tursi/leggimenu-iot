#!/usr/bin/env python3
"""Registra una sessione del radar dal nodo e la riassume.

    .venv/bin/python tools/registra_radar.py 60 radar.local 3

Il terzo parametro e' QUANTE PERSONE ci sono davvero: e' la verita' con cui
poi confrontiamo le stime. Senza quella non si puo' migliorare niente.

Salva in registrazioni/<data>-<N>persone.jsonl.
"""
import asyncio
import json
import sys
import time
from collections import Counter
from datetime import datetime
from pathlib import Path

import websockets

SECONDI = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
HOST = sys.argv[2] if len(sys.argv) > 2 else "radar.local"
VERE = int(sys.argv[3]) if len(sys.argv) > 3 else -1      # persone realmente presenti
CARTELLA = Path(__file__).parent.parent / "registrazioni"


async def main() -> None:
    CARTELLA.mkdir(exist_ok=True)
    etichetta = f"-{VERE}persone" if VERE >= 0 else "-senzaetichetta"
    nome = CARTELLA / f"{datetime.now():%Y%m%d-%H%M%S}{etichetta}.jsonl"
    if VERE < 0:
        print("⚠️  non hai detto quante persone ci sono: la registrazione")
        print("   servira' solo a guardarla, non a tarare l'algoritmo.")
        print("   Usa:  registra_radar.py <secondi> <host> <numero-persone>\n")
    url = f"ws://{HOST}:81/"
    print(f"registro da {url} per {SECONDI:.0f}s  ->  {nome.name}")
    print("muoviti pure, il riassunto esce alla fine.\n")

    conteggi = Counter()
    dist: list[float] = []
    ang: list[float] = []
    buchi, buco_da, buco_max = 0, None, 0.0
    frame = 0

    try:
        async with websockets.connect(url, open_timeout=6) as ws, nome.open("w") as f:
            f.write(json.dumps({"tipo": "meta", "persone_vere": VERE,
                                "secondi": SECONDI, "host": HOST,
                                "quando": datetime.now().isoformat()}) + "\n")
            fine = time.time() + SECONDI
            while time.time() < fine:
                try:
                    grezzo = await asyncio.wait_for(ws.recv(), timeout=3)
                except asyncio.TimeoutError:
                    print("  ...nessun dato da 3 secondi")
                    continue
                m = json.loads(grezzo)
                if m.get("tipo") != "frame":
                    continue
                m["ts"] = time.time()
                f.write(json.dumps(m) + "\n")
                frame += 1
                n = m.get("n", 0)
                conteggi[n] += 1
                ora = time.time()
                if n == 0 and buco_da is None:
                    buco_da = ora
                elif n > 0 and buco_da is not None:
                    d = ora - buco_da
                    buchi += 1
                    buco_max = max(buco_max, d)
                    buco_da = None
                for t in m.get("bersagli", []):
                    dist.append(t["dist"])
                    ang.append(t["ang"])
    except Exception as e:
        print(f"connessione fallita: {e}")
        print(f"prova con l'IP numerico invece di {HOST}")
        return

    tot = sum(conteggi.values()) or 1
    print("\n" + "=" * 62)
    print(f"frame registrati: {frame}   ({frame/SECONDI:.1f}/s)")
    print("-" * 62)
    print("QUANTE PERSONE HA VISTO")
    for n in sorted(conteggi):
        q = conteggi[n] * 100 / tot
        print(f"  {n}  {conteggi[n]:5d} letture  {q:5.1f}%  {'#' * int(q / 2)}")
    if dist:
        print("-" * 62)
        print(f"distanza  min {min(dist):.0f}  max {max(dist):.0f}  media {sum(dist)/len(dist):.0f} cm")
        print(f"angolo    da {min(ang):+.0f}°  a  {max(ang):+.0f}°")
    print("-" * 62)
    print(f"buchi: {buchi}   il piu' lungo: {buco_max:.1f} s")
    if VERE >= 0:
        esatte = conteggi.get(VERE, 0) * 100 / tot
        print("-" * 62)
        print(f"PERSONE VERE: {VERE}   letture esatte: {esatte:.1f}%")
    print(f"\nsalvato in {nome}")
    print("=" * 62)


if __name__ == "__main__":
    asyncio.run(main())
