"""Sensori del nodo tavolo.

Due livelli:
  - PIR (HC-SR501): movimento grezzo. Dice "qualcuno si e' mosso", costa 2 EUR,
    ma se stanno fermi a mangiare smette di vedere.
  - Radar mmWave LD2450: presenza anche da fermi + posizione di max 3 bersagli,
    da cui stimo i coperti. Sostituisce bene il lidar: non ha parti mobili,
    vede attraverso la plastica (lo si annega nella scatola stampata in 3D)
    e non gli frega nulla della luce.

Ogni classe espone .leggi() -> Lettura | None. Se una libreria hardware manca,
la classe non viene istanziata: l'agent parte lo stesso in modalita' finta.
"""
from __future__ import annotations

import math
import struct
import time
from dataclasses import dataclass, field


@dataclass
class Lettura:
    movimento: bool = False
    presenza: bool = False
    target: int = 0
    distanze_cm: list[float] = field(default_factory=list)

    def unisci(self, altra: "Lettura") -> "Lettura":
        return Lettura(
            movimento=self.movimento or altra.movimento,
            presenza=self.presenza or altra.presenza,
            target=max(self.target, altra.target),
            distanze_cm=self.distanze_cm or altra.distanze_cm,
        )


class SensorePir:
    """HC-SR501 / AM312 su un pin GPIO."""

    def __init__(self, pin: int = 17, tenuta: float = 8.0) -> None:
        from gpiozero import MotionSensor  # type: ignore

        self.pir = MotionSensor(pin)
        self.tenuta = tenuta        # per quanto tengo buono l'ultimo movimento
        self._ultimo = 0.0

    def leggi(self) -> Lettura:
        if self.pir.motion_detected:
            self._ultimo = time.time()
        attivo = (time.time() - self._ultimo) < self.tenuta
        return Lettura(movimento=attivo)


class SensoreLd2450:
    """Radar 24 GHz HLK-LD2450: fino a 3 bersagli con coordinate in mm."""

    HEADER = b"\xaa\xff\x03\x00"
    FOOTER = b"\x55\xcc"

    def __init__(self, porta: str = "/dev/serial0", baud: int = 256000,
                 raggio_cm: float = 140.0) -> None:
        import serial  # type: ignore

        self.ser = serial.Serial(porta, baud, timeout=0.5)
        self.raggio_cm = raggio_cm   # oltre questo raggio non e' gente del tavolo
        self._buf = b""

    @staticmethod
    def _coord(raw: int) -> float:
        """Il LD2450 mette il segno nel bit alto invece di usare il complemento."""
        valore = raw & 0x7FFF
        return float(valore) if raw & 0x8000 else -float(valore)

    def _frame(self) -> bytes | None:
        self._buf += self.ser.read(256)
        if len(self._buf) > 4096:
            self._buf = self._buf[-1024:]
        i = self._buf.rfind(self.HEADER)
        if i < 0 or len(self._buf) < i + 30:
            return None
        frame = self._buf[i:i + 30]
        self._buf = self._buf[i + 30:]
        return frame if frame.endswith(self.FOOTER) else None

    def leggi(self) -> Lettura | None:
        frame = self._frame()
        if frame is None:
            return None
        distanze: list[float] = []
        mosso = False
        for n in range(3):
            blocco = frame[4 + n * 8: 12 + n * 8]
            x, y, v, _porta = struct.unpack("<HHHH", blocco)
            if (x, y, v) == (0, 0, 0):
                continue
            cx, cy = self._coord(x), self._coord(y)
            dist_cm = math.hypot(cx, cy) / 10.0
            if dist_cm > self.raggio_cm:
                continue          # passante o tavolo accanto
            distanze.append(round(dist_cm, 1))
            if abs(self._coord(v)) > 2:
                mosso = True
        return Lettura(
            movimento=mosso,
            presenza=bool(distanze),
            target=len(distanze),
            distanze_cm=distanze,
        )


class SensoreFinto:
    """Nessun hardware: la presenza arriva dal pannello simulatore della dashboard."""

    def leggi(self) -> Lettura | None:
        return None


def costruisci(config: dict) -> list:
    """Istanzia i sensori descritti in config, saltando quelli non disponibili."""
    sensori = []
    if config.get("pir", {}).get("attivo"):
        try:
            sensori.append(SensorePir(pin=config["pir"].get("pin", 17)))
        except Exception as e:                     # niente GPIO: non e' un Pi
            print(f"[sensori] PIR non disponibile: {e}")
    if config.get("ld2450", {}).get("attivo"):
        try:
            sensori.append(SensoreLd2450(
                porta=config["ld2450"].get("porta", "/dev/serial0"),
                raggio_cm=config["ld2450"].get("raggio_cm", 140.0),
            ))
        except Exception as e:
            print(f"[sensori] LD2450 non disponibile: {e}")
    if not sensori:
        print("[sensori] modalita' finta: presenza pilotata dalla dashboard")
        sensori.append(SensoreFinto())
    return sensori


def leggi_tutti(sensori: list) -> Lettura | None:
    unione: Lettura | None = None
    for s in sensori:
        try:
            l = s.leggi()
        except Exception as e:
            print(f"[sensori] errore lettura {type(s).__name__}: {e}")
            continue
        if l is None:
            continue
        unione = l if unione is None else unione.unisci(l)
    return unione
