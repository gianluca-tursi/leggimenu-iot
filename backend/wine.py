"""Motore di abbinamento vino: regole deterministiche sui tag dei piatti.

Volutamente senza LLM: gira offline, e' spiegabile e istantaneo. Il punto di
innesto per un modello piu' furbo e' `suggerisci()`, che ha gia' in mano tutto
il contesto (piatti, coperti, giacenze).
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from models import Abbinamento, RigaOrdine, StatoPiatto

DATA = Path(__file__).parent / "data"

# quanto "pesa" una portata nel decidere il vino di tutto il tavolo
PESO_CATEGORIA = {
    "antipasto": 1.0,
    "primo": 1.3,
    "pizza": 1.2,
    "secondo": 1.6,
    "dolce": 1.4,
    "contorno": 0.5,
}

ETICHETTA_TAG = {
    "carne_rossa": "carne rossa",
    "carne": "carne",
    "pesce": "pesce",
    "grasso": "piatti grassi",
    "latticino": "formaggio",
    "piccante": "piccante",
    "pomodoro": "pomodoro",
    "selvaggina": "selvaggina",
    "strutturato": "piatto strutturato",
    "delicato": "sapori delicati",
    "dolce": "dessert",
    "stagionato": "stagionati",
    "salume": "salumi",
    "grigliato": "griglia",
    "fresco": "sapori freschi",
    "speziato": "speziato",
    "cremoso": "dolce cremoso",
    "secco": "pasticceria secca",
}


def _carica(nome: str) -> dict[str, Any]:
    return json.loads((DATA / nome).read_text(encoding="utf-8"))


VINI: list[dict[str, Any]] = _carica("wines.json")["vini"]
MENU: list[dict[str, Any]] = _carica("menu.json")["piatti"]
MENU_BY_ID = {p["id"]: p for p in MENU}


def pesi_tag(ordine: list[RigaOrdine]) -> dict[str, float]:
    """Somma pesata dei tag dei piatti effettivamente in tavola."""
    pesi: dict[str, float] = {}
    for riga in ordine:
        base = PESO_CATEGORIA.get(riga.categoria, 1.0) * riga.quantita
        # un piatto gia' consegnato conta un filo di piu': e' li', lo stanno mangiando
        if riga.stato == StatoPiatto.CONSEGNATO:
            base *= 1.15
        for tag in riga.tag:
            pesi[tag] = pesi.get(tag, 0.0) + base
    return pesi


def _punteggio(vino: dict[str, Any], pesi: dict[str, float]) -> tuple[float, list[str]]:
    prof = vino["profilo"]
    score = 0.0
    match: list[str] = []

    for tag in vino["affinita"]:
        if tag in pesi:
            score += pesi[tag]
            match.append(tag)

    # --- correzioni enologiche ------------------------------------------
    grasso = pesi.get("grasso", 0) + pesi.get("latticino", 0) + pesi.get("fritto", 0)
    if grasso:
        if prof["bolle"] >= 3 or prof["acidita"] >= 4:
            score += 0.8 * min(grasso, 3)      # bollicina/acidita' sgrassano
    if pesi.get("piccante") and prof["tannino"] >= 4:
        score -= 1.5 * pesi["piccante"]        # tannino + peperoncino = bruciore
    if pesi.get("pesce") and prof["tannino"] >= 3:
        score -= 1.8 * pesi["pesce"]           # tannino sul pesce: no
    strutt = pesi.get("carne_rossa", 0) + pesi.get("selvaggina", 0) + pesi.get("strutturato", 0)
    if strutt and prof["corpo"] <= 2:
        score -= 1.2 * strutt                  # vino troppo esile per il piatto
    if pesi.get("dolce"):
        # sul dessert solo un vino dolce regge; gli altri li spingo giu'
        score += 2.0 * pesi["dolce"] if prof["dolcezza"] >= 4 else -2.0 * pesi["dolce"]
    if vino.get("giacenza", 0) <= 2:
        score -= 1.0                           # quasi finito: non spingerlo

    return score, match


def _motivo(vino: dict[str, Any], match: list[str], pesi: dict[str, float]) -> str:
    forti = sorted(match, key=lambda t: pesi.get(t, 0), reverse=True)[:2]
    if not forti:
        return "Scelta versatile per la tavola"
    etichette = [ETICHETTA_TAG.get(t, t) for t in forti]
    return "Va bene con " + " e ".join(etichette)


def suggerisci(ordine: list[RigaOrdine], coperti: int) -> Abbinamento | None:
    """Ritorna il vino migliore per l'ordine corrente, o None se e' presto."""
    rilevanti = [r for r in ordine if r.categoria != "bevanda"]
    if not rilevanti:
        return None

    pesi = pesi_tag(rilevanti)
    if not pesi:
        return None

    classifica = []
    for vino in VINI:
        if vino.get("giacenza", 0) <= 0:
            continue
        score, match = _punteggio(vino, pesi)
        classifica.append((score, vino, match))
    if not classifica:
        return None

    classifica.sort(key=lambda x: x[0], reverse=True)
    score, vino, match = classifica[0]
    if score <= 0:
        return None

    coperti = max(coperti, 1)
    # dai 3 coperti in su conviene la bottiglia; il passito si beve sempre a calice
    bottiglia = coperti >= 3 and vino["tipo"] != "dolce"
    quantita = 1 if bottiglia else coperti

    return Abbinamento(
        vino_id=vino["id"],
        nome=vino["nome"],
        tipo=vino["tipo"],
        annata=vino.get("annata"),
        calice=vino["calice"],
        bottiglia=vino["bottiglia"],
        punteggio=round(score, 2),
        motivo=_motivo(vino, match, pesi),
        formato_consigliato="bottiglia" if bottiglia else "calice",
        quantita_consigliata=quantita,
    )
