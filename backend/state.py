"""Macchina a stati della sala: un'istanza tiene tutti i tavoli in memoria.

Il flusso e' quello del prototipo:
  libero -> (movimento/presenza) -> presenza -> (stabile N sec) -> aperto
  aperto -> (primo piatto) -> in_servizio -> (conto) -> conto
  qualsiasi stato aperto -> (nessuno per M min) -> pulizia -> (ok cassa) -> libero
"""
from __future__ import annotations

import json
import zlib
from collections import deque
from pathlib import Path
from typing import Any, Callable

import wine
from models import (
    Abbinamento,
    LetturaSensori,
    RigaOrdine,
    StatoPiatto,
    StatoTavolo,
    Tavolo,
    new_id,
    now,
)

# --- parametri di comportamento (tarabili a caldo dalla dashboard) --------
SEC_STABILIZZAZIONE = 6.0    # quanto deve durare la presenza prima di aprire
SEC_USCITA_PRESENZA = 20.0   # presenza sparita in stato "presenza" -> torna libero
SEC_ABBANDONO = 180.0        # tavolo aperto senza nessuno -> pulizia
SEC_DEVICE_OFFLINE = 30.0    # nessun heartbeat -> nodo considerato offline
FINESTRA_STIMA = 15.0        # finestra su cui stimo i coperti


class Sala:
    def __init__(self, base_url: str = "http://localhost:8000") -> None:
        self.base_url = base_url
        self.tavoli: dict[str, Tavolo] = {}
        self.eventi: deque[dict[str, Any]] = deque(maxlen=200)
        self._campioni: dict[str, deque[tuple[float, int]]] = {}
        self._listener: Callable[[str, dict[str, Any]], None] | None = None
        self.menu = wine.MENU
        self.vini = wine.VINI

    # --- infrastruttura --------------------------------------------------
    def on_change(self, cb: Callable[[str, dict[str, Any]], None]) -> None:
        """Callback invocata a ogni cambiamento: (tavolo_id, payload tavolo)."""
        self._listener = cb

    def _notifica(self, tavolo: Tavolo) -> None:
        if self._listener:
            self._listener(tavolo.id, tavolo.to_dict())

    def log(self, tavolo_id: str, testo: str, livello: str = "info") -> None:
        self.eventi.appendleft({"ts": now(), "tavolo": tavolo_id, "testo": testo, "livello": livello})

    def aggiungi_tavolo(self, id: str, nome: str, posti: int = 4, sala: str = "Sala principale") -> Tavolo:
        t = Tavolo(id=id, nome=nome, posti=posti, sala=sala)
        self.tavoli[id] = t
        self._campioni[id] = deque(maxlen=120)
        return t

    def get(self, tavolo_id: str) -> Tavolo:
        if tavolo_id not in self.tavoli:
            raise KeyError(f"tavolo sconosciuto: {tavolo_id}")
        return self.tavoli[tavolo_id]

    def _cambia_stato(self, t: Tavolo, nuovo: StatoTavolo, motivo: str) -> None:
        if t.stato == nuovo:
            return
        t.stato = nuovo
        t.ts_stato = now()
        self.log(t.id, f"{t.nome}: {motivo}", "stato")

    # --- ingresso dati dal nodo IoT --------------------------------------
    def heartbeat(self, tavolo_id: str, online: bool = True) -> None:
        t = self.get(tavolo_id)
        cambiato = t.device_online != online
        t.device_online = online
        t.ultimo_heartbeat = now()
        if cambiato:
            self.log(tavolo_id, f"{t.nome}: nodo {'online' if online else 'offline'}",
                     "info" if online else "warn")
            self._notifica(t)

    def sensori(self, tavolo_id: str, lettura: LetturaSensori) -> Tavolo:
        t = self.get(tavolo_id)
        t.sensori = lettura
        t.device_online = True
        t.ultimo_heartbeat = now()

        qualcuno = lettura.presenza or lettura.movimento
        if qualcuno:
            t.ts_ultima_presenza = lettura.ts
            self._campioni[tavolo_id].append((lettura.ts, max(lettura.target, 1)))

        if t.stato == StatoTavolo.LIBERO and qualcuno:
            self._cambia_stato(t, StatoTavolo.PRESENZA, "qualcuno si e' seduto, sto contando i coperti")

        if t.stato == StatoTavolo.PRESENZA:
            t.coperti_stimati = self._stima_coperti(t)
            if qualcuno and now() - t.ts_stato >= SEC_STABILIZZAZIONE:
                self.apri(tavolo_id, automatico=True)
        elif t.stato in (StatoTavolo.APERTO, StatoTavolo.IN_SERVIZIO) and t.coperti_confermati is None:
            # finche' la cassa non conferma, continuo a raffinare la stima
            stima = self._stima_coperti(t)
            if stima > t.coperti_stimati:
                t.coperti_stimati = stima

        self._notifica(t)
        return t

    def _stima_coperti(self, t: Tavolo) -> int:
        """Stima i coperti dal numero di bersagli visti dal radar nell'ultima finestra."""
        limite = now() - FINESTRA_STIMA
        recenti = [n for ts, n in self._campioni[t.id] if ts >= limite]
        if not recenti:
            return t.coperti_stimati
        recenti.sort()
        # 80esimo percentile: ignora il picco isolato (cameriere che passa) ma
        # non si fa fregare dalla persona che in quel campione stava immobile
        idx = min(len(recenti) - 1, int(len(recenti) * 0.8))
        return max(1, min(recenti[idx], t.posti))

    # --- azioni di sala ---------------------------------------------------
    def apri(self, tavolo_id: str, coperti: int | None = None, automatico: bool = False) -> Tavolo:
        t = self.get(tavolo_id)
        t.sessione_id = new_id("s")
        t.ts_apertura = now()
        t.ordine = []
        t.abbinamento = None
        t.abbinamento_accettato = False
        t.coperti_confermati = coperti
        if coperti:
            t.coperti_stimati = coperti
        elif not t.coperti_stimati:
            t.coperti_stimati = self._stima_coperti(t) or 2
        origine = "in automatico dai sensori" if automatico else "dalla cassa"
        self._cambia_stato(t, StatoTavolo.APERTO, f"tavolo aperto {origine} ({t.coperti} coperti)")
        self._notifica(t)
        return t

    def conferma_coperti(self, tavolo_id: str, coperti: int) -> Tavolo:
        t = self.get(tavolo_id)
        t.coperti_confermati = max(1, coperti)
        self.log(tavolo_id, f"{t.nome}: coperti confermati a {coperti}", "info")
        self._ricalcola_vino(t)
        self._notifica(t)
        return t

    def ordina(self, tavolo_id: str, piatto_id: str, quantita: int = 1, origine: str = "cassa") -> Tavolo:
        t = self.get(tavolo_id)
        piatto = wine.MENU_BY_ID.get(piatto_id)
        if piatto is None:
            raise KeyError(f"piatto sconosciuto: {piatto_id}")
        if t.stato in (StatoTavolo.LIBERO, StatoTavolo.PULIZIA):
            self.apri(tavolo_id)

        esistente = next(
            (r for r in t.ordine if r.piatto_id == piatto_id and r.stato == StatoPiatto.ORDINATO), None
        )
        if esistente:
            esistente.quantita += quantita
        else:
            t.ordine.append(
                RigaOrdine(
                    id=new_id("r"),
                    piatto_id=piatto_id,
                    nome=piatto["nome"],
                    categoria=piatto["categoria"],
                    prezzo=piatto["prezzo"],
                    quantita=quantita,
                    tag=list(piatto.get("tag", [])),
                )
            )
        self._cambia_stato(t, StatoTavolo.IN_SERVIZIO, f"primo ordine ricevuto ({origine})")
        self.log(tavolo_id, f"{t.nome}: +{quantita}x {piatto['nome']} ({origine})", "ordine")
        self._ricalcola_vino(t)
        self._notifica(t)
        return t

    def stato_riga(self, tavolo_id: str, riga_id: str, stato: StatoPiatto) -> Tavolo:
        t = self.get(tavolo_id)
        riga = next((r for r in t.ordine if r.id == riga_id), None)
        if riga is None:
            raise KeyError(f"riga sconosciuta: {riga_id}")
        riga.stato = stato
        if stato == StatoPiatto.CONSEGNATO:
            riga.ts_consegna = now()
        self.log(tavolo_id, f"{t.nome}: {riga.nome} -> {stato.value}", "ordine")
        self._ricalcola_vino(t)
        self._notifica(t)
        return t

    def _ricalcola_vino(self, t: Tavolo) -> None:
        if t.abbinamento_accettato:
            return
        nuovo = wine.suggerisci(t.ordine, t.coperti)
        prima = t.abbinamento.vino_id if t.abbinamento else None
        t.abbinamento = nuovo
        if nuovo and nuovo.vino_id != prima:
            self.log(t.id, f"{t.nome}: proposto {nuovo.nome} ({nuovo.motivo.lower()})", "vino")

    def accetta_vino(self, tavolo_id: str, accettato: bool = True) -> Tavolo:
        t = self.get(tavolo_id)
        t.abbinamento_accettato = accettato
        if t.abbinamento:
            esito = "accettato" if accettato else "rifiutato"
            self.log(tavolo_id, f"{t.nome}: vino {esito} ({t.abbinamento.nome})", "vino")
        self._notifica(t)
        return t

    def chiedi_conto(self, tavolo_id: str) -> Tavolo:
        t = self.get(tavolo_id)
        self._cambia_stato(t, StatoTavolo.CONTO, f"conto richiesto ({t.totale:.2f} EUR)")
        self._notifica(t)
        return t

    def chiudi(self, tavolo_id: str) -> Tavolo:
        t = self.get(tavolo_id)
        totale = t.totale
        t.sessione_id = None
        t.ordine = []
        t.abbinamento = None
        t.abbinamento_accettato = False
        t.coperti_confermati = None
        t.coperti_stimati = 0
        t.ts_apertura = None
        self._campioni[t.id].clear()
        self._cambia_stato(t, StatoTavolo.LIBERO, f"tavolo chiuso e liberato ({totale:.2f} EUR)")
        self._notifica(t)
        return t

    # --- timer periodico --------------------------------------------------
    def tick(self) -> None:
        adesso = now()
        for t in self.tavoli.values():
            cambiato = False

            if t.device_online and t.ultimo_heartbeat and adesso - t.ultimo_heartbeat > SEC_DEVICE_OFFLINE:
                t.device_online = False
                self.log(t.id, f"{t.nome}: nodo non risponde", "warn")
                cambiato = True

            assenza = adesso - (t.ts_ultima_presenza or 0)
            if t.stato == StatoTavolo.PRESENZA and assenza > SEC_USCITA_PRESENZA:
                self._cambia_stato(t, StatoTavolo.LIBERO, "falso allarme, nessuno si e' fermato")
                t.coperti_stimati = 0
                cambiato = True
            elif t.stato in (StatoTavolo.APERTO, StatoTavolo.IN_SERVIZIO, StatoTavolo.CONTO) \
                    and assenza > SEC_ABBANDONO:
                self._cambia_stato(t, StatoTavolo.PULIZIA, "clienti usciti, tavolo da risistemare")
                cambiato = True

            if cambiato:
                self._notifica(t)

    # --- vista per il display e-ink ---------------------------------------
    def url_menu(self, t: Tavolo) -> str:
        return f"{self.base_url}/m/{t.sessione_id or t.id}"

    def vista_display(self, tavolo_id: str) -> dict[str, Any]:
        """View-model minimale: il nodo lo trasforma in pixel, io non mando bitmap."""
        t = self.get(tavolo_id)
        v: dict[str, Any] = {
            "schermata": t.stato.value,
            "tavolo": t.nome,
            "stato": t.stato.value,
            "coperti": t.coperti,
            "qr": self.url_menu(t),
            "totale": t.totale,
            "righe": [
                {"nome": r.nome, "quantita": r.quantita, "stato": r.stato.value}
                for r in t.ordine
            ],
            "vino": None,
            "rev": int(t.ts_stato * 1000),
        }
        if t.abbinamento and not t.abbinamento_accettato:
            a = t.abbinamento
            prezzo = a.bottiglia if a.formato_consigliato == "bottiglia" else a.calice
            v["vino"] = {
                "nome": a.nome,
                "tipo": a.tipo,
                "motivo": a.motivo,
                "formato": a.formato_consigliato,
                "prezzo": prezzo,
            }
        # la revisione cambia a ogni contenuto diverso: il nodo ridisegna solo se serve
        firma = json.dumps({k: v[k] for k in v if k != "rev"}, sort_keys=True, default=str)
        v["rev"] = zlib.crc32(firma.encode())
        return v

    def snapshot(self) -> dict[str, Any]:
        return {
            "tavoli": [t.to_dict() for t in self.tavoli.values()],
            "eventi": list(self.eventi)[:60],
            "menu": self.menu,
            "vini": self.vini,
        }
