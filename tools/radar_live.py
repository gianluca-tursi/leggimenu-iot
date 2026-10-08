#!/usr/bin/env python3
"""Radar live: legge l'LD2450 dalla seriale e lo mostra nel browser.

    ./scripts/radar-live.sh          ->  http://localhost:8080

Il nodo continua a stampare il suo testo di diagnosi: qui lo parsiamo e lo
ributtiamo fuori come JSON su WebSocket, cosi' la pagina disegna in tempo reale.
"""
from __future__ import annotations

import atexit
import os
import pathlib
import uuid
import sys
import hmac
import hashlib
import asyncio
import glob
import json
import math
import re
import time
from contextlib import asynccontextmanager
from pathlib import Path

import serial
import uvicorn
from fastapi import (FastAPI, File, Form, Request, UploadFile, WebSocket,
                     WebSocketDisconnect)
from fastapi.responses import (FileResponse, HTMLResponse, JSONResponse,
                               RedirectResponse)

QUI = Path(__file__).parent
RE_N = re.compile(r"bersagli:\s*(\d+)")
RE_T = re.compile(r"\[(\d)\]\s*x=(-?\d+)cm\s+y=(-?\d+)cm\s+dist=([\d.]+)cm\s+v=(-?\d+)")
# La riga di diagnosi dello sketch: la inoltro anch'io, cosi' la pagina sa
# distinguere "radar muto" da "dashboard rotta".
# accetta sia il vecchio formato sia quello nuovo del firmware nodo_wifi
RE_D = re.compile(r"byte(?: ricevuti:)?[=\s]+(\d+).*?frame(?: validi:)?[=\s]+(\d+)")
RE_RETE  = re.compile(r"ip=(\S+)\s+rssi=(-?\d+)")
RE_RADAR = re.compile(r"radar=(ok|MUTO)")
RE_FASE = re.compile(r"fase:\s*(\w+)\s+cand=(\d+)\s+conf=(\d+)\s+manca=([\d.]+)")
# Quanti LED il firmware crede di aver acceso. Serve per capire, quando la
# striscia resta spenta, se sbaglia il programma o se sbaglia il filo.
RE_LUCE = re.compile(r">>> luce:\s*(\d+)")
# La pausa dopo la liberazione, in secondi. La ricordo perche' una dashboard
# aperta dopo l'avvio del nodo non vedrebbe mai il valore corrente.
RE_PAUSA = re.compile(r"\[pausa\]\s*(\d+)")
RE_APERTO = re.compile(r">>> tavolo aperto:\s*(\d+)")

PAGINA_MENU = """<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>leggimenu - Tavolo {{tavolo}}</title>
<style>
*{box-sizing:border-box}
body{margin:0;font:16px/1.5 -apple-system,system-ui,sans-serif;background:#faf8f5;color:#231f1c}
.t{padding:26px 20px 120px}
h1{margin:0 0 2px;font-size:26px}.s{color:#8a807a;font-size:14px;margin-bottom:22px}
.p{border-top:1px solid #e6e0d8;padding:13px 0;display:flex;align-items:center;gap:12px}
.p .n{flex:1}.p b{font-weight:600;display:block}.p .e{color:#8a807a;font-size:14px}
.q{display:flex;align-items:center;gap:10px}
.q button{width:34px;height:34px;border-radius:17px;border:1px solid #d6cec3;
  background:#fff;font-size:19px;line-height:1;color:#231f1c}
.q span{min-width:16px;text-align:center;font-variant-numeric:tabular-nums}
.b{position:fixed;left:0;right:0;bottom:0;padding:14px 20px 26px;background:#fff;
  border-top:1px solid #e6e0d8}
.b button{width:100%;padding:15px;border:0;border-radius:11px;background:#1f7a3f;
  color:#fff;font-size:17px;font-weight:600}
.b button:disabled{background:#cfcac3}
.m{text-align:center;color:#8a807a;font-size:14px;margin-top:9px;min-height:19px}
</style></head><body><div class="t">
<h1>leggimenu</h1><div class="s">Tavolo {{tavolo}}</div>
<div id="lista"></div></div>
<div class="b"><button id="invia" disabled>Invia l'ordine</button>
<div class="m" id="msg"></div></div>
<script>
const PIATTI=__PIATTI__;
const qta=PIATTI.map(()=>0);
const lista=document.getElementById('lista');
PIATTI.forEach((p,i)=>{
  const d=document.createElement('div'); d.className='p';
  d.innerHTML=`<div class="n"><b>${p.nome}</b><span class="e">${p.prezzo} euro</span></div>
    <div class="q"><button data-i="${i}" data-d="-1">&minus;</button>
    <span id="q${i}">0</span><button data-i="${i}" data-d="1">+</button></div>`;
  lista.appendChild(d);
});
lista.onclick=e=>{
  const b=e.target.closest('button'); if(!b) return;
  const i=+b.dataset.i;
  qta[i]=Math.max(0,Math.min(9,qta[i]+ +b.dataset.d));
  document.getElementById('q'+i).textContent=qta[i];
  document.getElementById('invia').disabled = qta.every(q=>q===0);
};
document.getElementById('invia').onclick=async()=>{
  const righe=PIATTI.map((p,i)=>({id:p.id,nome:p.nome,qta:qta[i],prezzo:p.prezzo}))
                     .filter(r=>r.qta>0);
  const msg=document.getElementById('msg'), bot=document.getElementById('invia');
  bot.disabled=true; msg.textContent='invio...';
  try{
    const r=await fetch('/api/ordine',{method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({tavolo:'{{tavolo}}',righe})});
    const d=await r.json();
    msg.textContent = d.ok ? 'Ordine inviato in cucina' : ('non riuscito: '+(d.motivo||'?'));
  }catch(e){ msg.textContent='non riuscito: '+e; bot.disabled=false; }
};
</script></body></html>"""
ultima_pausa: list[int] = []
PORTA_HTTP: list[int] = []

# File di PID: identifica la dashboard in modo inequivocabile. Cercarla con
# "pgrep -f radar_live.py" non funziona, perche' quel pattern matcha anche la
# shell che sta eseguendo il comando in cui compare la stringa - e un pkill
# dello stesso tipo si porta via la shell stessa.
FILE_PID = pathlib.Path("/tmp/leggimenu-dashboard.pid")

# Il menu e le regole del vino stanno in backend/: li' ci sono le categorie e i
# tag dei piatti, senza i quali l'abbinamento non ha su cosa ragionare.
sys.path.insert(0, str(QUI.parent / "backend"))
try:
    from wine import MENU_BY_ID, suggerisci          # type: ignore
    from models import RigaOrdine                    # type: ignore
    VINI_CARTA = json.loads(
        (QUI.parent / "backend" / "data" / "wines.json").read_text())["vini"]
except Exception as _e:                              # pragma: no cover
    MENU_BY_ID, suggerisci, RigaOrdine, VINI_CARTA = {}, None, None, []
    print(f"[vino] motore non disponibile: {_e}")


def piatti_ordinabili() -> list[dict]:
    """Quello che il cliente vede sul telefono: le portate piu' qualche vino.

    I vini servono anche a far funzionare il suggerimento: senza poterli
    ordinare, "il vino non e' stato ordinato" sarebbe sempre vero."""
    voci = [{"id": p["id"], "nome": p["nome"], "prezzo": p["prezzo"],
             "categoria": p["categoria"]}
            for p in MENU_BY_ID.values()]
    for v in VINI_CARTA[:3]:
        voci.append({"id": v["id"], "nome": f"{v['nome']} (calice)",
                     "prezzo": v["calice"], "categoria": "bevanda"})
    return voci


# --- offerte ed eventi ----------------------------------------------------
# Roba del gestore, non del tavolo: resta su disco e sopravvive ai riavvii,
# perche' un evento lo si prepara la mattina e lo si manda la sera.
DATI = QUI.parent / "dati"
IMMAGINI = DATI / "immagini"
FILE_EVENTI = DATI / "eventi.json"
DATI.mkdir(exist_ok=True)
IMMAGINI.mkdir(exist_ok=True)


def leggi_eventi() -> list[dict]:
    try:
        return json.loads(FILE_EVENTI.read_text())
    except Exception:
        return []


def scrivi_eventi(lista: list[dict]) -> None:
    FILE_EVENTI.write_text(json.dumps(lista, ensure_ascii=False, indent=1))


async def al_pannello(titolo: str, riga1: str = "", riga2: str = "") -> bool:
    """Un annuncio sul pannello del tavolo: tre righe e via."""
    if not seriale_aperta:
        return False
    # Una riga sola: cosi' non puo' restare mezzo annuncio vecchio sotto il
    # titolo nuovo se un pezzo si perde per strada.
    pezzi = [accorcia(titolo, 22), accorcia(riga1, 26), accorcia(riga2, 26)]
    try:
        seriale_aperta[0].write(("D:" + "|".join(p.replace("|", " ") for p in pezzi)
                                 + "\n").encode())
        return True
    except Exception:
        return False


def accorcia(testo: str, massimo: int) -> str:
    """Taglia una riga per il pannello senza spezzare le parole.

    Tagliare al carattere faceva finire "Tagliatelle al ragu di cinghiale" in
    "...di c": il pezzo mozzato non si capisce ed e' peggio di una riga corta.
    """
    if len(testo) <= massimo:
        return testo
    corto = testo[:massimo].rstrip()
    spazio = corto.rfind(" ")
    if spazio > massimo // 2:           # se resta abbastanza, taglio a parola
        corto = corto[:spazio]
    return corto + "."


def abbina_vino(righe: list[dict], coperti: int):
    """Il vino da proporre, o None se non ha senso proporlo."""
    if not suggerisci or not RigaOrdine:
        return None
    # Se il vino l'hanno gia' ordinato, proporlo e' maleducazione.
    if any(MENU_BY_ID.get(r.get("id"), {}).get("categoria") == "bevanda"
           or str(r.get("id", "")).startswith("vin-") for r in righe):
        return None
    ordine = []
    for r in righe:
        p = MENU_BY_ID.get(r.get("id"))
        if not p:
            continue
        ordine.append(RigaOrdine(id=p["id"], piatto_id=p["id"], nome=p["nome"],
                                 categoria=p["categoria"], prezzo=p["prezzo"],
                                 quantita=int(r.get("qta", 1)), tag=p["tag"]))
    if not ordine:
        return None
    return suggerisci(ordine, max(coperti, 1))
# Il WiFi si offre una volta sola per sessione al tavolo: alla seconda
# scansione il cliente lo ha gia' visto, e ripeterglielo gli toglierebbe il
# QR del menu proprio mentre lo sta usando. Si riarma quando il tavolo si libera.
wifi_offerto: list[bool] = [False]

# --- la vita del tavolo ---------------------------------------------------
# Il gestionale cassa non guarda il radar: guarda cosa e' successo e quando.
# Tengo gli eventi qui cosi' una pagina aperta a meta' servizio vede comunque
# tutto quello che e' gia' accaduto, invece di partire vuota.
eventi: list[dict] = []

# L'ordine in corso e a che punto e'. Il percorso e' quello vero di una sala:
#   inviato -> in preparazione -> servito -> conto
# La cassa mostra da quanti minuti e' fermo nello stato in cui sta, perche' e'
# l'unica cosa che fa agire qualcuno.
STATI = ["inviato", "preparazione", "servito", "conto", "pagato"]
ordine_corrente: dict = {}
offerta_viva: dict = {}
evento_vivo: dict = {}
# Chi ha inquadrato il QR, in questa sessione. Il biscotto da solo non basta:
# la fotocamera di iOS apre il link in una finestra che spesso non lo conserva,
# e lo stesso telefono veniva contato una volta per scansione. Quindi riconosco
# l'apparecchio anche da indirizzo di rete + modello di browser, e il biscotto
# serve a distinguere due telefoni identici sulla stessa rete.
dispositivi: dict[str, dict] = {}


def stato_tavolo() -> dict:
    """Com'e' messo il tavolo adesso, per chi arriva a servizio iniziato."""
    return {"tipo": "tavolo",
            "aperto": bool(apertura),
            "coperti": apertura[1] if len(apertura) > 1 else 0,
            "confermato": confermato[0]}


def riconosci(biscotto: str | None, ip: str, ua: str) -> tuple[str, bool]:
    """Restituisce (identita', e' la prima volta che lo vedo)."""
    if biscotto and biscotto in dispositivi:
        return biscotto, False
    for chiave, d in dispositivi.items():
        if d["ip"] == ip and d["ua"] == ua:
            return chiave, False
    chiave = biscotto or uuid.uuid4().hex[:8]
    dispositivi[chiave] = {"ip": ip, "ua": ua, "ts": []}
    return chiave, True


def statistiche_scansioni() -> dict:
    """Quante volte riaprono il menu, e ogni quanto."""
    tutti = sorted(t for d in dispositivi.values() for t in d["ts"])
    riaperture = sum(max(0, len(d["ts"]) - 1) for d in dispositivi.values())
    intervalli = []
    for d in dispositivi.values():
        ts = sorted(d["ts"])
        intervalli += [b - a for a, b in zip(ts, ts[1:])]
    return {
        "dispositivi": len(dispositivi),
        "scansioni": len(tutti),
        "riaperture": riaperture,
        # La media la calcolo per telefono, non sul totale: due telefoni che
        # inquadrano insieme non sono "una riapertura ogni zero secondi".
        "ogni": int(sum(intervalli) / len(intervalli)) if intervalli else 0,
    }
apertura: list[float] = []         # quando si e' aperto il tavolo
# I coperti li ha confermati qualcuno in sala? Serve come STATO e non come
# evento: una cassa aperta a meta' servizio, o ricaricata, l'evento non lo
# rivede piu' e non saprebbe se chiedere conferma o no.
confermato: list[bool] = [False]


def registra_evento(tipo: str, **dati) -> dict:
    ev = {"tipo": "evento", "evento": tipo, "ts": time.time(),
          "ora": time.strftime("%H:%M"), **dati}
    eventi.append(ev)
    del eventi[:-60]                # ne bastano gli ultimi
    return ev


async def manda_a_tutti(msg: dict) -> None:
    for c in list(clienti):
        try:
            await c.send_json(msg)
        except Exception:
            pass
# Quanti bersagli fissi il firmware sta scartando come arredamento.
RE_FISSI = re.compile(r"\[fissi\]\s*ignorati=(\d+)\s+taratura=(\w+)(?:\s+tavolo=(\d+))?")

def avvia_guardiano_ricarica():
    """Sorveglia il file RICARICA e chiede a gunicorn di ricambiare i worker.

    E' il modo in cui si pubblica senza root: si copiano i file con scp e si
    tocca RICARICA. Il capo di gunicorn, ricevuto un SIGHUP, avvia worker nuovi
    col codice nuovo e lascia finire quelli vecchi - niente interruzioni.

    Attivo solo se RICARICA_ATTIVA=1, perche' in locale il processo padre e' la
    shell e mandarle un SIGHUP la chiuderebbe.
    """
    if os.environ.get("RICARICA_ATTIVA") != "1":
        return None

    import signal
    import threading

    segnale = pathlib.Path(os.environ.get("RICARICA_FILE", "RICARICA")).resolve()
    basta = threading.Event()

    def guarda() -> None:
        visto = segnale.stat().st_mtime if segnale.exists() else 0.0
        while not basta.wait(2.0):
            try:
                ora = segnale.stat().st_mtime if segnale.exists() else 0.0
            except OSError:
                continue
            if ora != visto:
                visto = ora
                print(f"[ricarica] {segnale.name} toccato: ricambio i worker",
                      flush=True)
                os.kill(os.getppid(), signal.SIGHUP)

    t = threading.Thread(target=guarda, daemon=True, name="ricarica")
    t.start()
    print(f"[ricarica] sorveglio {segnale}", flush=True)
    return basta


@asynccontextmanager
async def lifespan(app: FastAPI):
    coda: asyncio.Queue = asyncio.Queue()
    loop = asyncio.get_running_loop()
    loop.run_in_executor(None, leggi_seriale, coda, loop)

    async def pompa() -> None:
        while True:
            msg = await coda.get()
            if msg.get("tipo") == "frame":
                ultimo.update(msg)
            elif msg.get("tipo") == "apertura":
                # Il tavolo si apre una volta per servizio: i successivi
                # annunci sono il conteggio che sale, non un tavolo nuovo.
                n = msg.get("coperti", 0)
                primo = not apertura
                if primo:
                    apertura[:] = [time.time(), n]
                elif n == apertura[1]:
                    continue                      # niente di nuovo, taccio
                else:
                    apertura[1] = n
                await diffondi(registra_evento(
                    "aperto" if primo else "coperti", coperti=n))
                # Anche qui, non solo nel percorso del ponte: lo stato deve
                # arrivare alla cassa subito, altrimenti la finestra dei
                # coperti compare solo ricaricando la pagina.
                await diffondi(stato_tavolo())
                continue
            await diffondi(msg)

    task = asyncio.create_task(pompa())
    guardiano = avvia_guardiano_ricarica()
    yield
    task.cancel()
    if guardiano:
        guardiano.set()


app = FastAPI(title="radar live", lifespan=lifespan)


def ip_vero(request: Request) -> str:
    """L'indirizzo del telefono, non quello del tunnel.

    Dietro un tunnel ogni richiesta arriva da localhost: senza questo, tutti i
    telefoni della fiera risulterebbero lo stesso apparecchio.
    """
    for h in ("cf-connecting-ip", "x-forwarded-for"):
        v = request.headers.get(h)
        if v:
            return v.split(",")[0].strip()
    return request.client.host if request.client else "?"


def da_fuori(request: Request) -> bool:
    """La richiesta arriva da internet e non dalla rete di casa?

    In fiera il menu deve essere raggiungibile dal telefono di chiunque, quindi
    il server finisce dietro un tunnel pubblico. Ma allora diventano pubbliche
    anche la cassa e i comandi: chiunque inquadri il QR potrebbe curiosare e
    premere "libera il tavolo" mentre la gente mangia.

    Un tunnel inoltra tutto a localhost, quindi guardare l'indirizzo del
    chiamante non basta: tutto sembrerebbe locale. Le intestazioni che il
    tunnel aggiunge invece ci sono solo quando si arriva da fuori.
    """
    if request.headers.get("x-forwarded-for") or request.headers.get("cf-connecting-ip"):
        return True
    ip = request.client.host if request.client else ""
    return not (ip.startswith(("127.", "10.", "192.168.", "::1"))
                or ip.startswith(tuple(f"172.{n}." for n in range(16, 32))))


TAVOLO_PREDEFINITO = "7"

# --- chi puo' entrare nel gestionale --------------------------------------
# Finche' il server girava sul Mac bastava guardare da che rete arrivava la
# richiesta. Ora che sta su internet quella distinzione non esiste piu': da
# fuori arrivano tutti, anche il cameriere. Quindi serve una parola.
#
# Se non e' impostata nessuna password si torna al controllo di rete, cosi' in
# locale si continua a lavorare senza doverla digitare ogni volta.
FILE_PASSWORD = DATI / "password.txt"


def password_cassa() -> str:
    dalla_env = os.environ.get("LEGGIMENU_PASSWORD", "").strip()
    if dalla_env:
        return dalla_env
    try:
        return FILE_PASSWORD.read_text().strip()
    except Exception:
        return ""


def gettone(pw: str) -> str:
    """Un biscotto che non si puo' inventare senza conoscere la password."""
    return hmac.new(pw.encode(), b"cassa-leggimenu", hashlib.sha256).hexdigest()


def puo_entrare(request: Request) -> bool:
    pw = password_cassa()
    if not pw:
        return not da_fuori(request)          # come prima: vale la rete
    return hmac.compare_digest(request.cookies.get("lm_cassa", ""), gettone(pw))


PAGINA_ENTRA = """<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>leggimenu</title><style>
body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;
background:#f5f5f7;color:#1d1d1f;font:16px/1.5 -apple-system,system-ui,sans-serif;padding:24px}
form{background:#fff;padding:30px 26px;border-radius:20px;max-width:330px;width:100%;
box-shadow:0 1px 2px rgba(0,0,0,.04),0 8px 30px rgba(0,0,0,.06)}
h1{margin:0 0 4px;font-size:22px;letter-spacing:-.02em}
p{margin:0 0 20px;color:#86868b;font-size:14px}
input{width:100%;box-sizing:border-box;padding:13px;border:1px solid #e8e8ed;
border-radius:12px;font:16px inherit;margin-bottom:12px}
button{width:100%;padding:14px;border:0;border-radius:12px;background:#0071e3;
color:#fff;font:16px inherit;font-weight:500}
.no{color:#d7263d;font-size:14px;margin-top:12px;min-height:20px}
@media (prefers-color-scheme:dark){body{background:#000;color:#f5f5f7}
form{background:#1c1c1e}input{background:#000;border-color:#2c2c2e;color:#f5f5f7}}
</style></head><body>
<form id="f"><h1>leggimenu</h1><p>gestionale cassa</p>
<input id="pw" type="password" placeholder="password" autofocus autocomplete="current-password">
<button>Entra</button><div class="no" id="no"></div></form>
<script>
document.getElementById('f').onsubmit = async e => {
  e.preventDefault();
  const r = await fetch('/api/entra', {method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({password: document.getElementById('pw').value})});
  const d = await r.json();
  if (d.ok) location.href = '/cassa';
  else document.getElementById('no').textContent = 'password sbagliata';
};
</script></body></html>"""

CHIUSO = """<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>leggimenu</title><style>
body{margin:0;min-height:100vh;display:flex;align-items:center;
justify-content:center;background:#faf8f5;color:#231f1c;text-align:center;
font:16px/1.6 -apple-system,system-ui,sans-serif;padding:30px}
h1{font-size:22px;margin:0 0 8px}p{color:#8a807a;margin:0;max-width:300px}
</style></head><body><div><h1>leggimenu</h1>
<p>Questa parte si apre solo dai dispositivi del locale.<br>
Per il menu, inquadra il codice sul tavolo.</p></div></body></html>"""


@app.middleware("http")
async def niente_copie_vecchie(request: Request, call_next):
    """Le pagine non vanno tenute in memoria dal browser.

    Pubblichiamo spesso, e una pagina vecchia e' indistinguibile da un guasto:
    il server ha i dati giusti, la pagina mostra altro, e si cerca il problema
    dalla parte sbagliata. E' gia' successo.
    """
    r = await call_next(request)
    if r.headers.get("content-type", "").startswith("text/html"):
        r.headers["Cache-Control"] = "no-store, must-revalidate"
    return r


@app.middleware("http")
async def chiudi_il_gestionale(request: Request, call_next):
    """Al cliente il menu e gli ordini; il resto a chi ha la password."""
    via = request.url.path

    pubblico = (via.startswith("/m/") or via.startswith("/menu/")
                or via == "/api/ordine"
                or via.startswith("/img/") or via.startswith("/static")
                or via in ("/entra", "/api/entra"))
    if pubblico or puo_entrare(request):
        return await call_next(request)

    # Chi inquadra un QR con il solo indirizzo, senza /m/<tavolo>, finirebbe
    # davanti alla richiesta della password senza capire perche'. Lo porto al
    # menu: e' quello che voleva, e il QR non deve essere perfetto per
    # funzionare.
    #
    # Questo controllo sta DOPO quello della password, non prima: altrimenti
    # manderebbe al menu anche chi e' entrato, e la dashboard del radar - che
    # vive proprio su "/" - diventerebbe irraggiungibile.
    if via == "/":
        return RedirectResponse(f"/menu/{TAVOLO_PREDEFINITO}")

    # A una pagina mando la richiesta della password; a una chiamata di
    # servizio un errore, perche' una pagina di login dentro una risposta
    # automatica non la capirebbe nessuno.
    if via.startswith("/api/") or via.startswith("/ws"):
        return JSONResponse({"errore": "serve la password"}, status_code=401)
    return RedirectResponse("/entra")


@app.get("/entra")
async def entra_pagina() -> HTMLResponse:
    return HTMLResponse(PAGINA_ENTRA)


@app.post("/api/entra")
async def entra(dati: dict):
    pw = password_cassa()
    if not pw:
        return {"ok": False, "motivo": "nessuna password impostata"}
    if not hmac.compare_digest(str(dati.get("password", "")), pw):
        time.sleep(0.6)                      # scoraggia chi prova a tentativi
        return {"ok": False}
    r = JSONResponse({"ok": True})
    r.set_cookie("lm_cassa", gettone(pw), max_age=60 * 60 * 24 * 30,
                 httponly=True, samesite="lax")
    return r
clienti: set[WebSocket] = set()
seriale_aperta: list = []          # la Serial viva, per poter mandare comandi

# --- il ponte -------------------------------------------------------------
# Quando il server non sta sulla stessa macchina del nodo, il Mac a cui il
# nodo e' attaccato apre un ponte: legge la seriale e la rigira qui, e porta
# indietro i comandi. L'ESP32 cosi' non ha bisogno di WiFi suo - alla fiera e'
# meglio avere una sola macchina collegata invece di due.
ponte_vivo: bool = False
SEGRETO = os.environ.get("LEGGIMENU_SEGRETO", "")


class Ponte:
    """Si comporta come la seriale, ma scrive dall'altra parte del mondo."""

    def __init__(self, sock: WebSocket, loop):
        self.sock, self.loop = sock, loop

    def write(self, dati: bytes) -> None:
        # Chiamato anche da codice sincrono: accodo e lascio fare al loop.
        asyncio.run_coroutine_threadsafe(
            self.sock.send_json({"comando": dati.decode("utf-8", "replace")}),
            self.loop)
ultimo = {"ts": 0.0, "bersagli": [], "n": 0}


def trova_porta() -> str | None:
    p = [x for x in glob.glob("/dev/cu.*")
         if any(k in x for k in ("wchusbserial", "usbmodem", "usbserial", "SLAB"))]
    return p[0] if p else None


async def diffondi(msg: dict) -> None:
    testo = json.dumps(msg)
    for ws in list(clienti):
        try:
            await ws.send_text(testo)
        except Exception:
            clienti.discard(ws)


class Lettore:
    """Trasforma le righe del nodo in messaggi per la dashboard.

    Le righe arrivano da due strade diverse - il cavo USB qui, oppure un ponte
    che le manda da un'altra macchina - ma vanno interpretate allo stesso modo.
    Lo stato (il gruppo di bersagli in costruzione) vive qui dentro invece che
    nel ciclo di lettura, cosi' le due strade non si calpestano.
    """

    def __init__(self, manda):
        self.manda = manda            # dove depositare i messaggi pronti
        self.parziale = ""
        self.gruppo: list[dict] = []
        self.attesi = 0
        self.ultima_fase: dict = {}

    def pezzo(self, dati: str) -> None:
        """Un pezzo di testo appena arrivato, anche a meta' riga."""
        self.parziale += dati
        *linee, self.parziale = self.parziale.split("\n")
        for riga in linee:
            self.riga(riga)

    def riga(self, riga: str) -> None:
        m = RE_N.search(riga)
        if m:
            if self.attesi or self.gruppo:      # chiudo il gruppo precedente
                self.manda({
                    "tipo": "frame", "ts": time.time(),
                    "bersagli": self.gruppo, "n": len(self.gruppo), **self.ultima_fase})
            self.gruppo = []
            self.attesi = int(m.group(1))
            if self.attesi == 0:
                self.manda({
                    "tipo": "frame", "ts": time.time(), "bersagli": [], "n": 0,
                    **self.ultima_fase})
            return
        fx = RE_FISSI.search(riga)
        if fx:
            self.manda(
                {"tipo": "fissi", "n": int(fx.group(1)),
                 "taratura": fx.group(2) == "si"})
            if fx.group(3) is not None and int(fx.group(3)) > 0:
                self.manda(
                    {"tipo": "apertura", "coperti": int(fx.group(3))})
            return
        ap = RE_APERTO.search(riga)
        if ap:
            self.manda(
                {"tipo": "apertura", "coperti": int(ap.group(1))})
            return
        pa = RE_PAUSA.search(riga)
        if pa:
            ultima_pausa[:] = [int(pa.group(1))]
            self.manda(
                                      {"tipo": "pausa", "secondi": int(pa.group(1))})
            return
        lc = RE_LUCE.search(riga)
        if lc:
            self.manda(
                                      {"tipo": "luce", "led": int(lc.group(1))})
            return
        fs = RE_FASE.search(riga)
        if fs:
            self.ultima_fase = {"fase": fs.group(1), "candidati": int(fs.group(2)),
                           "confermati": int(fs.group(3)), "manca": float(fs.group(4))}
            return
        dg = RE_D.search(riga)
        if dg:
            msg = {"tipo": "diagnosi", "byte": int(dg.group(1)),
                   "frame": int(dg.group(2))}
            rete = RE_RETE.search(riga)
            if rete:
                msg["ip"] = rete.group(1)
                msg["rssi"] = int(rete.group(2))
            rad = RE_RADAR.search(riga)
            if rad:
                msg["radar"] = (rad.group(1) == "ok")
            self.manda(msg)
            return
        t = RE_T.search(riga)
        if t:
            x, y = int(t.group(2)), int(t.group(3))
            self.gruppo.append({
                "id": int(t.group(1)), "x": x, "y": y,
                "dist": float(t.group(4)), "v": int(t.group(5)),
                "ang": round(math.degrees(math.atan2(x, max(y, 1))), 1),
            })


def leggi_seriale(coda: asyncio.Queue, loop: asyncio.AbstractEventLoop) -> None:
    """Gira in un thread: la seriale e' bloccante, l'event loop no."""
    porta = None
    ser = None
    lettore = Lettore(lambda m: loop.call_soon_threadsafe(coda.put_nowait, m))

    while True:
        if ser is None:
            if ponte_vivo:
                time.sleep(1.0)       # il nodo arriva dal ponte, non da qui
                continue
            porta = trova_porta()
            if porta is None:
                loop.call_soon_threadsafe(coda.put_nowait,
                                          {"tipo": "stato", "collegato": False})
                time.sleep(1.5)
                continue
            try:
                ser = serial.Serial(porta, 115200, timeout=0.2)
                seriale_aperta.clear(); seriale_aperta.append(ser)
                loop.call_soon_threadsafe(coda.put_nowait,
                                          {"tipo": "stato", "collegato": True, "porta": porta})
            except Exception:
                ser = None
                time.sleep(1.5)
                continue
        try:
            dati = ser.read(2048).decode("utf-8", "replace")
        except Exception:
            try: ser.close()
            except Exception: pass
            ser = None
            seriale_aperta.clear()
            continue
        if not dati:
            continue
        lettore.pezzo(dati)


@app.post("/api/reset")
async def reset():
    """Il tasto "nuova prova": manda 'R' al nodo, che azzera la sequenza."""
    if not seriale_aperta:
        return {"ok": False, "motivo": "nodo non collegato"}
    try:
        seriale_aperta[0].write(b"R\n")
        return {"ok": True}
    except Exception as e:
        return {"ok": False, "motivo": str(e)}


@app.post("/api/libera")
async def libera():
    """Il pasto e' finito: chiude il tavolo e rimanda il pannello a "prenotato"."""
    if not seriale_aperta:
        return {"ok": False, "motivo": "nodo non collegato"}
    try:
        seriale_aperta[0].write(b"F\n")
        wifi_offerto[0] = False        # tavolo nuovo, suggerimento di nuovo utile
        durata = int(time.time() - apertura[0]) if apertura else 0
        await manda_a_tutti(registra_evento("liberato", durata=durata))
        ordine_corrente.clear()
        eventi.clear()                 # servizio chiuso: la cassa riparte pulita
        dispositivi.clear()
        apertura.clear()
        confermato[0] = False
        await manda_a_tutti(stato_tavolo())
        return {"ok": True}
    except Exception as e:
        return {"ok": False, "motivo": str(e)}


def _ip_locale() -> str:
    """L'indirizzo del Mac sulla rete di casa: il telefono deve raggiungerlo,
    quindi 127.0.0.1 non serve a niente. Non apro davvero la connessione."""
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


@app.get("/api/rete")
async def rete():
    """Dove puntare il QR del menu, gia' pronto da incollare.

    Oltre all'indirizzo numerico do anche il nome di rete del Mac: quello non
    cambia quando il router riassegna gli indirizzi, e un QR stampato o gia'
    disegnato sul pannello continua a funzionare. Lo capiscono tutti gli iPhone
    e gli Android recenti, ma non quelli vecchi: percio' il numerico resta la
    scelta sicura e il nome e' l'alternativa comoda."""
    import subprocess
    try:
        nome = subprocess.run(["scutil", "--get", "LocalHostName"],
                              capture_output=True, text=True, timeout=2).stdout.strip()
    except Exception:
        nome = ""
    return {"ip": _ip_locale(), "porta": PORTA_HTTP[0] if PORTA_HTTP else 8080,
            "nome": f"{nome}.local" if nome else ""}


@app.get("/menu/{tavolo}")
@app.get("/m/{tavolo}")
async def menu(tavolo: str, request: Request):
    """La pagina che si apre inquadrando il QR sul pannello.

    E' anche il rilevatore: se qualcuno la chiede, qualcuno ha scansionato.
    Avviso il nodo, che accende la schermata del WiFi sul pannello."""
    primo = not wifi_offerto[0]
    if primo and seriale_aperta:
        try:
            seriale_aperta[0].write(b"W\n")
            wifi_offerto[0] = True
        except Exception:
            pass
    suo, nuovo = riconosci(request.cookies.get("lm_disp"),
                           ip_vero(request),
                           request.headers.get("user-agent", "?"))
    dispositivi[suo]["ts"].append(time.time())
    st = statistiche_scansioni()

    await manda_a_tutti({"tipo": "scansione", "tavolo": tavolo, "primo": primo})
    await manda_a_tutti(registra_evento("scansione", tavolo=tavolo,
                                        nuovo=nuovo, **st))

    r = HTMLResponse(PAGINA_MENU.replace("{{tavolo}}", tavolo)
                                .replace("__PIATTI__", json.dumps(piatti_ordinabili())))
    r.set_cookie("lm_disp", suo, max_age=60 * 60 * 12, samesite="lax")
    return r


@app.post("/api/qr")
async def qr(dati: dict):
    """Indirizzo del menu e credenziali della rete da mettere nei due QR."""
    if not seriale_aperta:
        return {"ok": False, "motivo": "nodo non collegato"}
    righe = []
    url = (dati.get("url") or "").strip()
    if url:
        righe.append(f"U:{url}\n".encode())
    ssid = (dati.get("ssid") or "").strip()
    if ssid:
        # Formato standard riconosciuto da iOS e Android per entrare in rete.
        chiave = (dati.get("password") or "").strip()
        righe.append(f"Q:WIFI:T:WPA;S:{ssid};P:{chiave};;\n".encode())
    if not righe:
        return {"ok": False, "motivo": "niente da mandare"}
    try:
        for r in righe:
            seriale_aperta[0].write(r)
            time.sleep(0.05)
        return {"ok": True, "mandate": len(righe)}
    except Exception as e:
        return {"ok": False, "motivo": str(e)}


@app.post("/api/ordine")
async def ordine(dati: dict):
    """L'ordine mandato dal telefono: va alla cassa e sul pannello del tavolo."""
    righe = dati.get("righe") or []
    tavolo = str(dati.get("tavolo", "?"))
    if not righe:
        return {"ok": False, "motivo": "ordine vuoto"}

    totale = sum(int(r.get("qta", 0)) * float(r.get("prezzo", 0)) for r in righe)
    ordine_corrente.clear()
    ordine_corrente.update({"righe": righe, "totale": totale, "stato": "inviato",
                            "da": time.time(), "tavolo": tavolo})
    await manda_a_tutti({"tipo": "ordine", "tavolo": tavolo, "righe": righe,
                         "quando": time.strftime("%H:%M:%S")})
    await manda_a_tutti(registra_evento("ordine", tavolo=tavolo, righe=righe,
                                        totale=totale))
    await manda_a_tutti({"tipo": "ordine_stato", **ordine_corrente})

    if seriale_aperta:
        try:
            seriale_aperta[0].write(b"Z\n")          # azzera la lista di prima
            time.sleep(0.05)
            for r in righe[:8]:
                q = int(r.get("qta", 1))
                testo = accorcia(f"{q}x {r.get('nome','')}", 24)
                seriale_aperta[0].write(f"V:{testo}\n".encode())
                time.sleep(0.05)
            seriale_aperta[0].write(b"X\n")          # mostra la schermata
        except Exception as e:
            return {"ok": True, "pannello": f"non raggiunto: {e}"}

    coperti = apertura[1] if len(apertura) > 1 else 2
    vino = abbina_vino(righe, coperti)
    if vino:
        n = vino.quantita_consigliata
        quanto = ("una bottiglia" if vino.formato_consigliato == "bottiglia"
                  else f"{n} {'calice' if n == 1 else 'calici'}")
        # "colore" e non "tipo": registra_evento ha gia' un parametro tipo e
        # i due si scontrerebbero.
        prop = {"nome": vino.nome, "colore": vino.tipo, "motivo": vino.motivo,
                "calice": vino.calice, "bottiglia": vino.bottiglia,
                "quanto": quanto}
        ordine_corrente["vino"] = prop
        await manda_a_tutti(registra_evento("vino", **prop))
        if seriale_aperta:
            try:
                # Al tavolo arriva solo l'essenziale: nome e perche'. Il
                # pannello ha 27 caratteri per riga, non ci sta un discorso.
                seriale_aperta[0].write(f"B:{vino.nome}\n".encode()[:40])
                time.sleep(0.05)
                seriale_aperta[0].write(f"C:{vino.motivo}\n".encode()[:40])
                time.sleep(0.05)
                seriale_aperta[0].write(b"Y\n")
            except Exception:
                pass
    return {"ok": True}


@app.post("/api/ordine/stato")
async def ordine_stato(dati: dict):
    """Avanza l'ordine: presa in carico, servito, conto."""
    nuovo = (dati.get("stato") or "").strip()
    if nuovo not in STATI:
        return {"ok": False, "motivo": "stato sconosciuto"}
    if not ordine_corrente:
        return {"ok": False, "motivo": "nessun ordine in corso"}
    atteso = int(time.time() - ordine_corrente.get("da", time.time()))
    ordine_corrente["stato"] = nuovo
    ordine_corrente["da"] = time.time()      # il cronometro riparte da qui
    await manda_a_tutti(registra_evento("stato", stato=nuovo, atteso=atteso))
    await manda_a_tutti({"tipo": "ordine_stato", **ordine_corrente})

    return {"ok": True}


@app.post("/api/coperti")
async def coperti(dati: dict):
    """I coperti confermati dal cameriere, che ha contato guardandoli."""
    try:
        n = max(0, min(8, int(dati.get("n", 0))))
    except (TypeError, ValueError):
        return {"ok": False, "motivo": "non e' un numero"}
    if apertura:
        apertura[1] = n
    else:
        apertura[:] = [time.time(), n]
    confermato[0] = True
    await manda_a_tutti(registra_evento("confermati", coperti=n))
    await manda_a_tutti(stato_tavolo())
    if seriale_aperta:
        try:
            seriale_aperta[0].write(f"K:{n}\n".encode())
        except Exception as e:
            return {"ok": True, "nodo": f"non raggiunto: {e}"}
    return {"ok": True}


@app.post("/api/offerta")
async def offerta(dati: dict):
    """Un'offerta su un piatto, mandata ai tavoli."""
    piatto = MENU_BY_ID.get(str(dati.get("piatto_id", "")))
    if not piatto:
        return {"ok": False, "motivo": "piatto sconosciuto"}
    try:
        sconto = max(1, min(90, int(dati.get("sconto", 0))))
    except (TypeError, ValueError):
        return {"ok": False, "motivo": "sconto non valido"}

    pieno = float(piatto["prezzo"])
    scontato = round(pieno * (100 - sconto) / 100, 2)
    off = {"piatto": piatto["nome"], "sconto": sconto,
           "prezzo": scontato, "pieno": pieno,
           "messaggio": (dati.get("messaggio") or "").strip()}
    offerta_viva.clear(); offerta_viva.update(off)

    await manda_a_tutti(registra_evento("offerta", **off))
    await manda_a_tutti({"tipo": "offerta", **off})
    arrivata = await al_pannello(piatto["nome"],
                                 f"-{sconto}% oggi",
                                 f"{scontato:.2f} invece di {pieno:.2f}".replace(".", ","))
    return {"ok": True, "pannello": arrivata}


@app.get("/api/piatti")
async def api_piatti():
    """La carta, per chi deve scegliere un piatto da scontare."""
    return {"piatti": piatti_ordinabili()}


@app.get("/api/eventi")
async def lista_eventi():
    return {"eventi": leggi_eventi()}


@app.post("/api/evento")
async def crea_evento(titolo: str = Form(...), descrizione: str = Form(""),
                      quando: str = Form(""), immagine: UploadFile | None = File(None)):
    """Un evento futuro da promuovere nelle giornate scarse."""
    ev = {"id": uuid.uuid4().hex[:8], "titolo": titolo.strip(),
          "descrizione": descrizione.strip(), "quando": quando.strip(),
          "immagine": "", "creato": time.time()}
    if immagine is not None and immagine.filename:
        est = pathlib.Path(immagine.filename).suffix.lower()
        if est not in (".jpg", ".jpeg", ".png", ".webp", ".gif"):
            return {"ok": False, "motivo": "formato immagine non accettato"}
        nome = f"{ev['id']}{est}"
        (IMMAGINI / nome).write_bytes(await immagine.read())
        ev["immagine"] = nome
    lista = leggi_eventi()
    lista.insert(0, ev)
    scrivi_eventi(lista[:30])
    return {"ok": True, "evento": ev}


@app.post("/api/evento/{eid}/invia")
async def invia_evento(eid: str):
    """Manda l'evento sui tavoli: sul pannello il titolo, il resto dietro al QR."""
    ev = next((e for e in leggi_eventi() if e["id"] == eid), None)
    if not ev:
        return {"ok": False, "motivo": "evento non trovato"}
    evento_vivo.clear(); evento_vivo.update(ev)
    await manda_a_tutti(registra_evento("promozione", titolo=ev["titolo"],
                                        quando=ev["quando"]))
    arrivata = await al_pannello(ev["titolo"], ev["quando"],
                                 "inquadra il QR per i dettagli")
    return {"ok": True, "pannello": arrivata}


@app.delete("/api/evento/{eid}")
async def togli_evento(eid: str):
    lista = leggi_eventi()
    resta = [e for e in lista if e["id"] != eid]
    for e in lista:
        if e["id"] == eid and e.get("immagine"):
            (IMMAGINI / e["immagine"]).unlink(missing_ok=True)
    scrivi_eventi(resta)
    return {"ok": True}


@app.get("/img/{nome}")
async def immagine(nome: str):
    f = IMMAGINI / pathlib.Path(nome).name     # niente percorsi fantasiosi
    if not f.exists():
        return JSONResponse({"errore": "non c'e'"}, status_code=404)
    return FileResponse(f)


@app.post("/api/menu")
async def rimetti_menu():
    """Rimette il QR del menu sul pannello, da qualunque schermata."""
    if not seriale_aperta:
        return {"ok": False, "motivo": "nodo non collegato"}
    try:
        seriale_aperta[0].write(b"Q\n")
        return {"ok": True}
    except Exception as e:
        return {"ok": False, "motivo": str(e)}


@app.post("/api/pausa")
async def pausa(dati: dict):
    """Quanti secondi ignorare il radar dopo "il pasto e' finito"."""
    if not seriale_aperta:
        return {"ok": False, "motivo": "nodo non collegato"}
    try:
        sec = max(0, min(600, int(dati.get("secondi", 5))))
    except (TypeError, ValueError):
        return {"ok": False, "motivo": "non e' un numero"}
    try:
        seriale_aperta[0].write(f"A:{sec}\n".encode())
        return {"ok": True, "secondi": sec}
    except Exception as e:
        return {"ok": False, "motivo": str(e)}


@app.post("/api/tavolo")
async def tavolo(dati: dict):
    """Nome, orario e ospite del tavolo: il nodo li inoltra al pannello."""
    if not seriale_aperta:
        return {"ok": False, "motivo": "nodo non collegato"}
    righe = []
    for chiave, lettera in (("nome", "N"), ("orario", "O"), ("ospite", "G")):
        testo = (dati.get(chiave) or "").strip()
        if testo:
            righe.append(f"{lettera}:{testo}\n".encode())
    if not righe:
        return {"ok": False, "motivo": "niente da mandare"}
    try:
        for r in righe:
            seriale_aperta[0].write(r)
            time.sleep(0.05)          # il pannello legge riga per riga
        return {"ok": True, "mandate": len(righe)}
    except Exception as e:
        return {"ok": False, "motivo": str(e)}


@app.get("/")
async def pagina() -> FileResponse:
    return FileResponse(QUI / "radar_live.html")


@app.get("/cassa")
async def cassa() -> FileResponse:
    """La vista per il ristoratore: cosa e' successo al tavolo, non il radar."""
    return FileResponse(QUI / "cassa.html")


@app.websocket("/ws/nodo")
async def ws_nodo(sock: WebSocket) -> None:
    """Il Mac col nodo attaccato si collega qui e fa da tramite."""
    global ponte_vivo
    await sock.accept()
    if SEGRETO:
        # Senza, chiunque conosca l'indirizzo potrebbe fingersi il tavolo e
        # riempire la cassa di ordini inventati.
        try:
            benvenuto = await asyncio.wait_for(sock.receive_json(), timeout=5)
        except Exception:
            await sock.close(code=4001); return
        if benvenuto.get("segreto") != SEGRETO:
            await sock.close(code=4003); return

    loop = asyncio.get_running_loop()
    seriale_aperta.clear(); seriale_aperta.append(Ponte(sock, loop))
    ponte_vivo = True
    await diffondi({"tipo": "stato", "collegato": True, "porta": "ponte"})
    lettore = Lettore(lambda m: asyncio.create_task(diffondi_o_accoda(m)))
    try:
        while True:
            msg = await sock.receive_json()
            if "righe" in msg:
                lettore.pezzo(msg["righe"])
    except Exception:
        pass
    finally:
        ponte_vivo = False
        seriale_aperta.clear()
        await diffondi({"tipo": "stato", "collegato": False})


async def diffondi_o_accoda(msg: dict) -> None:
    """Stessa strada dei messaggi che arrivano dalla seriale locale."""
    if msg.get("tipo") == "frame":
        ultimo.update(msg)
    elif msg.get("tipo") == "apertura":
        n = msg.get("coperti", 0)
        primo = not apertura
        if primo:
            apertura[:] = [time.time(), n]
        elif n == apertura[1]:
            return
        else:
            apertura[1] = n
        await diffondi(registra_evento("aperto" if primo else "coperti", coperti=n))
        await diffondi(stato_tavolo())
        return
    await diffondi(msg)


@app.websocket("/ws")
async def ws(sock: WebSocket) -> None:
    await sock.accept()
    clienti.add(sock)
    if ultima_pausa:                     # stato iniziale per chi arriva dopo
        await sock.send_json({"tipo": "pausa", "secondi": ultima_pausa[0]})
    for ev in eventi:                    # ...e tutto quello che e' gia' successo
        await sock.send_json(ev)
    await sock.send_json(stato_tavolo())
    if ordine_corrente:
        await sock.send_json({"tipo": "ordine_stato", **ordine_corrente})
    try:
        while True:
            await sock.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        clienti.discard(sock)


def main() -> None:
    import os
    import socket
    import sys

    # Sul server la 8080 puo' essere di qualcun altro: la si sceglie con PORTA.
    porta_http = int(os.environ.get("PORTA", "8080"))
    # Se 8080 e' gia' occupata (spesso una copia di questo stesso script rimasta
    # aperta), lo dico chiaro e passo alla prima porta libera invece di morire.
    def libera(n: int) -> bool:
        with socket.socket() as s_:
            s_.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                s_.bind(("127.0.0.1", n))
                return True
            except OSError:
                return False

    if not libera(porta_http):
        # Se a tenerla e' un'altra copia di questa dashboard, NON ne apro una
        # seconda: la seriale puo' aprirla un processo solo, e la seconda
        # resterebbe muta per sempre confondendo le idee.
        import subprocess
        try:
            altre = subprocess.run(["pgrep", "-f", "radar_live.py"],
                                   capture_output=True, text=True).stdout.split()
        except Exception:
            altre = []
        mie = [x for x in altre if int(x) != os.getpid()]

        if mie:
            print(f"⚠️  Hai gia' una dashboard aperta (PID {', '.join(mie)}).")
            print()
            print("   La porta seriale puo' usarla un processo solo, quindi una")
            print("   seconda copia resterebbe muta. Scegli:")
            print()
            print(f"   - usa quella gia' aperta:  http://localhost:{porta_http}")
            print("   - oppure chiudila (Ctrl-C nel suo terminale) e rilancia")
            sys.exit(0)

        for n in range(8081, 8091):
            if libera(n):
                porta_http = n
                print(f"⚠️  la {8080} e' occupata da qualcun altro: uso la {porta_http}.\n")
                break
        else:
            sys.exit("Nessuna porta libera fra 8080 e 8090.")

    p = trova_porta()
    print(f"porta seriale: {p or 'NON TROVATA (attacca il nodo, la cerco da solo)'}")
    PORTA_HTTP[:] = [porta_http]
    FILE_PID.write_text(str(os.getpid()))
    atexit.register(lambda: FILE_PID.unlink(missing_ok=True))
    ip = _ip_locale()
    print(f"dashboard:     http://localhost:{porta_http}")
    print(f"dal telefono:  http://{ip}:{porta_http}")
    # Sul Mac ascolto su tutte le interfacce, perche' il telefono deve poter
    # arrivare qui dalla rete di casa.
    #
    # Su un server pubblico NO: dietro nginx va ascoltato solo 127.0.0.1.
    # Altrimenti si arriva alla porta anche in diretta, saltando il proxy - e
    # senza l'intestazione che il proxy aggiunge il server crederebbe che il
    # visitatore sia in casa, aprendogli la cassa e i comandi.
    #   ASCOLTA=127.0.0.1
    ascolta = os.environ.get("ASCOLTA", "0.0.0.0")
    if ascolta != "127.0.0.1":
        print("           (in ascolto su tutta la rete; dietro un proxy usa ASCOLTA=127.0.0.1)")
    uvicorn.run(app, host=ascolta, port=porta_http, log_level="warning")


if __name__ == "__main__":
    main()
