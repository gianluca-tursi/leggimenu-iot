#!/usr/bin/env python3
"""Trova l'algoritmo di conteggio piu' preciso, sulle registrazioni vere.

    .venv/bin/python tools/taratura.py

Il radar da' un numero istantaneo che balla. Quello che conta e' la STIMA
che ne ricaviamo: su quanti secondi guardare, e quale statistica usare.
Invece di indovinare i parametri, li proviamo tutti sulle registrazioni
etichettate e vediamo quale sbaglia meno.

Niente prove fisiche da rifare: si registra una volta, si tara all'infinito.
"""
from __future__ import annotations

import json
import statistics
from pathlib import Path

CARTELLA = Path(__file__).parent.parent / "registrazioni"
FINESTRE = [3, 5, 8, 10, 15, 20, 30]          # secondi su cui guardare
STATISTICHE = ["max", "p90", "p80", "p60", "mediana"]


def applica(valori: list[int], come: str) -> float:
    if not valori:
        return 0
    v = sorted(valori)
    if come == "max":
        return v[-1]
    if come == "mediana":
        return statistics.median(v)
    perc = {"p90": 0.90, "p80": 0.80, "p60": 0.60}[come]
    return v[min(len(v) - 1, int(len(v) * perc))]


def carica(f: Path) -> tuple[int, list[tuple[float, int]]]:
    vere, serie = -1, []
    for riga in f.read_text().splitlines():
        if not riga.strip():
            continue
        m = json.loads(riga)
        if m.get("tipo") == "meta":
            vere = m.get("persone_vere", -1)
        elif m.get("tipo") == "frame":
            serie.append((m.get("ts", 0.0), m.get("n", 0)))
    return vere, serie


def valuta(serie: list[tuple[float, int]], finestra: float, come: str) -> list[float]:
    """Stima a ogni istante guardando indietro `finestra` secondi."""
    stime = []
    for i, (t, _) in enumerate(serie):
        dentro = [n for ts, n in serie[max(0, i - 400):i + 1] if t - ts <= finestra]
        if dentro:
            stime.append(applica(dentro, come))
    return stime


def main() -> None:
    if not CARTELLA.exists() or not list(CARTELLA.glob("*.jsonl")):
        print("Nessuna registrazione in registrazioni/.")
        print("Falla cosi', dicendo quante persone ci sono davvero:")
        print("  .venv/bin/python tools/registra_radar.py 60 radar.local 2")
        return

    prove = []
    print("REGISTRAZIONI TROVATE")
    for f in sorted(CARTELLA.glob("*.jsonl")):
        vere, serie = carica(f)
        if vere < 0:
            print(f"  {f.name}: senza etichetta, la salto")
            continue
        if not serie:
            print(f"  {f.name}: vuota, la salto")
            continue
        istantanee = [n for _, n in serie]
        esatte = sum(1 for n in istantanee if n == vere) * 100 / len(istantanee)
        print(f"  {f.name}: {vere} persone, {len(serie)} frame, "
              f"istantanea esatta {esatte:.0f}%")
        prove.append((vere, serie))

    if not prove:
        print("\nNessuna registrazione etichettata: non posso tarare niente.")
        return

    print("\n" + "=" * 70)
    print("CONFRONTO DEGLI ALGORITMI  (errore medio in persone, piu' basso e' meglio)")
    print("=" * 70)
    print(f"{'finestra':>9} | " + " | ".join(f"{s:>8}" for s in STATISTICHE))
    print("-" * 70)

    risultati = []
    for fin in FINESTRE:
        riga = [f"{fin:>7}s |"]
        for st in STATISTICHE:
            errori, esatti, tot = [], 0, 0
            for vere, serie in prove:
                for stima in valuta(serie, fin, st):
                    errori.append(abs(stima - vere))
                    esatti += (round(stima) == vere)
                    tot += 1
            err = sum(errori) / len(errori) if errori else 9
            perc = esatti * 100 / tot if tot else 0
            risultati.append((err, perc, fin, st))
            riga.append(f"{err:>8.2f}")
        print(" ".join(riga))

    risultati.sort()
    print("\n" + "=" * 70)
    print("I TRE MIGLIORI")
    for err, perc, fin, st in risultati[:3]:
        print(f"  finestra {fin:>2}s + {st:<8}  errore medio {err:.2f} persone, "
              f"esatto nel {perc:.0f}% del tempo")
    err, perc, fin, st = risultati[0]
    print("\nDa mettere in backend/state.py:")
    print(f"  FINESTRA_STIMA = {fin}.0        # secondi")
    print(f"  statistica     = {st}")
    print("=" * 70)


if __name__ == "__main__":
    main()
