#!/usr/bin/env python3
"""Legge la seriale del nodo.

    leggi_seriale.py [secondi] [--reset]

--reset fa ripartire la scheda (impulso su RTS, come fa esptool) cosi' si
cattura anche quello che stampa all'avvio: senza, il banner te lo perdi.
"""
import sys, time, glob
import serial

argomenti = [a for a in sys.argv[1:] if not a.startswith('--')]
secondi = float(argomenti[0]) if argomenti else 10.0
reset = '--reset' in sys.argv
porte = [p for p in glob.glob('/dev/cu.*')
         if any(k in p for k in ('wchusbserial', 'usbmodem', 'usbserial', 'SLAB'))]
if not porte:
    sys.exit("nessuna porta del nodo trovata")

try:
    _prova = serial.Serial(porte[0], 115200, timeout=0.3)
    _prova.close()
except Exception:
    sys.exit(f"La porta {porte[0]} e' occupata da un altro programma.\n"
             "Quasi sempre e' la dashboard: chiudila con Ctrl-C (o pkill -f radar_live.py)\n"
             "e riprova. Per caricare senza riaprirla:  RIAPRI=0 ./scripts/flash.sh <sketch>")

with serial.Serial(porte[0], 115200, timeout=0.3) as s:
    print(f"--- {porte[0]} @115200, leggo {secondi:.0f}s ---", flush=True)
    if reset:
        # RTS pilota EN sull'ESP32: un impulso e la scheda riparte.
        s.setDTR(False); s.setRTS(True)
        time.sleep(0.15)
        s.setRTS(False)
        print("[riavviata]", flush=True)
    else:
        s.setDTR(False); s.setRTS(False)
    fine = time.time() + secondi
    while time.time() < fine:
        try:
            d = s.read(4096)
        except serial.SerialException:
            print("\n[la porta e' stata presa da un altro programma o il nodo si e' scollegato]")
            break
        if d:
            sys.stdout.write(d.decode('utf-8', 'replace'))
            sys.stdout.flush()
print("\n--- fine ---")
