"""Driver del pannello e-ink, con un finto che salva PNG per sviluppare sul Mac."""
from __future__ import annotations

from pathlib import Path

from PIL import Image


class DisplayPng:
    """Scrive l'immagine su file: e' il display quando non hai il display."""

    def __init__(self, percorso: str = "/tmp/leggimenu-eink.png", scala: int = 2) -> None:
        self.percorso = Path(percorso)
        self.scala = scala

    def mostra(self, img: Image.Image, parziale: bool = False) -> None:
        out = img.convert("L")
        if self.scala != 1:
            out = out.resize((img.width * self.scala, img.height * self.scala), Image.NEAREST)
        out.save(self.percorso)

    def dormi(self) -> None:
        pass


class DisplayWaveshare:
    """Pannello Waveshare e-Paper collegato allo SPI del Raspberry.

    `modello` e' il nome del modulo dentro waveshare_epd, es. "epd4in2_V2",
    "epd2in9_V2", "epd2in13_V4". La libreria si installa con:
        git clone https://github.com/waveshareteam/e-Paper
        pip install ./e-Paper/RaspberryPi_JetsonNano/python
    """

    def __init__(self, modello: str = "epd4in2_V2", refresh_completo_ogni: int = 10) -> None:
        import importlib

        modulo = importlib.import_module(f"waveshare_epd.{modello}")
        self.epd = modulo.EPD()
        self.epd.init()
        self.epd.Clear()
        self.parziali = 0
        self.refresh_completo_ogni = refresh_completo_ogni
        self.supporta_parziale = hasattr(self.epd, "display_Partial") or hasattr(self.epd, "displayPartial")

    def mostra(self, img: Image.Image, parziale: bool = False) -> None:
        # il pannello vuole l'immagine nella sua risoluzione nativa
        if (img.width, img.height) != (self.epd.width, self.epd.height):
            img = img.resize((self.epd.width, self.epd.height))
        buf = self.epd.getbuffer(img.convert("1"))

        usa_parziale = parziale and self.supporta_parziale and \
            self.parziali < self.refresh_completo_ogni
        if usa_parziale:
            metodo = getattr(self.epd, "display_Partial", None) or getattr(self.epd, "displayPartial")
            metodo(buf)
            self.parziali += 1
        else:
            if hasattr(self.epd, "init_fast"):
                self.epd.init_fast()
            self.epd.display(buf)
            self.parziali = 0

    def dormi(self) -> None:
        try:
            self.epd.sleep()
        except Exception:
            pass


def costruisci(tipo: str, **kw):
    if tipo == "waveshare":
        return DisplayWaveshare(modello=kw.get("modello", "epd4in2_V2"))
    return DisplayPng(percorso=kw.get("percorso", "/tmp/leggimenu-eink.png"))
