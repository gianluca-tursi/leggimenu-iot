#!/usr/bin/env python3
"""Il ponte: dal nodo attaccato via USB al server che sta su internet.

Il Freenove non ha WiFi. La sua connessione al mondo e' il cavo USB e questa
macchina: qui si legge la seriale e si rigira al server, e si riportano
indietro i comandi che arrivano dalla cassa.

    ./scripts/ponte.sh                          verso il server configurato
    SERVER=wss://tavolo.esempio.it ./scripts/ponte.sh

Il server lo si decide con SERVER, il segreto condiviso con LEGGIMENU_SEGRETO.
"""
from __future__ import annotations

import asyncio
import glob
import os
import sys
import time

try:
    import serial
    import websockets
except ImportError:
    sys.exit("mancano le librerie: ./scripts/installa.sh")

SERVER = os.environ.get("SERVER", "ws://localhost:8080")
SEGRETO = os.environ.get("LEGGIMENU_SEGRETO", "")


def trova_porta() -> str | None:
    for p in sorted(glob.glob("/dev/cu.wchusbserial*") + glob.glob("/dev/cu.usbserial*")
                    + glob.glob("/dev/ttyUSB*")):
        return p
    return None


async def una_sessione() -> None:
    """Una connessione al server, finche' regge."""
    indirizzo = SERVER.rstrip("/") + "/ws/nodo"
    async with websockets.connect(indirizzo, ping_interval=20) as ws:
        if SEGRETO:
            await ws.send(f'{{"segreto": "{SEGRETO}"}}')
        print(f"· collegato a {indirizzo}")

        porta = trova_porta()
        if not porta:
            raise RuntimeError("nessun nodo sull'USB")
        ser = serial.Serial(porta, 115200, timeout=0.1)
        print(f"· nodo su {porta}")

        async def dal_nodo() -> None:
            """Legge la seriale senza bloccare il resto."""
            while True:
                dati = await asyncio.to_thread(ser.read, 2048)
                if dati:
                    import json
                    await ws.send(json.dumps(
                        {"righe": dati.decode("utf-8", "replace")}))
                else:
                    await asyncio.sleep(0.02)

        async def dal_server() -> None:
            """I comandi della cassa finiscono sulla seriale."""
            import json
            async for grezzo in ws:
                msg = json.loads(grezzo)
                if "comando" in msg:
                    ser.write(msg["comando"].encode())

        try:
            await asyncio.gather(dal_nodo(), dal_server())
        finally:
            ser.close()


async def main() -> None:
    print(f"ponte leggimenu · server: {SERVER}")
    attesa = 2
    while True:
        try:
            await una_sessione()
            attesa = 2
        except Exception as e:
            # Il ponte deve rialzarsi da solo: in fiera nessuno lo guarda, e
            # una disconnessione di rete non deve spegnere il tavolo.
            print(f"· caduto ({type(e).__name__}: {e}) — riprovo fra {attesa}s")
            await asyncio.sleep(attesa)
            attesa = min(attesa * 2, 30)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nponte chiuso")
