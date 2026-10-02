#!/usr/bin/env python3
"""Legge il radar per N secondi e ne tira fuori un riassunto leggibile.

    .venv/bin/python scripts/analizza_radar.py [secondi]

Invece di scorrere centinaia di righe: quante persone ha visto, fino a che
distanza, fino a che angolo, e quanto e' rimasto cieco.
"""
import glob
import math
import re
import sys
import time
from collections import Counter

import serial

SECONDI = float(sys.argv[1]) if len(sys.argv) > 1 else 40.0
RE_N = re.compile(r"bersagli:\s*(\d+)")
RE_T = re.compile(r"\[(\d)\]\s*x=(-?\d+)cm\s+y=(-?\d+)cm\s+dist=([\d.]+)cm\s+v=(-?\d+)")


def porta():
    p = [x for x in glob.glob('/dev/cu.*')
         if any(k in x for k in ('wchusbserial', 'usbmodem', 'usbserial', 'SLAB'))]
    if not p:
        sys.exit("nessuna porta del nodo trovata")
    return p[0]


def mappa(punti, largh=64, alt=17):
    """Vista dall'alto in ASCII: il sensore e' in basso al centro."""
    if not punti:
        return "  (nessun punto)"
    maxy = max(120, max(y for _, y in punti))
    maxx = max(60, max(abs(x) for x, _ in punti))
    griglia = [[' '] * largh for _ in range(alt)]
    for x, y in punti:
        cx = int((x / maxx) * (largh // 2 - 1)) + largh // 2
        cy = alt - 1 - int((y / maxy) * (alt - 1))
        if 0 <= cx < largh and 0 <= cy < alt:
            c = griglia[cy][cx]
            griglia[cy][cx] = '#' if c in '#o' else 'o'
    righe = ["  +" + "-" * largh + "+  ^ y (lontano)"]
    for i, r in enumerate(griglia):
        et = f"{int(maxy * (alt - 1 - i) / (alt - 1)):4d}cm"
        righe.append(f"{et} |" + "".join(r) + "|")
    righe.append("     +" + "-" * largh + "+")
    righe.append(f"     {-maxx:>4}cm" + " " * (largh - 14) + f"{maxx:>4}cm   (sensore al centro in basso)")
    return "\n".join(righe)


def main():
    p = porta()
    print(f"--- {p} - analizzo {SECONDI:.0f}s. Muoviti! ---", flush=True)
    conteggi, punti, dist, ang = Counter(), [], [], []
    buchi, buco_da, buco_max = 0, None, 0.0
    t0 = time.time()

    with serial.Serial(p, 115200, timeout=0.3) as s:
        s.setDTR(False); s.setRTS(False)
        resto = ""
        while time.time() - t0 < SECONDI:
            resto += s.read(4096).decode('utf-8', 'replace')
            *linee, resto = resto.split("\n")
            for l in linee:
                m = RE_N.search(l)
                if m:
                    n = int(m.group(1))
                    conteggi[n] += 1
                    ora = time.time()
                    if n == 0 and buco_da is None:
                        buco_da = ora
                    elif n > 0 and buco_da is not None:
                        d = ora - buco_da
                        buchi += 1; buco_max = max(buco_max, d); buco_da = None
                t = RE_T.search(l)
                if t:
                    x, y, d = int(t.group(2)), int(t.group(3)), float(t.group(4))
                    punti.append((x, y)); dist.append(d)
                    ang.append(math.degrees(math.atan2(x, max(y, 1))))

    tot = sum(conteggi.values()) or 1
    print("\n" + "=" * 72)
    print(f"letture: {tot}   ({tot / SECONDI:.1f} al secondo)")
    print("-" * 72)
    print("QUANTE PERSONE HA VISTO")
    for n in sorted(conteggi):
        q = conteggi[n] * 100 / tot
        print(f"  {n} bersagli  {conteggi[n]:5d}  {q:5.1f}%  {'#' * int(q / 2)}")
    if dist:
        print("-" * 72)
        print(f"DISTANZA   min {min(dist):.0f} cm   max {max(dist):.0f} cm   media {sum(dist)/len(dist):.0f} cm")
        print(f"ANGOLO     da {min(ang):+.0f}°  a  {max(ang):+.0f}°   (il cono utile e' +/-60°)")
    print("-" * 72)
    print(f"BUCHI (nessun bersaglio): {buchi}   il piu' lungo: {buco_max:.1f} s")
    if buco_max > 3:
        print("  ^ se e' successo mentre stavi fermo, serve l'LD2410C")
    print("-" * 72)
    print("DOVE TI HA VISTO (vista dall'alto)")
    print(mappa(punti))
    print("=" * 72)


if __name__ == "__main__":
    main()
