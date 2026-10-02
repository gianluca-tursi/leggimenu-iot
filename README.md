# leggimenu — prototipo del tavolo intelligente

Un oggetto che sta al centro del tavolo di un ristorante. Si accorge che
qualcuno si è seduto, stima quante persone sono, mostra il QR del menu su uno
schermo e-ink, riceve l'ordine fatto dal telefono e lo manda alla cassa.

Stato: **prototipo funzionante**, in corsa per la Maker Faire.

## Cosa fa adesso

```
persona che si siede
   → radar la rileva
   → LED: una strisciata per persona, poi restano accesi tanti quanti i coperti
   → pannello e-ink: "Benvenuto <nome>" + QR del menu
   → la dashboard cassa vede il tavolo aprirsi

cliente che inquadra il QR
   → si apre il menu sul telefono
   → il pannello suggerisce il QR per entrare nel WiFi (una volta sola)
   → dopo 30 secondi torna da solo al QR del menu

cliente che ordina dal telefono
   → l'ordine arriva alla dashboard cassa
   → il pannello mostra "Ordine inviato in cucina" con la lista

cameriere che preme "Il pasto è finito"
   → tutto si azzera, 5 secondi di pausa, si ricomincia
```

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
| **Mac** | server, dashboard cassa, pagina del menu | USB al Freenove |

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

## I due Mac

Lo sviluppo si fa su una macchina sola; l'altra, quella che va in fiera, serve
solo a far girare il progetto e non ha Arduino ne' strumenti di sviluppo.

**Sul Mac di sviluppo**, dopo aver cambiato qualcosa:

```bash
git add -A && git commit -m "cosa ho cambiato" && git push
```

**Sul Mac della fiera**, per allinearsi:

```bash
./scripts/aggiorna.sh      # scarica, reinstalla se serve, riavvia il server
./scripts/stato.sh         # dice cosa funziona e cosa manca
```

`stato.sh` e' pensato per quando non c'e' nessuno a cui chiedere: ogni riga
dice se va o cosa fare.

**Il firmware delle schede non si aggiorna cosi'.** Arduino sta solo sul Mac di
sviluppo, quindi le schede vanno caricate **prima di partire**. Se durante la
fiera serve una modifica al firmware, serve il Mac di sviluppo sul posto.

## Uso quotidiano

```bash
./scripts/dashboard.sh avvia      # il server: http://localhost:8080
./scripts/dashboard.sh stato
./scripts/dashboard.sh ferma

./scripts/con-nodo.sh <comando>   # per tutto ciò che usa la porta seriale:
                                  # chiude la dashboard e la RIAPRE dopo
./scripts/scarica-log.sh          # scarica il registro dalla flash del nodo
```

La porta seriale la può tenere **un processo solo**. Su macOS due processi
possono aprirla senza errore e si dividono i byte: sembra un guasto del radar.
Per questo esiste `con-nodo.sh`.

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
firmware/pannello42/     il pannello e-ink 4.2"
tools/radar_live.py      server: dashboard, pagina del menu, ordini
tools/radar_live.html    la dashboard cassa
backend/wine.py          abbinamento vino — scritto e non ancora collegato
scripts/                 avvio, caricamento firmware, scarico registri
registrazioni/           prove sul campo, con etichetta
```
