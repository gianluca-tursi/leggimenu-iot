"""Modelli dati condivisi fra backend, dashboard cassa e nodo IoT."""
from __future__ import annotations

import time
import uuid
from dataclasses import dataclass, field, asdict
from enum import Enum
from typing import Any


def now() -> float:
    return time.time()


def new_id(prefix: str) -> str:
    return f"{prefix}_{uuid.uuid4().hex[:8]}"


class StatoTavolo(str, Enum):
    LIBERO = "libero"          # nessuno al tavolo, display in idle
    PRESENZA = "presenza"      # movimento rilevato, sto stimando i coperti
    APERTO = "aperto"          # sessione aperta, QR a display, nessun ordine
    IN_SERVIZIO = "in_servizio"  # almeno un piatto ordinato
    CONTO = "conto"            # conto richiesto
    PULIZIA = "pulizia"        # clienti andati via, tavolo da risistemare


class StatoPiatto(str, Enum):
    ORDINATO = "ordinato"
    IN_PREPARAZIONE = "in_preparazione"
    CONSEGNATO = "consegnato"


@dataclass
class RigaOrdine:
    id: str
    piatto_id: str
    nome: str
    categoria: str
    prezzo: float
    quantita: int = 1
    stato: StatoPiatto = StatoPiatto.ORDINATO
    tag: list[str] = field(default_factory=list)
    ts_ordine: float = field(default_factory=now)
    ts_consegna: float | None = None

    def to_dict(self) -> dict[str, Any]:
        d = asdict(self)
        d["stato"] = self.stato.value
        return d


@dataclass
class LetturaSensori:
    """Ultimo campione grezzo arrivato dal nodo tavolo."""
    movimento: bool = False          # PIR: qualcuno si muove / appoggia i gomiti
    presenza: bool = False           # mmWave: qualcuno c'e' anche se sta fermo
    target: int = 0                  # quanti bersagli distinti vede il radar
    distanze_cm: list[float] = field(default_factory=list)
    ts: float = field(default_factory=now)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class Abbinamento:
    """Proposta di vino calcolata sui piatti in tavola."""
    vino_id: str
    nome: str
    tipo: str
    annata: int | None
    calice: float
    bottiglia: float
    punteggio: float
    motivo: str
    formato_consigliato: str  # "calice" | "bottiglia"
    quantita_consigliata: int

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class Tavolo:
    id: str                      # es. "T07"
    nome: str                    # es. "Tavolo 7"
    posti: int = 4
    sala: str = "Sala principale"

    stato: StatoTavolo = StatoTavolo.LIBERO
    sessione_id: str | None = None
    coperti_stimati: int = 0
    coperti_confermati: int | None = None
    ordine: list[RigaOrdine] = field(default_factory=list)
    abbinamento: Abbinamento | None = None
    abbinamento_accettato: bool = False

    sensori: LetturaSensori = field(default_factory=LetturaSensori)
    device_online: bool = False
    ultimo_heartbeat: float | None = None

    ts_stato: float = field(default_factory=now)
    ts_apertura: float | None = None
    ts_ultima_presenza: float | None = None

    # --- derivati -------------------------------------------------------
    @property
    def coperti(self) -> int:
        return self.coperti_confermati or self.coperti_stimati

    @property
    def totale(self) -> float:
        return round(sum(r.prezzo * r.quantita for r in self.ordine), 2)

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "nome": self.nome,
            "posti": self.posti,
            "sala": self.sala,
            "stato": self.stato.value,
            "sessione_id": self.sessione_id,
            "coperti_stimati": self.coperti_stimati,
            "coperti_confermati": self.coperti_confermati,
            "coperti": self.coperti,
            "ordine": [r.to_dict() for r in self.ordine],
            "abbinamento": self.abbinamento.to_dict() if self.abbinamento else None,
            "abbinamento_accettato": self.abbinamento_accettato,
            "sensori": self.sensori.to_dict(),
            "device_online": self.device_online,
            "ultimo_heartbeat": self.ultimo_heartbeat,
            "ts_stato": self.ts_stato,
            "ts_apertura": self.ts_apertura,
            "ts_ultima_presenza": self.ts_ultima_presenza,
            "totale": self.totale,
        }
