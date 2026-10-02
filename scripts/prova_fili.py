#!/usr/bin/env python3
"""Stato del radar in tempo reale, per trovare il filo che non fa contatto.

    .venv/bin/python scripts/prova_fili.py

Si appoggia alla dashboard (non tocca la seriale, quindi girano insieme).
Muovi UN filo alla volta, delicatamente, e guarda quale fa cadere il radar.
"""
import asyncio
import json
import sys
import time

import websockets

URL = "ws://localhost:8080/ws"


async def main() -> None:
    print("Stato del radar, una riga al secondo. Ctrl-C per uscire.\n")
    print("  Muovi UN filo alla volta:  1) 3V3   2) GND   3) IO40")
    print("  Quello che fa passare la riga da OK a MUTO e' il colpevole.\n")

    vivo = None
    cadute = 0
    ultimo_byte = None
    ultimo_cambio = time.time()

    try:
        async with websockets.connect(URL, open_timeout=6) as ws:
            while True:
                try:
                    m = json.loads(await asyncio.wait_for(ws.recv(), timeout=4))
                except asyncio.TimeoutError:
                    print("  … nessun dato dalla dashboard")
                    continue
                if m.get("tipo") != "diagnosi":
                    continue

                # preferisco il flag del firmware; se manca, guardo se il
                # contatore avanza (il totale non torna mai a zero)
                if "radar" in m:
                    ora_vivo = bool(m["radar"])
                else:
                    ora_vivo = ultimo_byte is not None and m["byte"] > ultimo_byte
                ultimo_byte = m["byte"]

                if vivo is None:
                    vivo = ora_vivo
                elif ora_vivo != vivo:
                    vivo = ora_vivo
                    if not vivo:
                        cadute += 1
                    durata = time.time() - ultimo_cambio
                    ultimo_cambio = time.time()
                    print(f"\n  >>> CAMBIO: ora {'OK' if vivo else 'MUTO'} "
                          f"(lo stato precedente e' durato {durata:.0f}s)\n")

                segno = "OK  " if vivo else "MUTO"
                barra = "#" * 30 if vivo else "." * 30
                print(f"  {time.strftime('%H:%M:%S')}  radar {segno} |{barra}|  "
                      f"cadute finora: {cadute}", end="\r", flush=True)
    except KeyboardInterrupt:
        print("\n\nfine.")
    except Exception as e:
        print(f"\nnon riesco a leggere dalla dashboard ({type(e).__name__}).")
        print("Assicurati che sia aperta:  ./scripts/radar-live.sh")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nfine.")
