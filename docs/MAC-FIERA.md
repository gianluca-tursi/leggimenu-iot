# Il Mac della fiera

Questo Mac fa **una cosa sola**: il ponte fra il nodo attaccato via USB e il
server su internet. Cassa, menu, ordini, eventi vivono tutti su
**iot.leggimenu.it**, non qui.

```
radar + LED + pannello            questo Mac                 iot.leggimenu.it
   Freenove ESP32   ---USB--->    scripts/ponte.sh   --->    cassa, menu, ordini
                                                               ^
                                       telefoni dei clienti ---+
```

Lo sviluppo, firmware compreso, si fa sull'altro Mac, quello di Gianluca.
Qui si scaricano solo le novità, con `./scripts/aggiorna.sh`.

## Una volta sola

```bash
git clone https://github.com/gianluca-tursi/leggimenu-iot.git
cd leggimenu-iot
./scripts/installa.sh
```

Poi il driver della seriale CH340, altrimenti il Mac non vede il Freenove:
<https://github.com/WCHSoftGroup/ch34xser_macos> (il `.dmg`, sull'App Store non
c'è). Dopo l'installazione macOS chiede di autorizzarlo in Impostazioni >
Privacy e sicurezza.

**Il segreto del ponte non è nel repository.** È una riga che te la dà
Gianluca a voce o in privato. Non va scritto in nessun file del progetto: il
repository su GitHub è pubblico.

## Ogni volta che si accende

1. Freenove nell'USB del Mac. È la scheda con i LED: il pannello e-ink è
   alimentato da lei col filo 3V3, non ha bisogno del suo USB.
2. Nel Terminale, dentro la cartella del progetto:

```bash
./scripts/aggiorna.sh
```

```bash
export LEGGIMENU_SEGRETO='...'
```

```bash
SERVER=wss://iot.leggimenu.it ./scripts/ponte.sh
```

Deve rispondere così, e restare aperto:

```
ponte leggimenu · server: wss://iot.leggimenu.it
· collegato a wss://iot.leggimenu.it/ws/nodo
· nodo su /dev/cu.wchusbserial...
```

3. Dal telefono o dal browser: **https://iot.leggimenu.it/cassa**. La password
   della cassa te la dà Gianluca, come il segreto.

Per controllare che sia tutto a posto, in un altro Terminale:

```bash
./scripts/stato.sh
```

Dice riga per riga se vanno il nodo, il ponte e il server, e cosa fare se no.

Il ponte tiene sveglio il Mac da solo (`caffeinate`). Se cade la rete o si
stacca il cavo riprova ogni 30 secondi: non serve riavviarlo a mano.

## Cosa NON fare qui

- **Non avviare la dashboard locale** (`./scripts/dashboard.sh avvia`) mentre
  gira il ponte. Aprirebbero tutti e due la stessa porta seriale, e su macOS
  due processi si dividono i byte senza dare errore: nessun frame arriva intero
  e sembra il radar rotto.
- **Non caricare firmware.** Le schede si caricano dal Mac di sviluppo prima
  di partire.
- **Mai l'USB nella CrowPanel mentre il filo 3V3 è collegato**: due
  regolatori che si spingono l'uno contro l'altro.

## Se la rete salta: la demo

Sul pannello, il tasto **EXIT** (quello dall'altra parte rispetto a MENU) fa
girare tutte le schermate, 12 secondi l'una, in loop: tavolo prenotato, menu,
WiFi, ordine, vino, offerta, evento, recensione. I contenuti sono scritti nel
pannello: non servono il ponte, il server né il nodo. Si esce con EXIT o MENU.

## Se qualcosa non va

| cosa vedi | cosa fare |
|---|---|
| `nessun nodo sull'USB` | il Freenove non è attaccato, il cavo è solo di ricarica, o manca il driver CH340 |
| il ponte si collega e cade subito | il segreto è sbagliato, o non è stato fatto `export` nello stesso Terminale |
| la cassa dice "dispositivo non risponde" | il ponte è fermo: guarda il Terminale |
| il radar non vede nessuno | la cassa ha **"Apri il tavolo a mano"**: tutto il resto funziona lo stesso |
| il pannello sembra fermo | MENU rimette il QR del menu; EXIT parte la demo |

Per chi apre il progetto con Claude su questo Mac: le note complete sono in
[CLAUDE.md](../CLAUDE.md).
