# leggimenu — prototipo del tavolo intelligente

Un oggetto che sta al centro del tavolo di un ristorante. Si accorge che
qualcuno si è seduto, stima quante persone sono, mostra il QR del menu su uno
schermo e-ink, riceve l'ordine fatto dal telefono e lo manda alla cassa.

Stato: **prototipo funzionante**, in corsa per la Maker Faire.

## Cosa fa adesso

```
persona che si siede (o il cameriere: "Apri il tavolo a mano" dalla cassa)
   → LED: una strisciata per persona, poi restano accesi tanti quanti i coperti
   → pannello e-ink: "Benvenuto <nome>" + QR del menu
   → la cassa vede il tavolo aprirsi e conferma i coperti con un tocco

cliente che inquadra il QR
   → si apre il menu sul telefono (la cassa conta quanti telefoni diversi)
   → il pannello suggerisce il QR per entrare nel WiFi: 15 secondi, una volta sola

cliente che ordina dal telefono
   → l'ordine arriva alla cassa, con "in attesa da x minuti"
   → pannello: "Ordine inviato in cucina" con la lista, 15 secondi
   → se nell'ordine non c'è vino: il vino abbinato, 30 secondi

dalla cassa, in qualunque momento
   → offerta del giorno o evento sul pannello, 15 secondi, col QR del menu
   → "il conto": il pannello mostra il QR della recensione Google

cameriere che preme "Il pasto è finito"
   → tutto si azzera, 5 secondi di pausa, si ricomincia
```

Ogni schermata speciale torna da sola al QR del menu. Il tasto **MENU** del
pannello ce lo riporta subito; il tasto **EXIT** fa partire una **demo a
scorrimento** di tutte le schermate, 12 secondi l'una, che non dipende né dal
nodo né dalla rete: è la riserva per la fiera.

Il tavolo **si apre da solo ma non si chiude da solo**: al ristorante la gente
si alza per andare in bagno, e uno schermo che torna a "prenotato" mentre
stanno mangiando sarebbe una figuraccia.

## I pezzi

| pezzo | cosa fa | come è collegato |
|---|---|---|
| **Freenove ESP32** | legge il radar, comanda i LED, decide lo stato del tavolo | USB al Mac |
| **HLK-LD2450** | radar 24 GHz che vede le persone | 5V + GND + TX al Freenove |
| **Stick WS2812** | 8 LED, uno per coperto | 5V + GND + dato dal Freenove |
| **CrowPanel 4.2"** | pannello e-ink 400×300 | alimentato e comandato dal Freenove |
| **Mac** | ponte: rigira la seriale del nodo al server | USB al Freenove |
| **iot.leggimenu.it** | server: cassa, menu, ordini, eventi | il Mac ci arriva via internet |

Le due schede sono separate di proposito: il radar deve girare a 10 Hz senza
mai fermarsi, e un refresh e-ink blocca la scheda per **3,1 secondi**. Sullo
stesso processore perderemmo i frame proprio nel momento in cui qualcuno si
siede.

## Collegamenti

```
RADAR                          LED                      PANNELLO
Freenove 5V  -> 5V             Freenove 5V  -> VCC      Freenove 3V3 -> 3V3
Freenove GND -> GND            Freenove GND -> GND      Freenove GND -> GND
Freenove 16  <- TX             Freenove 13  -> IN       Freenove 25  -> IO18
Freenove 17  -> RX                                      Freenove 26  <- IO17
```

**Il radar va alimentato a 5V, non a 3,3.** A 3,3 funziona per qualche minuto e
poi comincia a perdere frame in modo intermittente: sembra un modulo difettoso
e non lo è.

**Mai l'USB nella CrowPanel mentre il filo 3V3 è collegato**: due regolatori
che si spingono l'uno contro l'altro. Per ricaricare il suo firmware, stacca
prima quel filo.

## Installazione su una macchina nuova

```bash
./scripts/installa.sh
```

Poi il driver della seriale, altrimenti il Mac non vede le schede:
<https://github.com/WCHSoftGroup/ch34xser_macos> (CH34xVCPDriver, il `.dmg` —
sull'App Store non c'è).

**Arduino non serve** se non devi modificare il firmware: le schede se lo
tengono. Se invece vuoi ricompilare, servono Arduino IDE, il core ESP32 e tre
librerie: GxEPD2, Adafruit GFX, Adafruit NeoPixel.

## Server, ponte e i due Mac

Il server sta su **iot.leggimenu.it** (cassa su `/cassa`, menu su `/menu/<tavolo>`).
Il Freenove non ha WiFi: il Mac a cui è attaccato fa da **ponte**, legge la
seriale e la rigira al server, e riporta indietro i comandi della cassa.

```bash
export LEGGIMENU_SEGRETO='...'     # non è nel repository: lo dà Gianluca
SERVER=wss://iot.leggimenu.it ./scripts/ponte.sh
```

Due Mac, due ruoli:

- **Mac di sviluppo** (Gianluca): si modifica il codice, si caricano i
  firmware, si pubblica il server con `./scripts/pubblica.sh`, si fa
  `git push`.
- **Mac della fiera**: solo `git pull` e il ponte. Istruzioni passo passo in
  [docs/MAC-FIERA.md](docs/MAC-FIERA.md).

**Il firmware delle schede non viaggia con git pull.** Va caricato dal Mac di
sviluppo prima di partire; `./scripts/versioni.sh` dice se le schede hanno
l'ultima versione.

## Il server sul Mac (senza internet)

Il vecchio schema funziona ancora: server sul Mac, nodo sull'USB, niente
ponte. Serve per provare in locale.

```bash
./scripts/dashboard.sh avvia      # http://localhost:8080
./scripts/dashboard.sh stato
./scripts/dashboard.sh ferma

./scripts/con-nodo.sh <comando>   # per tutto ciò che usa la porta seriale:
                                  # chiude la dashboard e la RIAPRE dopo
./scripts/scarica-log.sh          # scarica il registro dalla flash del nodo
```

La porta seriale la può tenere **un processo solo**: mai dashboard locale e
ponte insieme. Su macOS due processi possono aprirla senza errore e si
dividono i byte: sembra un guasto del radar.

## Il problema aperto

**Il radar vede una persona sola anche quando ce ne sono tre.** Misurato: in
5437 frame registrati non ha mai mandato due bersagli entro 90 cm.

Non è un bug del codice, sono due limiti fisici dell'LD2450:

- il cono è **120°, non 360°**: al centro del tavolo chi siede fuori dal
  ventaglio non esiste
- separa i bersagli **per distanza o per velocità**, non per angolo: attorno a
  un tavolo rotondo sono tutti alla stessa distanza, che è il caso peggiore

Le strade possibili sono in [CLAUDE.md](CLAUDE.md).

## Com'è fatto

```
firmware/radar_luce/     il nodo: radar, LED, stato del tavolo, registro a bordo
firmware/pannello42/     il pannello e-ink 4.2": tutte le schermate e la demo
tools/radar_live.py      server: cassa, menu, ordini, eventi, vino
tools/cassa.html         la cassa del ristoratore (/cassa)
tools/radar_live.html    la dashboard tecnica del radar (/)
tools/ponte.py           il ponte nodo → server
backend/wine.py          abbinamento vino
scripts/                 ponte, pubblicazione, caricamento firmware, registri
docs/                    hardware, server, Mac della fiera
registrazioni/           prove sul campo, con etichetta
```
