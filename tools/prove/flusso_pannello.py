#!/usr/bin/env python3
"""Prova il flusso delle schermate del pannello, collegato via USB al Mac.

    .venv/bin/python tools/prove/flusso_pannello.py

Manda gli stessi comandi che manderebbe il nodo, con gli stessi intervalli del
server, e misura quando il pannello cambia schermata. Serve a verificare il
flusso INTERO prima di rimontare i cavi, non solo il pezzo appena cambiato.

Atteso:
  WiFi                       15 s, poi torna al benvenuto
  ordine                     15 s
  vino (dopo l'ordine)       30 s, poi torna al benvenuto con gli stessi coperti
  offerta (non deve rompersi) 15 s
"""
import re, serial, sys, time, glob

porta = (glob.glob("/dev/cu.wchusbserial*") + [None])[0]
if not porta:
    sys.exit("nessuna scheda sull'USB")
s = serial.Serial(porta, 115200, timeout=0.2, exclusive=True)
time.sleep(4); s.reset_input_buffer()
t0 = time.time()
SCHERMI = {"-7": "WIFI", "-8": "ORDINE", "-9": "VINO", "-10": "ANNUNCIO",
           "-11": "RECENSIONE", "0": "prenotato"}
visti = []

def ascolta(secondi):
    fine = time.time() + secondi
    buf = ""
    while time.time() < fine:
        buf += s.read(4000).decode("utf-8", "replace")
        *righe, buf = buf.split("\n")
        for r in righe:
            m = re.search(r"disegnato: (-?\d+) coperti", r)
            if m:
                n = m.group(1)
                nome = SCHERMI.get(n, f"benvenuto {n} coperti")
                visti.append((round(time.time() - t0), nome))
                print(f"  {time.time()-t0:5.0f}s  {nome}", flush=True)
            elif "vino in attesa" in r or "ora il vino" in r:
                print(f"  {time.time()-t0:5.0f}s    ({r.split('] ')[-1].strip()})", flush=True)

def manda(*righe):
    for r in righe:
        s.write(("*" + r + "\n").encode()); time.sleep(0.05)   # come il server

print("1) tavolo aperto con 2 coperti"); manda("T:2"); ascolta(8)
print("2) WiFi");                        manda("W");   ascolta(24)
print("3) ordine + vino, di fila come li manda il server")
manda("Z", "V:2x Tagliatelle al ragu.", "X",
      "B:Amarone della Valpolicella DOCG", "C:Va bene con selvaggina e piatto.", "Y")
ascolta(58)
print("4) offerta (controllo di non averla rotta)")
manda("D:Cacio e pepe|-25% solo oggi|9,75 invece di 13,00"); ascolta(24)
s.close()

print("\nriassunto:", " -> ".join(n for _, n in visti))
