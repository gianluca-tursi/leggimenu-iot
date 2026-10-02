"""Disegno delle schermate e-ink.

Il nodo riceve dal server un view-model JSON e lo trasforma in un'immagine 1-bit
della dimensione del pannello. Tutto pensato per bianco/nero: niente grigi,
niente antialias che sul e-ink diventa sporco.
"""
from __future__ import annotations

import io
from pathlib import Path
from typing import Any

import qrcode
from PIL import Image, ImageDraw, ImageFont

BIANCO = 255
NERO = 0

CANDIDATI_FONT = [
    ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
    ("/System/Library/Fonts/Supplemental/Arial Bold.ttf",
     "/System/Library/Fonts/Supplemental/Arial.ttf"),
    ("/Library/Fonts/Arial Bold.ttf", "/Library/Fonts/Arial.ttf"),
    ("C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/arial.ttf"),
]


def _percorsi_font() -> tuple[str | None, str | None]:
    for grassetto, normale in CANDIDATI_FONT:
        if Path(grassetto).exists() and Path(normale).exists():
            return grassetto, normale
    return None, None


_BOLD, _REG = _percorsi_font()
_cache: dict[tuple[bool, int], ImageFont.ImageFont] = {}


def font(dim: int, bold: bool = False) -> ImageFont.ImageFont:
    chiave = (bold, dim)
    if chiave not in _cache:
        percorso = _BOLD if bold else _REG
        if percorso:
            _cache[chiave] = ImageFont.truetype(percorso, dim)
        else:
            try:
                _cache[chiave] = ImageFont.load_default(size=dim)
            except TypeError:      # Pillow < 10.1
                _cache[chiave] = ImageFont.load_default()
    return _cache[chiave]


def _testo_troncato(d: ImageDraw.ImageDraw, testo: str, f, larghezza: int) -> str:
    if d.textlength(testo, font=f) <= larghezza:
        return testo
    while testo and d.textlength(testo + "...", font=f) > larghezza:
        testo = testo[:-1]
    return testo + "..."


def _qr(dati: str, lato: int) -> Image.Image:
    qr = qrcode.QRCode(version=None, error_correction=qrcode.constants.ERROR_CORRECT_M,
                       box_size=10, border=1)
    qr.add_data(dati)
    qr.make(fit=True)
    img = qr.make_image(fill_color="black", back_color="white").convert("1")
    return img.resize((lato, lato), Image.NEAREST)


def _intestazione(d: ImageDraw.ImageDraw, w: int, titolo: str, destra: str = "") -> int:
    """Barra nera in alto con il nome del tavolo. Ritorna la y libera sotto."""
    h = 34
    d.rectangle([0, 0, w, h], fill=NERO)
    d.text((10, h // 2), titolo, font=font(20, True), fill=BIANCO, anchor="lm")
    if destra:
        d.text((w - 10, h // 2), destra, font=font(15), fill=BIANCO, anchor="rm")
    return h + 8


def _piede(d: ImageDraw.ImageDraw, w: int, h: int, testo: str) -> None:
    d.line([0, h - 22, w, h - 22], fill=NERO)
    d.text((w // 2, h - 11), testo, font=font(12), fill=NERO, anchor="mm")


# --- singole schermate -----------------------------------------------------
def _schermo_libero(d, w, h, v):
    d.text((w // 2, h // 2 - 46), "leggimenu", font=font(40, True), fill=NERO, anchor="mm")
    d.line([w // 2 - 90, h // 2 - 20, w // 2 + 90, h // 2 - 20], fill=NERO)
    d.text((w // 2, h // 2 + 6), v.get("tavolo", ""), font=font(24, True), fill=NERO, anchor="mm")
    d.text((w // 2, h // 2 + 36), "Accomodatevi pure", font=font(16), fill=NERO, anchor="mm")
    _piede(d, w, h, "il menu compare appena vi sedete")


def _schermo_presenza(d, w, h, v):
    y = _intestazione(d, w, v.get("tavolo", ""), "benvenuti")
    d.text((w // 2, h // 2 - 14), "Un attimo solo...", font=font(26, True), fill=NERO, anchor="mm")
    coperti = v.get("coperti") or 0
    d.text((w // 2, h // 2 + 18), f"riconosco {coperti} coperti" if coperti else "vi sto contando",
           font=font(16), fill=NERO, anchor="mm")
    _piede(d, w, h, "leggimenu")


def _schermo_qr(d, img, w, h, v):
    y = _intestazione(d, w, v.get("tavolo", ""), f"{v.get('coperti', 0)} coperti")
    lato = min(h - y - 34, 168)
    qr = _qr(v.get("qr", ""), lato)
    x_qr = w - lato - 14
    img.paste(qr, (x_qr, y + 4))
    d.rectangle([x_qr - 4, y, x_qr + lato + 3, y + lato + 8], outline=NERO)

    col = 14
    largh = x_qr - col - 16
    d.text((col, y + 10), "Il menu", font=font(26, True), fill=NERO)
    d.text((col, y + 42), "e' qui", font=font(26, True), fill=NERO)
    d.text((col, y + 84), "Inquadra il codice", font=font(14), fill=NERO)
    d.text((col, y + 102), "con la fotocamera.", font=font(14), fill=NERO)
    d.text((col, y + 130), "Ordini dal telefono,", font=font(13), fill=NERO)
    d.text((col, y + 146), "senza scaricare nulla.", font=font(13), fill=NERO)
    _piede(d, w, h, "leggimenu - servizio al tavolo")


def _schermo_servizio(d, img, w, h, v):
    y = _intestazione(d, w, v.get("tavolo", ""),
                      f"{v.get('coperti', 0)} coperti - {v.get('totale', 0):.2f} EUR")
    vino = v.get("vino")
    altezza_vino = 64 if vino else 0
    fine_lista = h - 22 - altezza_vino - 6

    lato_qr = 74
    x_qr = w - lato_qr - 12
    img.paste(_qr(v.get("qr", ""), lato_qr), (x_qr, y + 2))
    d.text((x_qr + lato_qr // 2, y + lato_qr + 12), "riordina", font=font(11), fill=NERO, anchor="mm")

    d.text((12, y + 2), "In tavola", font=font(15, True), fill=NERO)
    riga_y = y + 24
    largh = x_qr - 24
    righe = v.get("righe", [])
    for r in righe:
        if riga_y > fine_lista - 16:
            rimaste = len(righe) - righe.index(r)
            d.text((12, riga_y), f"+ altri {rimaste} piatti", font=font(13), fill=NERO)
            break
        consegnato = r.get("stato") == "consegnato"
        # casella di stato: piena = gia' servito
        d.rectangle([12, riga_y + 2, 22, riga_y + 12], outline=NERO,
                    fill=NERO if consegnato else BIANCO)
        etichetta = f"{r.get('quantita', 1)}x {r.get('nome', '')}"
        d.text((30, riga_y), _testo_troncato(d, etichetta, font(13), largh - 30),
               font=font(13), fill=NERO)
        if not consegnato and r.get("stato") == "in_preparazione":
            d.text((30 + d.textlength(etichetta, font=font(13)) + 6, riga_y + 1),
                   "in cucina", font=font(10), fill=NERO)
        riga_y += 19

    if vino:
        top = h - 22 - altezza_vino
        d.rectangle([8, top, w - 8, h - 26], outline=NERO)
        d.rectangle([8, top, 8 + 6, h - 26], fill=NERO)
        d.text((22, top + 6), "IN ABBINAMENTO", font=font(11, True), fill=NERO)
        d.text((22, top + 20), _testo_troncato(d, vino["nome"], font(16, True), w - 46),
               font=font(16, True), fill=NERO)
        prezzo = f"{vino['prezzo']:.0f} EUR / {vino['formato']}"
        d.text((22, top + 42), _testo_troncato(d, f"{vino['motivo']} - {prezzo}", font(12), w - 46),
               font=font(12), fill=NERO)
    _piede(d, w, h, "leggimenu")



def _schermo_servizio_compatto(d, img, w, h, v):
    """Layout per pannelli bassi (< 200 px): QR + tavolo + vino, niente lista.

    Su 2,9 cm di altezza il QR si mangia tutto: la lista dei piatti non ci sta,
    e va bene cosi' -- il cliente ce l'ha sul telefono, al cameriere serve la
    cassa. Qui contano solo: dove si ordina, e cosa bere.
    """
    lato = h - 10
    x_qr = w - lato - 5
    img.paste(_qr(v.get("qr", ""), lato), (x_qr, 5))

    col = 6
    largh = x_qr - col - 8
    d.rectangle([0, 0, largh + 4, 20], fill=NERO)
    d.text((col, 10), v.get("tavolo", ""), font=font(14, True), fill=BIANCO, anchor="lm")
    d.text((largh, 10), f"{v.get('coperti', 0)}p", font=font(11), fill=BIANCO, anchor="rm")

    vino = v.get("vino")
    if vino:
        d.text((col, 26), "IN ABBINAMENTO", font=font(9, True), fill=NERO)
        nome = _testo_troncato(d, vino["nome"], font(13, True), largh)
        d.text((col, 38), nome, font=font(13, True), fill=NERO)
        prezzo = f"{vino['prezzo']:.0f} EUR / {vino['formato']}"
        d.text((col, 56), _testo_troncato(d, prezzo, font(11), largh), font=font(11), fill=NERO)
        if h > 100:
            d.text((col, 72), _testo_troncato(d, vino["motivo"], font(10), largh),
                   font=font(10), fill=NERO)
    else:
        n = sum(r.get("quantita", 1) for r in v.get("righe", []))
        d.text((col, 34), "Inquadra", font=font(15, True), fill=NERO)
        d.text((col, 52), "per il menu", font=font(15, True), fill=NERO)
        if n:
            d.text((col, 74), f"{n} piatti ordinati", font=font(10), fill=NERO)

    if h > 100:
        d.text((col, h - 12), f"{v.get('totale', 0):.2f} EUR", font=font(12, True), fill=NERO)


def _schermo_conto(d, img, w, h, v):
    y = _intestazione(d, w, v.get("tavolo", ""), "conto")
    d.text((16, y + 8), "Totale", font=font(18), fill=NERO)
    d.text((16, y + 32), f"{v.get('totale', 0):.2f} EUR", font=font(40, True), fill=NERO)
    d.text((16, y + 86), f"{v.get('coperti', 0)} coperti", font=font(14), fill=NERO)
    lato = min(h - y - 40, 120)
    img.paste(_qr(v.get("qr", ""), lato), (w - lato - 16, y + 10))
    d.text((w - lato // 2 - 16, y + lato + 22), "paga dal telefono",
           font=font(12), fill=NERO, anchor="mm")
    _piede(d, w, h, "grazie e a presto - leggimenu")


def _schermo_pulizia(d, w, h, v):
    y = _intestazione(d, w, v.get("tavolo", ""), "")
    d.text((w // 2, h // 2), "Tavolo in riassetto", font=font(24, True), fill=NERO, anchor="mm")
    d.text((w // 2, h // 2 + 28), "torna libero tra poco", font=font(14), fill=NERO, anchor="mm")
    _piede(d, w, h, "leggimenu")


SCHERMATE = {
    "libero": "libero",
    "presenza": "presenza",
    "aperto": "qr",
    "in_servizio": "servizio",
    "conto": "conto",
    "pulizia": "pulizia",
}


def disegna(vista: dict[str, Any], larghezza: int = 400, altezza: int = 300) -> Image.Image:
    img = Image.new("1", (larghezza, altezza), BIANCO)
    d = ImageDraw.Draw(img)
    quale = SCHERMATE.get(vista.get("schermata", "libero"), "libero")

    if quale == "libero":
        _schermo_libero(d, larghezza, altezza, vista)
    elif quale == "presenza":
        _schermo_presenza(d, larghezza, altezza, vista)
    elif quale == "qr":
        _schermo_qr(d, img, larghezza, altezza, vista)
    elif quale == "servizio":
        if altezza < 200:      # pannelli a striscia bassa: layout ridotto
            _schermo_servizio_compatto(d, img, larghezza, altezza, vista)
        else:
            _schermo_servizio(d, img, larghezza, altezza, vista)
    elif quale == "conto":
        _schermo_conto(d, img, larghezza, altezza, vista)
    else:
        _schermo_pulizia(d, larghezza, altezza, vista)
    return img


def png_bytes(img: Image.Image) -> bytes:
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()
