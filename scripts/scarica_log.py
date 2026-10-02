#!/usr/bin/env python3
"""Scarica il registro dalla flash del nodo e lo salva in registrazioni/.

    ./scripts/scarica-log.sh              scarica
    ./scripts/scarica-log.sh --cancella   scarica e poi svuota il registro

La porta seriale la puo' usare un processo solo: se la dashboard e' aperta,
questo script te lo dice invece di restare muto.
"""
import sys, time, glob, pathlib, datetime

try:
    import serial
except ImportError:
    sys.exit("manca pyserial: usa .venv/bin/python")

QUI = pathlib.Path(__file__).resolve().parent.parent
INIZIO, FINE = "---INIZIO REGISTRO---", "---FINE REGISTRO---"


def trova_porta() -> str:
    for p in sorted(glob.glob("/dev/cu.wchusbserial*") + glob.glob("/dev/cu.usbserial*")):
        return p
    sys.exit("nodo non trovato: attacca l'USB del Freenove")


def main() -> None:
    porta = trova_porta()
    try:
        # exclusive: se un altro processo ha gia' la porta, voglio un errore
        # chiaro adesso e non byte mancanti a meta' scarico.
        ser = serial.Serial(porta, 115200, timeout=2, exclusive=True)
    except Exception as e:
        sys.exit(f"non riesco ad aprire {porta}: {e}\n"
                 "  la porta e' occupata da un altro processo. Usa\n"
                 "  ./scripts/scarica-log.sh, che chiude la dashboard da solo.")

    print(f"porta: {porta}")
    time.sleep(2.5)                      # l'apertura resetta la scheda: la aspetto
    ser.reset_input_buffer()
    ser.write(b"L\n")

    testo, scaduto = "", time.time() + 180
    while time.time() < scaduto:
        pezzo = ser.read(4096).decode("utf-8", "replace")
        if not pezzo and INIZIO in testo:
            break
        testo += pezzo
        if FINE in testo:
            break
    else:
        print("⚠️  scaduto il tempo: salvo quello che ho preso.")

    if INIZIO not in testo:
        ser.close()
        sys.exit("il nodo non ha risposto. E' acceso e col firmware nuovo?")

    corpo = testo.split(INIZIO, 1)[1].split(FINE, 1)[0].strip("\r\n")
    righe = [r for r in corpo.splitlines() if r.strip()]

    cartella = QUI / "registrazioni"
    cartella.mkdir(exist_ok=True)
    nome = cartella / f"log-{datetime.datetime.now():%Y%m%d-%H%M%S}.csv"
    nome.write_text(corpo + "\n")

    marche = [r for r in righe if r.startswith("#MARCA")]
    avvii = [r for r in righe if r.startswith("#AVVIO")]
    print(f"\n✅ {nome.relative_to(QUI)}")
    print(f"   {len(righe)} righe, {len(avvii)} accensioni, {len(marche)} segnalibri")

    if "--cancella" in sys.argv:
        ser.write(b"C\n")
        time.sleep(1)
        print("   registro svuotato sul nodo")
    ser.close()


if __name__ == "__main__":
    main()
